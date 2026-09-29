#include "sengine/game_scene.hpp"
#include "surface_material.hpp"
#include <cmath>
#include <optional>
#include <stdexcept>

namespace sengine {
namespace {
void validate_planes(float near_plane, float far_plane) {
    if (!std::isfinite(near_plane) || !std::isfinite(far_plane) || near_plane <= 0 || far_plane <= near_plane)
        throw std::invalid_argument("Camera clipping planes require 0 < near < far");
}
}
namespace detail {
class scene_content {
  public:
    scene_content(renderer& graphics, const scene& target, const asset_store& assets,
                  std::unique_ptr<scene_world> world)
        : world_(std::move(world)), resources_(graphics, target),
          models_(world_->entities(), resources_, assets) {}
    scene_world& world() noexcept { return *world_; }
    void require_owner() const {
        if (std::this_thread::get_id() != owner_)
            throw std::logic_error("Scene objects belong to their creating thread");
    }
    material_id surface(const surface_options& options) {
        if (!surface_)
            surface_ = make_surface(resources_);
        const auto result = duplicate_material(resources_, *surface_);
        set_color(resources_, result, "base_color", options.color);
        set_parameter(resources_, result, "roughness", options.roughness);
        set_parameter(resources_, result, "metallic", options.metallic);
        return result;
    }
    void light(entity id, const light_options& options) {
        auto found = lights_.find(id);
        if (found != lights_.end() && found->second.options.kind == options.kind) {
            update_light(resources_, found->second.node, options);
            found->second.options = options;
            return;
        }
        const auto node = add_light(resources_, options);
        try {
            set_transform(resources_, node, world_->entities().world_transform(id));
            if (found != lights_.end()) {
                visible(resources_, node, found->second.visible);
                destroy_node(resources_, found->second.node);
                found->second = {node, options, {}, found->second.visible};
            } else {
                lights_.emplace(id, light_record{node, options, {}});
            }
        } catch (...) {
            destroy_node(resources_, node);
            throw;
        }
    }
    light_options light(entity id) const {
        const auto found = lights_.find(id);
        if (found == lights_.end())
            throw std::logic_error("Object has no light");
        return found->second.options;
    }
    void show(entity id, bool shown) {
        const bool rendered = models_.contains(id);
        const auto light = lights_.find(id);
        if (!rendered && light == lights_.end())
            throw std::logic_error("Object has no model, mesh or light");
        if (rendered)
            models_.show(id, shown);
        if (light != lights_.end()) {
            visible(resources_, light->second.node, shown);
            light->second.visible = shown;
        }
    }
    void advance(double seconds) {
        models_.advance(seconds);
        const transform_scope scope(resources_);
        for (auto it = lights_.begin(); it != lights_.end();) {
            if (!world_->entities().alive(it->first)) {
                destroy_node(resources_, it->second.node);
                it = lights_.erase(it);
            } else {
                const auto transform = world_->entities().world_transform(it->first);
                if (transform != it->second.transform) {
                    set_transform(resources_, it->second.node, transform);
                    it->second.transform = transform;
                }
                ++it;
            }
        }
    }
    scene& resources() noexcept { return resources_; }
    world_renderer& models() noexcept { return models_; }
#if SENGINE_HAS_PHYSICS
    physics_world& enable_physics(const physics_options& options) {
        auto next = std::make_unique<physics_world>(world_->entities(), options);
        next->synchronize();
        physics_ = std::move(next);
        return *physics_;
    }
    physics_world& physics() {
        if (!physics_)
            throw std::logic_error("Call enable_physics() before accessing scene physics");
        return *physics_;
    }
    void step() {
        if (physics_)
            physics_->step();
    }
    void disable_physics() noexcept { physics_.reset(); }
#endif

  private:
    struct light_record {
        scene_node node;
        light_options options;
        mat4 transform;
        bool visible{true};
    };

