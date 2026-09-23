#include <algorithm>
#include <iostream>
#include <sengine/application.hpp>
#include <sengine/native_hud.hpp>
#include <sengine/primitives.hpp>
#include <sengine/renderer.hpp>
#include <sengine/scene_loader.hpp>
#include <sengine/world_renderer.hpp>

namespace {
using namespace sengine;
struct surface {
    asset_reference material;
    float4 color{1};
};
struct pickup {};
scene_registry components() {
    scene_registry registry;
    component_schema<surface> schema;
    schema.field("material", &surface::material).field("color", &surface::color);
    registry.add("surface", std::move(schema));
    registry.add("pickup", component_schema<pickup>{});
    return registry;
}
class level {
  public:
    level(std::unique_ptr<scene_world> content, renderer& graphics, const asset_store& assets)
        : content_(std::move(content)), resources_(graphics), meshes_(content_->entities(), resources_) {
        player_ = content_->find("player");
        if (!player_)
            throw std::invalid_argument("Level needs a player entity");
        const auto cube = upload_mesh(resources_, box_mesh());
        std::map<std::string, material_id> materials;
        for (auto id : content_->entities().entities()) {
            if (const auto* appearance = content_->entities().get<surface>(id)) {
                const auto& uri = appearance->material.uri();
                if (!materials.contains(uri))
                    materials.emplace(uri, load_material(resources_, assets.resolve(uri)));
                const auto material = duplicate_material(resources_, materials.at(uri));
                set_color(resources_, material, "base_color", appearance->color);
                set_parameter(resources_, material, "roughness", .6f);
                meshes_.mesh(id, cube, material);
            }
            if (content_->entities().get<pickup>(id))
                cells_.push_back(id);
        }
        total_ = cells_.size();
    }
    void advance(float2 direction, double seconds) {
        auto& world = content_->entities();
        auto pose = world.world_transform(player_);
        pose[3].x = std::clamp(pose[3].x + direction.x * float(seconds) * 3.4f, -4.4f, 4.4f);
        pose[3].z = std::clamp(pose[3].z + direction.y * float(seconds) * 3.4f, -4.4f, 4.4f);
        world.set_world_transform(player_, pose);
        for (auto it = cells_.begin(); it != cells_.end();) {
            const auto position = world.world_transform(*it)[3];
            if (std::hypot(pose[3].x - position.x, pose[3].z - position.z) < .6f) {
                world.destroy(*it);
                it = cells_.erase(it);
            } else {
                ++it;
            }
        }
    }
    void synchronize() { meshes_.synchronize(); }
    std::string progress() const {
        return cells_.empty() ? "Complete"
                              : std::to_string(total_ - cells_.size()) + " / " + std::to_string(total_);
    }

  private:
    std::unique_ptr<scene_world> content_;
    scene resources_;
    world_renderer meshes_;
    entity player_;
    std::vector<entity> cells_;
    std::size_t total_{};
};
class relay final : public runtime_scene {
  public:
    relay(application& host, std::filesystem::path capture, unsigned selected)
        : host_(host), graphics_(host.display()), lighting_(graphics_),
          assets_(window::executable_directory()), loader_(registry_, assets_),
          hud_(graphics_, assets_.resolve("materials/hud.filamat"),
               assets_.resolve("data/fonts/DM-Sans.ttf")),
          capture_(std::move(capture)), selected_(selected) {
        graphics_.configure({.aperture = 8, .shutter = 1.f / 90, .sensitivity = 100, .occlusion = true});
        add_sun(lighting_, {.color = {1, .94f, .85f}, .intensity = 18000, .direction = {-.4f, -1, -.6f}});
        set_environment(lighting_, {.intensity = 14000});
        bind("horizontal", {{.key = key_code::a, .scale = -1}, {.key = key_code::d}});
        bind("vertical", {{.key = key_code::w, .scale = -1}, {.key = key_code::s}});
        reload(selected_);
    }
    bool event(const input_event& event) override {
        if (event.type != event_type::key_down || event.key.repeat)
            return false;
        try {
            if (event.key.code == key_code::r)
                reload(selected_);
            else if (event.key.code == key_code::tab)
                reload(selected_ == 1 ? 2 : 1);
            else if (event.key.code == key_code::escape)
                host_.scenes().quit();
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            status_ = "Reload failed; see terminal";
        }
        return false;
    }
    void fixed_update(runtime_step step) override {
        const auto direction = fixed_input().axis_pair("horizontal", "vertical");
        level_->advance({direction[0], direction[1]}, step.seconds);
    }
    void update(const runtime_frame& frame) override {
        elapsed_ += frame.seconds;
        if (!capture_.empty() && elapsed_ > 60)
            throw std::runtime_error("Capture timed out");
    }
    void render(const runtime_frame&) override {
        level_->synchronize();
        const auto& size = host_.viewport();
        hud_.begin(size.width, size.height, size.scale);
        hud_.rectangle(0, 0, hud_.width(), 88, {.035f, .055f, .08f, .96f});
        hud_.text(28, 20, "Relay / " + std::to_string(selected_), 28, {.92f, .96f, 1});
        hud_.text(hud_.width() - 170, 24, level_->progress(), 24, {1, .75f, .35f});
        hud_.text(28, 60, status_, 15, {.68f, .78f, .86f});
        const bool capture = !capture_.empty() && frames_ >= 60;
        if (graphics_.frame(camera_, size.width, size.height, .1f, 80, &hud_,
                            capture ? capture_ : std::filesystem::path{})) {
            ++frames_;
            if (capture)
                host_.scenes().quit();
        }
    }

  private:
    void reload(unsigned selected) {
        if (selected < 1 || selected > 2)
            throw std::invalid_argument("Choose level 1 or 2");
        auto content = loader_.load("data/relay/level-" + std::to_string(selected) + ".json");
        auto next = std::make_unique<level>(std::move(content), graphics_, assets_);
        level_ = std::move(next);
        selected_ = selected;
        status_ = "WASD move   Tab level   R reload   Esc quit";
    }

  private:
    application& host_;
    renderer graphics_;
    scene lighting_;
    asset_store assets_;
    scene_registry registry_{components()};
    scene_loader loader_;
    native_hud hud_;
    std::unique_ptr<level> level_;
    camera_view camera_{.eye = {0, 12, 11}, .direction = {0, -12, -11}, .fov = 48};
    std::filesystem::path capture_;
    std::string status_;
    unsigned selected_, frames_{};
    double elapsed_{};
};
}
int main(int argc, char** argv) {
    try {
        application host({.title = "sengine / relay", .width = 1200, .height = 800});
        host.scenes().emplace<relay>(host, argc > 1 ? argv[1] : "", argc > 2 ? std::stoi(argv[2]) : 1);
        host.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
