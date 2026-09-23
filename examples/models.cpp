#include <iostream>
#include <sengine/application.hpp>
#include <sengine/native_hud.hpp>
#include <sengine/primitives.hpp>
#include <sengine/renderer.hpp>
#include <sengine/scene_loader.hpp>
#include <sengine/world_renderer.hpp>

namespace {
using namespace sengine;
scene_registry components() {
    scene_registry result;
    register_model_components(result);
    return result;
}
class exhibit {
  public:
    exhibit(std::unique_ptr<scene_world> content, renderer& graphics, const asset_store& assets)
        : content_(std::move(content)), resources_(graphics),
          models_(content_->entities(), resources_, assets) {
        const auto actor = content_->find("left/model");
        if (!content_->entities().get<model_animation>(actor))
            throw std::invalid_argument("The left model needs an animation");
        models_.model(actor).clip("Turn");
        models_.model(actor).clip("Sway");
    }
    void advance(double seconds) { models_.advance(seconds); }
    void toggle() {
        const auto actor = content_->find("left/model");
        const auto* animation = content_->entities().get<model_animation>(actor);
        models_.play(actor, animation->clip == "Turn" ? "Sway" : "Turn", .5);
    }

  private:
    std::unique_ptr<scene_world> content_;
    scene resources_;
    world_renderer models_;
};
class models final : public runtime_scene {
  public:
    models(application& host, std::filesystem::path capture)
        : host_(host), graphics_(host.display()), lighting_(graphics_),
          assets_(window::executable_directory()), loader_(registry_, assets_),
          hud_(graphics_, assets_.resolve("materials/hud.filamat"),
               assets_.resolve("data/fonts/DM-Sans.ttf")),
          capture_(std::move(capture)) {
        graphics_.configure({.aperture = 8, .shutter = 1.f / 90, .sensitivity = 100, .occlusion = true});
        add_sun(lighting_, {.color = {1, .94f, .85f}, .intensity = 18000, .direction = {-.4f, -1, -.6f}});
        set_environment(lighting_, {.intensity = 14000});
        const auto material = load_material(lighting_, assets_.resolve("materials/surface.filamat"));
        set_color(lighting_, material, "base_color", {.2f, .27f, .32f, 1});
        set_parameter(lighting_, material, "roughness", .8f);
        const auto floor = create_mesh(lighting_, upload_mesh(lighting_, box_mesh({4, .1f, 2.5f})), material);
        set_transform(lighting_, floor, translation({0, -.1f, 0}));
        visible(lighting_, floor, true);
        reload();
    }
    bool event(const input_event& event) override {
        if (event.type != event_type::key_down || event.key.repeat)
            return false;
        try {
            if (event.key.code == key_code::space)
                exhibit_->toggle();
            else if (event.key.code == key_code::r)
                reload();
            else if (event.key.code == key_code::escape)
                host_.scenes().quit();
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
        }
        return false;
    }
    void update(const runtime_frame& frame) override {
        elapsed_ += frame.seconds;
        if (!capture_.empty() && elapsed_ > 60)
            throw std::runtime_error("Capture timed out");
        exhibit_->advance(capture_.empty() ? frame.seconds : 1.0 / 60);
    }
    void render(const runtime_frame&) override {
        const auto& size = host_.viewport();
        hud_.begin(size.width, size.height, size.scale);
        hud_.text(28, 24, "Models", 28, {.08f, .12f, .16f});
        hud_.text(28, 64, "Space change animation   R reload   Esc quit", 15, {.18f, .25f, .30f});
        const bool capture = !capture_.empty() && frames_ >= 45;
        if (graphics_.frame(camera_, size.width, size.height, .1f, 80, &hud_,
                            capture ? capture_ : std::filesystem::path{})) {
            ++frames_;
            if (capture)
                host_.scenes().quit();
        }
    }

  private:
    void reload() {
        auto next = std::make_unique<exhibit>(loader_.load("data/models/room.json"), graphics_, assets_);
        exhibit_ = std::move(next);
    }

  private:
    application& host_;
    renderer graphics_;
    scene lighting_;
    asset_store assets_;
    scene_registry registry_{components()};
    scene_loader loader_;
    native_hud hud_;
    std::unique_ptr<exhibit> exhibit_;
    camera_view camera_{.eye = {5, 4.5f, 8}, .direction = {-5, -3.5f, -8}, .fov = 45};
    std::filesystem::path capture_;
    double elapsed_{};
    unsigned frames_{};
};
}
int main(int argc, char** argv) {
    try {
        application host({.title = "sengine / models", .width = 1200, .height = 800});
        host.scenes().emplace<models>(host, argc > 1 ? argv[1] : "");
        host.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