  private:
    const std::thread::id owner_{std::this_thread::get_id()};
    std::unique_ptr<scene_world> world_;
    scene resources_;
    world_renderer models_;
    std::optional<material_id> surface_;
    std::map<entity, light_record> lights_;
#if SENGINE_HAS_PHYSICS
    std::unique_ptr<physics_world> physics_;
#endif
};
}
using detail::scene_content;
struct game_scene::impl {
  public:
    impl(application& host, game_scene_options options)
        : host(host), graphics(host.graphics()), resources(graphics, scene_target::isolated),
          assets(options.asset_root.empty() ? window::executable_directory() : options.asset_root),
          loader(registry, assets), options(std::move(options)) {
        validate_planes(this->options.near_plane, this->options.far_plane);
        register_model_components(registry);
        register_physics_components(registry);
        if (this->options.daylight) {
            sun = add_sun(resources, {.direction = {-.4f, -1, -.6f}});
            set_environment(resources, {});
        }
        content =
            std::make_shared<scene_content>(graphics, resources, assets, std::make_unique<scene_world>());
    }

  public:
    application& host;
    renderer& graphics;
    scene resources;
    asset_store assets;
    scene_registry registry;
    scene_loader loader;
    game_scene_options options;
    std::shared_ptr<scene_content> content;
    scene_node sun;
    std::optional<physics_options> physics_settings;
    std::string source;
    native_hud* overlay{};
    std::filesystem::path capture;
};
game_scene::game_scene(application& host, game_scene_options options)
    : impl_(std::make_unique<impl>(host, std::move(options))) {}
game_scene::~game_scene() = default;
application& game_scene::host() noexcept {
    return impl_->host;
}
renderer& game_scene::graphics() noexcept {
    return impl_->graphics;
}
void game_scene::configure(const render_options& options) {
    impl_->options.rendering = options;
}
void game_scene::visible_layers(std::uint8_t layers) {
    impl_->options.visible_layers = layers;
}
asset_store& game_scene::assets() noexcept {
    return impl_->assets;
}
scene_registry& game_scene::components() noexcept {
    return impl_->registry;
}
world& game_scene::entities() noexcept {
    return impl_->content->world().entities();
}
scene& game_scene::resources() noexcept {
    return impl_->content->resources();
}
world_renderer& game_scene::models() noexcept {
    return impl_->content->models();
}
const camera_view& game_scene::camera() const noexcept {
    return impl_->options.camera;
}
void game_scene::camera(const camera_view& value) {
    impl_->options.camera = value;
}
void game_scene::clip_planes(float near_plane, float far_plane) {
    validate_planes(near_plane, far_plane);
    impl_->options.near_plane = near_plane;
    impl_->options.far_plane = far_plane;
}
void game_scene::load(const std::string& uri) {
    auto source = uri;
    auto next = std::make_shared<scene_content>(impl_->graphics, impl_->resources, impl_->assets,
                                                impl_->loader.load(uri));
#if SENGINE_HAS_PHYSICS
    if (impl_->physics_settings)
        next->enable_physics(*impl_->physics_settings);
#endif
    impl_->content = std::move(next);
    impl_->source = std::move(source);
}
void game_scene::reload() {
    if (impl_->source.empty())
        throw std::logic_error("Call load() before reloading scene content");
    load(impl_->source);
}
game_object game_scene::find(const std::string& name) const {
    const auto id = impl_->content->world().find(name);
    if (!id)
        throw std::out_of_range("Scene has no object named '" + name + "'");
    return object(id);
}
game_object game_scene::object(entity id) const {
    impl_->content->require_owner();
    if (!impl_->content->world().entities().alive(id))
        throw std::invalid_argument("Cannot reference a stale or foreign scene object");
    return game_object(impl_->content, id);
}
game_object game_scene::create(std::string name) {
    impl_->content->require_owner();
    const auto id = entities().create(name);
    try {
        if (!name.empty())
            impl_->content->world().identify(id, std::move(name));
    } catch (...) {
        entities().destroy(id);
        throw;
    }
    return object(id);
}
game_object game_scene::create_model(const std::string& source, std::string name) {
    auto result = create(std::move(name));
    try {
        result.add<model_component>(model_component{.source = asset_reference(source), .offset = {}});
        models().attach(result.id());
    } catch (...) {
        result.destroy();
        throw;
    }
    return result;
}
game_object game_scene::create_mesh(const geometry& shape, const material& surface, std::string name) {
    if (shape.owner_.lock() != impl_->content || surface.owner_.lock() != impl_->content)
        throw std::invalid_argument("Mesh geometry and material must belong to this scene");
    auto result = create(std::move(name));
    try {
        models().mesh(result.id(), shape.id_, surface.id_);
    } catch (...) {
        result.destroy();
        throw;
    }
    return result;
}
game_object game_scene::create_mesh(const mesh_data& shape, const material& surface, std::string name) {
    if (surface.owner_.lock() != impl_->content)
        throw std::invalid_argument("Mesh material must belong to this scene");
    return create_mesh(upload(shape), surface, std::move(name));
}
game_object game_scene::create_light(const light_options& options, std::string name) {
    auto result = create(std::move(name));
    try {
        result.light(options);
    } catch (...) {
        result.destroy();
        throw;
    }
    return result;
}
geometry game_scene::upload(const mesh_data& mesh) {
    impl_->content->require_owner();
    return geometry(impl_->content, upload_mesh(resources(), mesh));
}
void game_scene::sun(const sun_options& options) {
    const auto next = add_sun(impl_->resources, options);
    if (impl_->sun)
        destroy_node(impl_->resources, impl_->sun);
    impl_->sun = next;
}
void game_scene::environment(const environment_options& options) {
    set_environment(impl_->resources, options);
}
physics_world& game_scene::enable_physics(physics_options options) {
#if SENGINE_HAS_PHYSICS
    options.fixed_step = impl_->host.fixed_step();
    auto& result = impl_->content->enable_physics(options);
    impl_->physics_settings = options;
    return result;
#else
    (void)options;
    throw std::logic_error("Physics support is disabled; configure sengine_physics=ON");
#endif
}
physics_world& game_scene::physics() {
#if SENGINE_HAS_PHYSICS
    return impl_->content->physics();
#else
    throw std::logic_error("Physics support is disabled; configure sengine_physics=ON");
#endif
}
void game_scene::disable_physics() noexcept {
#if SENGINE_HAS_PHYSICS
    impl_->content->disable_physics();
#endif
    impl_->physics_settings.reset();
}
void game_scene::overlay(native_hud& value) noexcept {
    impl_->overlay = &value;
}
void game_scene::clear_overlay() noexcept {
    impl_->overlay = nullptr;
}
void game_scene::capture(std::filesystem::path path) {
    if (path.empty())
        throw std::invalid_argument("A capture needs an output path");
    impl_->capture = std::move(path);
}
bool game_scene::capture_pending() const noexcept {
    return !impl_->capture.empty();
}
void game_scene::quit() noexcept {
    impl_->host.scenes().quit();
}
void game_scene::run_fixed_update(runtime_step step) {
    fixed_update(step);
    entities().flush();
#if SENGINE_HAS_PHYSICS
    impl_->content->step();
#endif
}
void game_scene::run_update(const runtime_frame& frame) {
    update(frame);
    entities().flush();
    impl_->content->advance(frame.seconds);
}
void game_scene::run_render(const runtime_frame& frame) {
    impl_->graphics.configure(impl_->options.rendering);
    impl_->graphics.visible_layers(impl_->options.visible_layers);
    render(frame);
    const auto& size = impl_->host.viewport();
    if (impl_->graphics.frame(impl_->resources, impl_->options.camera, size.width, size.height,
                              impl_->options.near_plane, impl_->options.far_plane, impl_->overlay,
                              impl_->capture)) {
        impl_->capture.clear();
        presented(frame);
    }
}
namespace {
void validate_color(float4 color) {
    if (!detail::finite(color) || color.x < 0 || color.y < 0 || color.z < 0 || color.w < 0 || color.w > 1)
        throw std::invalid_argument(
            "Surface color must be finite, with nonnegative RGB and alpha from 0 to 1");
}
void validate_fraction(float value, const char* property) {
    if (!std::isfinite(value) || value < 0 || value > 1)
        throw std::invalid_argument(std::string(property) + " must range from 0 to 1");
}
}
material game_scene::surface(surface_options options) {
    impl_->content->require_owner();
    validate_color(options.color);
    validate_fraction(options.roughness, "Roughness");
    validate_fraction(options.metallic, "Metallic");
    return material(impl_->content, impl_->content->surface(options));
}
material::material(const std::shared_ptr<scene_content>& owner, material_id id) : owner_(owner), id_(id) {}
bool material::alive() const noexcept {
    return !owner_.expired();
}
std::shared_ptr<scene_content> material::require() const {
    auto owner = owner_.lock();
    if (!owner)
        throw std::logic_error("Material's scene has been released or replaced");
    owner->require_owner();
    return owner;
}
void material::color(float4 value) {
    const auto owner = require();
    validate_color(value);
    set_color(owner->resources(), id_, "base_color", value);
}
void material::roughness(float value) {
    const auto owner = require();
    validate_fraction(value, "Roughness");
    set_parameter(owner->resources(), id_, "roughness", value);
}
void material::metallic(float value) {
    const auto owner = require();
    validate_fraction(value, "Metallic");
    set_parameter(owner->resources(), id_, "metallic", value);
}
material material::clone() const {
    const auto owner = require();
    return material(owner, duplicate_material(owner->resources(), id_));
}
geometry::geometry(const std::shared_ptr<scene_content>& owner, mesh_id id) : owner_(owner), id_(id) {}
bool geometry::alive() const noexcept {
    return !owner_.expired();
}
game_object::game_object(const std::shared_ptr<scene_content>& owner, entity id) : owner_(owner), id_(id) {}
bool game_object::alive() const noexcept {
    const auto owner = owner_.lock();
    return owner && owner->world().entities().alive(id_);
}
std::shared_ptr<scene_content> game_object::require() const {
    auto owner = owner_.lock();
    if (!owner)
        throw std::logic_error("Object's scene has been released or replaced");
    owner->require_owner();
    if (!owner->world().entities().alive(id_))
        throw std::logic_error("Object has been destroyed");
    return owner;
}
world& game_object::entities() const {
    return require()->world().entities();
}
entity game_object::id() const {
    require();
    return id_;
}
void game_object::destroy() {
    entities().destroy(id_);
}
std::string game_object::name() const {
    return entities().name(id_);
}
mat4 game_object::transform() const {
    return entities().local_transform(id_);
}
void game_object::transform(const mat4& value) {
    entities().set_local_transform(id_, value);
}
float3 game_object::position() const {
    const auto value = transform()[3];
    return {value.x, value.y, value.z};
}
void game_object::position(float3 value) {
    auto next = transform();
    next[3] = {value.x, value.y, value.z, 1};
    transform(next);
}
void game_object::parent(const game_object& parent, bool preserve_world) {
    const auto owner = require();
    if (parent.require() != owner)
        throw std::invalid_argument("Parent must belong to the same scene");
    owner->world().entities().reparent(id_, parent.id_, preserve_world);
}
void game_object::visible(bool visible) {
    require()->show(id_, visible);
}
void game_object::material(const sengine::material& value) {
    const auto owner = require();
    if (value.require() != owner)
        throw std::invalid_argument("Material must belong to the object's scene");
    owner->models().material(id_, value.id_);
}
void game_object::material(std::string_view node, const sengine::material& value, unsigned slot) {
    const auto owner = require();
    if (value.require() != owner)
        throw std::invalid_argument("Material must belong to the object's scene");
    auto& instance = owner->models().model(id_);
    instance.material(instance.find(node), value.id_, slot);
}
void game_object::play(std::string clip, double transition, playback_mode mode) {
    require()->models().play(id_, std::move(clip), transition, mode);
}
void game_object::pause(bool paused) {
    get<model_animation>().paused = paused;
}
void game_object::reload() {
    const auto owner = require();
    owner->models().reload(get<model_component>().source.uri());
}
std::string game_object::animation() const {
    return get<model_animation>().clip;
}
model_instance& game_object::model() {
    return require()->models().model(id_);
}
void game_object::light(const light_options& options) {
    require()->light(id_, options);
}
light_options game_object::light() const {
    return require()->light(id_);
}
void game_object::walk(float2 velocity, bool jump) {
#if SENGINE_HAS_PHYSICS
    require()->physics().walk(id_, velocity, jump);
#else
    (void)velocity;
    (void)jump;
    throw std::logic_error("Physics support is disabled; configure sengine_physics=ON");
#endif
}
void game_object::impulse(float3 value) {
#if SENGINE_HAS_PHYSICS
    require()->physics().impulse(id_, value);
#else
    (void)value;
    throw std::logic_error("Physics support is disabled; configure sengine_physics=ON");
#endif
}
}
