#include <chrono>
#include <functional>
#include <iostream>
#include <numbers>
#include <sengine/animation.hpp>
#include <sengine/input.hpp>
#include <sengine/primitives.hpp>
#include <sengine/renderer.hpp>
#include <sengine/timing.hpp>
#include <sengine/world.hpp>

namespace {
using namespace sengine;
struct drawable {
    scene_node node;
};
struct spinning {
    float phase{}, rate{}, radius{}, height{};
};
class gallery {
  public:
    explicit gallery(const std::filesystem::path& material)
        : window_("sengine / gallery / space to pause / tab for projection", 1200, 800), renderer_(window_),
          scene_(renderer_) {
        renderer_.configure({.aperture = 8, .shutter = 1.f / 90, .sensitivity = 100});
        input_.define("pause", {{.key = key_code::space}});
        input_.define("projection", {{.key = key_code::tab}});
        input_.define("exit", {{.key = key_code::escape}});
        build_scene(material);
    }
    int run(const std::filesystem::path& capture, bool orthographic) {
        camera_.projection = orthographic ? projection_kind::orthographic : projection_kind::perspective;
        auto previous = std::chrono::steady_clock::now();
        const auto start = previous;
        unsigned frames = 0;
        while (poll()) {
            const auto now = std::chrono::steady_clock::now();
            const double elapsed =
                capture.empty() ? std::chrono::duration<double>(now - previous).count() : 1. / 60;
            previous = now;
            const auto frame = clock_.advance(elapsed);
            for (unsigned i = 0; i < frame.steps; ++i)
                update((frame.first_tick + i + 1) * frame.step);
            const auto size = window_.metrics();
            const bool ready = !capture.empty() && frames >= 90;
            if (renderer_.frame(camera_, size.width, size.height, .1f, 80, nullptr,
                                ready ? capture : std::filesystem::path{})) {
                ++frames;
                if (ready)
                    return 0;
            }
            if (!capture.empty() && now - start > std::chrono::seconds(60))
                throw std::runtime_error("Capture timed out");
            sleep_for(1. / 120);
        }
        return 0;
    }

  private:
    material_id colored(material_id base, float4 color, float roughness, float metallic = 0) {
        const auto material = duplicate_material(scene_, base);
        set_color(scene_, material, "base_color", color);
        set_parameter(scene_, material, "roughness", roughness);
        set_parameter(scene_, material, "metallic", metallic);
        return material;
    }
    void add_shape(mesh_id mesh, material_id material, const mat4& transform) {
        const auto id = world_.create();
        world_.emplace<drawable>(id, create_mesh(scene_, mesh, material));
        visible(scene_, world_.get<drawable>(id)->node, true);
        world_.set_local_transform(id, transform);
    }
    void build_scene(const std::filesystem::path& material_path) {
        const auto base = load_material(scene_, material_path);
        const auto ivory = colored(base, {.82f, .81f, .75f, 1}, .8f);
        const auto graphite = colored(base, {.13f, .17f, .20f, 1}, .35f, .2f);
        const auto copper = colored(base, {.91f, .47f, .24f, 1}, .25f, .7f);
        const auto teal = colored(base, {.12f, .65f, .61f, 1}, .32f, .25f);
        const auto sphere = upload_mesh(scene_, sphere_mesh(.5f, 48, 32));
        const auto cube = upload_mesh(scene_, box_mesh());
        add_shape(upload_mesh(scene_, plane_mesh({200, 200})), ivory, {});
        add_shape(cube, graphite, translation({0, .15f, 0}) * scaling({6, .3f, 6}));
        add_shape(sphere, copper, translation({0, 1.65f, 0}) * scaling(float3{2.1f}));
        for (unsigned i = 0; i < 12; ++i) {
            const float phase = i * 2 * std::numbers::pi_v<float> / 12;
            const auto id = world_.create("satellite");
            world_.emplace<drawable>(id, create_mesh(scene_, i % 2 ? cube : sphere, i % 3 ? teal : copper));
            visible(scene_, world_.get<drawable>(id)->node, true);
            world_.emplace<spinning>(id, phase, .18f, 2.3f, .85f + float(i % 3) * .4f);
        }
        add_sun(scene_, {.color = {1, .93f, .82f}, .intensity = 16000, .direction = {-.5f, -1, -.6f}});
        add_light(scene_, {.kind = light_kind::point,
                           .color = {.2f, .6f, 1},
                           .position = {-3, 3, 2},
                           .intensity = 8000,
                           .radius = 12,
                           .shadows = false});
        add_light(scene_, {.kind = light_kind::spot,
                           .color = {1, .55f, .25f},
                           .position = {3, 5, -3},
                           .direction = {-3, -5, 3},
                           .intensity = 14000,
                           .radius = 15});
        set_environment(scene_, {.intensity = 14000});
        update(0);
    }
    bool poll() {
        input_.begin_frame();
        input_event event;
        while (window_.poll(event)) {
            if (event.type == event_type::quit || event.type == event_type::close)
                return false;
            input_.process(event);
        }
        if (input_.action("pause").pressed)
            clock_.pause(!clock_.paused());
        if (input_.action("projection").pressed)
            camera_.projection = camera_.projection == projection_kind::perspective
                                     ? projection_kind::orthographic
                                     : projection_kind::perspective;
        return !input_.action("exit").pressed;
    }
    void update_orbit(double seconds, entity id, const spinning& spin) {
        const float angle = spin.phase + float(seconds) * spin.rate;
        const float3 position{std::cos(angle) * spin.radius,
                              spin.height + .15f * std::sin(float(seconds) + spin.phase),
                              std::sin(angle) * spin.radius};
        world_.set_local_transform(id, translation(position) * rotation(angle, {0, 1, 0}) *
                                           scaling(float3{.65f}));
    }
    void update_drawable(entity id, const drawable& draw) {
        set_transform(scene_, draw.node, world_.world_transform(id));
    }
    void update(double seconds) {
        world_.each<spinning>(std::bind_front(&gallery::update_orbit, this, seconds));
        world_.each<drawable>(std::bind_front(&gallery::update_drawable, this));
    }

  private:
    window window_;
    renderer renderer_;
    scene scene_;
    world world_;
    input_map input_;
    fixed_clock clock_;
    camera_view camera_{.eye = {7, 6, 9}, .direction = {-7, -4.8f, -9}, .fov = 42, .vertical_size = 8};
};
}
int main(int argc, char** argv) {
    try {
        const auto material = std::filesystem::path(argv[0]).parent_path() / "materials/surface.filamat";
        return gallery(material).run(argc > 1 ? argv[1] : "",
                                     argc > 2 && std::string_view(argv[2]) == "ortho");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
