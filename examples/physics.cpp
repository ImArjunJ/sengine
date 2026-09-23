#include <iostream>
#include <sengine/application.hpp>
#include <sengine/native_hud.hpp>
#include <sengine/physics.hpp>
#include <sengine/primitives.hpp>
#include <sengine/renderer.hpp>
#include <sengine/scene_loader.hpp>
#include <sengine/world_renderer.hpp>

namespace {
using namespace sengine;
struct surface {
    float4 color{1};
};
scene_registry components() {
    scene_registry registry;
    register_physics_components(registry);
    component_schema<surface> schema;
    schema.field("color", &surface::color);
    registry.add("surface", std::move(schema));
    return registry;
}
class room {
  public:
    room(std::unique_ptr<scene_world> content, renderer& graphics, const asset_store& assets)
        : content_(std::move(content)), physics_(content_->entities()), resources_(graphics),
          meshes_(content_->entities(), resources_) {
        player_ = content_->find("player");
        if (!content_->entities().get<character_body>(player_))
            throw std::invalid_argument("Room needs a player character");
        const auto material = load_material(resources_, assets.resolve("materials/surface.filamat"));
        for (auto id : content_->entities().entities()) {
            const auto* appearance = content_->entities().get<surface>(id);
            if (!appearance)
                continue;
            const auto* body = content_->entities().get<rigid_body>(id);
            const auto geometry =
                body ? body->geometry() : collider{.kind = collider_kind::sphere, .radius = .4f};
            const auto mesh =
                upload_mesh(resources_, geometry.kind == collider_kind::box ? box_mesh(geometry.half_extent)
                                                                            : sphere_mesh(geometry.radius));
            const auto painted = duplicate_material(resources_, material);
            set_color(resources_, painted, "base_color", appearance->color);
            set_parameter(resources_, painted, "roughness", .6f);
            meshes_.mesh(id, mesh, painted);
        }
    }
    void advance(float2 direction, bool jump) {
        physics_.walk(player_, {direction.x * 3, direction.y * 3}, jump);
        physics_.step();
    }
    void synchronize() { meshes_.synchronize(); }

  private:
    std::unique_ptr<scene_world> content_;
    physics_world physics_;
    scene resources_;
    world_renderer meshes_;
    entity player_;
};
class playground final : public runtime_scene {
  public:
    playground(application& host, std::filesystem::path capture)
        : host_(host), graphics_(host.display()), lighting_(graphics_),
          assets_(window::executable_directory()), loader_(registry_, assets_),
          hud_(graphics_, assets_.resolve("materials/hud.filamat"),
               assets_.resolve("data/fonts/DM-Sans.ttf")),
          capture_(std::move(capture)) {
        graphics_.configure({.aperture = 8, .shutter = 1.f / 90, .sensitivity = 100, .occlusion = true});
        add_sun(lighting_, {.color = {1, .94f, .85f}, .intensity = 18000, .direction = {-.4f, -1, -.6f}});
        set_environment(lighting_, {.intensity = 14000});
        bind("horizontal", {{.key = key_code::a, .scale = -1}, {.key = key_code::d}});
        bind("vertical", {{.key = key_code::w, .scale = -1}, {.key = key_code::s}});
        bind("jump", {{.key = key_code::space}});
        reload();
    }
    bool event(const input_event& event) override {
        if (event.type == event_type::key_down && !event.key.repeat) {
            try {
                if (event.key.code == key_code::r)
                    reload();
                else if (event.key.code == key_code::escape)
                    host_.scenes().quit();
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
            }
        }
        return false;
    }
    void fixed_update(runtime_step) override {
        const auto direction = fixed_input().axis_pair("horizontal", "vertical");
        room_->advance({direction[0], direction[1]}, fixed_input().action("jump").pressed);
        ++steps_;
    }
    void update(const runtime_frame& frame) override {
        elapsed_ += frame.seconds;
        if (!capture_.empty() && elapsed_ > 60)
            throw std::runtime_error("Capture timed out");
    }
    void render(const runtime_frame&) override {
        room_->synchronize();
        const auto& size = host_.viewport();
        hud_.begin(size.width, size.height, size.scale);
        hud_.text(28, 24, "Physics", 28, {.08f, .12f, .16f});
        hud_.text(28, 64, "WASD move   Space jump   R reload   Esc quit", 15, {.18f, .25f, .30f});
        const bool capture = !capture_.empty() && steps_ >= 120;
        if (graphics_.frame(camera_, size.width, size.height, .1f, 80, &hud_,
                            capture ? capture_ : std::filesystem::path{}) &&
            capture)
            host_.scenes().quit();
    }

  private:
    void reload() {
        auto next = std::make_unique<room>(loader_.load("data/physics/room.json"), graphics_, assets_);
        room_ = std::move(next);
        steps_ = 0;
    }

  private:
    application& host_;
    renderer graphics_;
    scene lighting_;
    asset_store assets_;
    scene_registry registry_{components()};
    scene_loader loader_;
    native_hud hud_;
    std::unique_ptr<room> room_;
    camera_view camera_{.eye = {8, 10, 12}, .direction = {-8, -9, -12}, .fov = 48};
    std::filesystem::path capture_;
    double elapsed_{};
    unsigned steps_{};
};
}
int main(int argc, char** argv) {
    try {
        application host({.title = "sengine / physics", .width = 1200, .height = 800});
        host.scenes().emplace<playground>(host, argc > 1 ? argv[1] : "");
        host.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
