#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <sengine/application.hpp>
#include <sengine/native_hud.hpp>
#include <sengine/primitives.hpp>
#include <sengine/renderer.hpp>
#include <sengine/world_renderer.hpp>
#include <stdexcept>

namespace {
using namespace sengine;
class relay_resources {
  public:
    relay_resources(window& display, const std::filesystem::path& directory, std::filesystem::path capture)
        : graphics_(display), scene_(graphics_),
          hud_(graphics_, directory / "materials/hud.filamat", directory / "data/fonts/DM-Sans.ttf"),
          capture_(std::move(capture)) {
        graphics_.configure({.clear_color = {.035f, .055f, .08f, 1},
                             .aperture = 8,
                             .shutter = 1.f / 90,
                             .sensitivity = 100,
                             .occlusion = true});
        const auto base = load_material(scene_, directory / "materials/surface.filamat");
        floor_ = material(base, {.12f, .17f, .22f, 1}, .8f);
        player_ = material(base, {.25f, .80f, .88f, 1}, .24f);
        cell_ = material(base, {1.f, .64f, .23f, 1}, .3f);
        hazard_ = material(base, {.85f, .15f, .22f, 1}, .45f);
        gate_ = material(base, {.35f, .9f, .6f, 1}, .35f);
        cube_ = upload_mesh(scene_, box_mesh());
        sphere_ = upload_mesh(scene_, sphere_mesh(.5f, 32, 16));
        const auto ground = create_mesh(scene_, cube_, floor_);
        set_transform(scene_, ground, translation({0, -.25f, 0}) * scaling({10, .5f, 10}));
        visible(scene_, ground, true);
        add_sun(scene_, {.color = {1, .94f, .85f}, .intensity = 18000, .direction = {-.4f, -1, -.6f}});
        set_environment(scene_, {.intensity = 14000});
    }
    scene& resources() noexcept { return scene_; }
    native_hud& hud() noexcept { return hud_; }
    mesh_id cube() const noexcept { return cube_; }
    mesh_id sphere() const noexcept { return sphere_; }
    material_id player() const noexcept { return player_; }
    material_id cell() const noexcept { return cell_; }
    material_id hazard() const noexcept { return hazard_; }
    material_id gate() const noexcept { return gate_; }
    bool capturing() const noexcept { return !capture_.empty(); }
    void advance(double seconds) {
        if (capture_.empty())
            return;
        capture_time_ += seconds;
        if (capture_time_ > 60)
            throw std::runtime_error("Capture timed out");
    }
    void begin(const window_metrics& viewport) {
        hud_.begin(viewport.width, viewport.height, viewport.scale);
    }
    void present(application& host) {
        const auto& size = host.viewport();
        const bool capture = !capture_.empty() && frames_ >= 60;
        if (graphics_.frame(camera_, size.width, size.height, .1f, 80, &hud_,
                            capture ? capture_ : std::filesystem::path{})) {
            ++frames_;
            if (capture)
                host.scenes().quit();
        }
    }
    void heading(const std::string& title, const std::string& subtitle) {
        hud_.rectangle(0, 0, hud_.width(), 106, {.035f, .055f, .08f, .94f});
        hud_.text(32, 20, title, 34, {.92f, .96f, 1});
        hud_.text(34, 66, subtitle, 17, {.64f, .74f, .82f});
    }
    void footer(const std::string& text) {
        hud_.rectangle(0, hud_.height() - 54, hud_.width(), 54, {.035f, .055f, .08f, .94f});
        hud_.text(32, hud_.height() - 38, text, 17, {.76f, .84f, .89f});
    }
    void panel(const std::string& title, const std::string& detail) {
        const float x = (hud_.width() - 500) * .5f, y = (hud_.height() - 156) * .5f;
        hud_.rectangle(x, y, 500, 156, {.035f, .055f, .08f, .96f});
        hud_.text(x + 30, y + 28, title, 32, {.95f, .97f, 1});
        hud_.text(x + 30, y + 92, detail, 17, {.70f, .80f, .87f});
    }

  private:
    material_id material(material_id base, float4 color, float roughness) {
        const auto result = duplicate_material(scene_, base);
        set_color(scene_, result, "base_color", color);
        set_parameter(scene_, result, "roughness", roughness);
        set_parameter(scene_, result, "metallic", .12f);
        return result;
    }

  private:
    renderer graphics_;
    scene scene_;
    native_hud hud_;
    camera_view camera_{.eye = {0, 12, 11}, .direction = {0, -12, -11}, .fov = 48};
    material_id floor_, player_, cell_, hazard_, gate_;
    mesh_id cube_, sphere_;
    std::filesystem::path capture_;
    double capture_time_{};
    unsigned frames_{};
};
std::unique_ptr<runtime_scene> make_game(application&, std::shared_ptr<relay_resources>);
std::unique_ptr<runtime_scene> make_menu(application&, std::shared_ptr<relay_resources>);
std::unique_ptr<runtime_scene> make_result(application&, std::shared_ptr<relay_resources>, bool won);

class relay_screen : public runtime_scene {
  public:
    relay_screen(application& host, std::shared_ptr<relay_resources> resources)
        : host_(host), resources_(std::move(resources)) {}
    void update(const runtime_frame& frame) final { resources_->advance(frame.seconds); }
    void render(const runtime_frame&) final {
        synchronize();
        resources_->begin(host_.viewport());
        draw();
        resources_->present(host_);
    }

  protected:
    virtual void synchronize() {}
    virtual void draw() = 0;
    application& host() noexcept { return host_; }
    relay_resources& graphics() noexcept { return *resources_; }
    std::shared_ptr<relay_resources> resources() const { return resources_; }

  private:
    application& host_;
    std::shared_ptr<relay_resources> resources_;
};
class menu_screen final : public relay_screen {
  public:
    using relay_screen::relay_screen;
    bool event(const input_event& event) override {
        if (event.type != event_type::key_down || event.key.repeat)
            return false;
        if (event.key.code == key_code::enter)
            host().scenes().replace(make_game(host(), resources()));
        else if (event.key.code == key_code::escape)
            host().scenes().request_quit();
        return true;
    }

  private:
    void draw() override {
        graphics().heading("RELAY", "A small game built with sengine.");
        graphics().panel("Six cells. One exit.", "Collect the amber cells. Avoid the red sweepers.");
        graphics().footer("Enter to play  /  WASD or arrows to move  /  Esc to quit");
    }
};
class pause_screen final : public relay_screen {
  public:
    using relay_screen::relay_screen;
    bool event(const input_event& event) override {
        if (event.type != event_type::key_down || event.key.repeat)
            return false;
        if (event.key.code == key_code::p || event.key.code == key_code::escape)
            host().scenes().pop();
        return true;
    }

  private:
    void draw() override {
        graphics().heading("RELAY", "Paused");
        graphics().panel("Take a moment.", "P or Esc to continue.");
        graphics().footer("The board will be here when you return.");
    }
};
class result_screen final : public relay_screen {
  public:
    result_screen(application& host, std::shared_ptr<relay_resources> resources, bool won)
        : relay_screen(host, std::move(resources)), won_(won) {}
    bool event(const input_event& event) override {
        if (event.type != event_type::key_down || event.key.repeat)
            return false;
        if (event.key.code == key_code::enter)
            host().scenes().replace(make_game(host(), resources()));
        else if (event.key.code == key_code::escape)
            host().scenes().replace(make_menu(host(), resources()));
        return true;
    }

  private:
    void draw() override {
        graphics().heading("RELAY", won_ ? "Circuit complete" : "Signal lost");
        graphics().panel(won_ ? "All six delivered." : "Try another route.",
                         "Enter to play again. Esc for the menu.");
        graphics().footer("WASD or arrows to move  /  P to pause");
    }

  private:
    bool won_;
};
struct pickup {
    float3 position;
    float phase;
};
struct sweeper {
    float phase;
    bool horizontal;
};
class game_screen final : public relay_screen {
  public:
    game_screen(application& host, std::shared_ptr<relay_resources> resources)
        : relay_screen(host, resources), meshes_(world_, resources->resources()) {
        bind("horizontal", {{.key = key_code::a, .scale = -1},
                            {.key = key_code::left, .scale = -1},
                            {.key = key_code::d},
                            {.key = key_code::right}});
        bind("vertical", {{.key = key_code::w, .scale = -1},
                          {.key = key_code::up, .scale = -1},
                          {.key = key_code::s},
                          {.key = key_code::down}});
        build_board();
    }
    bool event(const input_event& event) override {
        if (event.type == event_type::key_down && !event.key.repeat) {
            if (event.key.code == key_code::p || event.key.code == key_code::escape)
                host().scenes().push(std::make_unique<pause_screen>(host(), resources()));
            else if (event.key.code == key_code::r)
                host().scenes().replace(make_game(host(), resources()));
        }
        return false;
    }
    void fixed_update(runtime_step step) override {
        if (ending_)
            return;
        elapsed_ += step.seconds;
        const auto axis = fixed_input().axis_pair("horizontal", "vertical");
        position_.x = std::clamp(position_.x + axis[0] * float(step.seconds) * 3.4f, -4.4f, 4.4f);
        position_.z = std::clamp(position_.z + axis[1] * float(step.seconds) * 3.4f, -4.4f, 4.4f);
        world_.each<pickup>(std::bind_front(&game_screen::update_pickup, this));
        world_.flush();
        world_.each<sweeper>(std::bind_front(&game_screen::update_sweeper, this));
        world_.set_local_transform(player_, translation(position_) * scaling(float3{.7f}));
        if (collected_ == 6 && std::hypot(position_.x, position_.z + 4.f) < .65f) {
            ending_ = true;
            host().scenes().replace(make_result(host(), resources(), true));
        }
    }

  private:
    entity shape(mesh_id mesh, material_id material, const mat4& transform) {
        const auto id = world_.create();
        world_.set_local_transform(id, transform);
        meshes_.mesh(id, mesh, material);
        return id;
    }
    void build_board() {
        player_ =
            shape(graphics().sphere(), graphics().player(), translation(position_) * scaling(float3{.7f}));
        shape(graphics().cube(), graphics().gate(), translation({0, .04f, -4}) * scaling({1.3f, .08f, 1.1f}));
        constexpr std::array<float3, 6> places{{{-3.7f, .5f, 3.3f},
                                                {3.7f, .5f, 3.3f},
                                                {-3.7f, .5f, 0},
                                                {3.7f, .5f, 0},
                                                {-3.7f, .5f, -3.3f},
                                                {3.7f, .5f, -3.3f}}};
        for (unsigned i = 0; i < places.size(); ++i) {
            const auto id =
                shape(graphics().cube(), graphics().cell(), translation(places[i]) * scaling(float3{.38f}));
            world_.emplace<pickup>(id, places[i], float(i));
        }
        for (unsigned i = 0; i < 2; ++i) {
            const auto id = shape(graphics().cube(), graphics().hazard(), scaling({1.1f, .35f, .6f}));
            world_.emplace<sweeper>(id, i * 2.f, i == 0);
        }
    }
    static void remove_pickup(entity id, world& world) { world.destroy(id); }
    void update_pickup(entity id, const pickup& cell) {
        world_.set_local_transform(id, translation(cell.position) *
                                           rotation(float(elapsed_) + cell.phase, {0, 1, 0}) *
                                           scaling(float3{.38f}));
        if (std::hypot(position_.x - cell.position.x, position_.z - cell.position.z) < .6f) {
            ++collected_;
            world_.defer(std::bind_front(remove_pickup, id));
        }
    }
    void update_sweeper(entity id, const sweeper& obstacle) {
        const float travel = 3.9f * std::sin(float(elapsed_) * .8f + obstacle.phase);
        const float3 position =
            obstacle.horizontal ? float3{travel, .22f, -1.6f} : float3{1.1f, .22f, travel};
        world_.set_local_transform(id, translation(position) * scaling({1.1f, .35f, .6f}));
        if (elapsed_ >= immune_until_ && std::abs(position_.x - position.x) < .82f &&
            std::abs(position_.z - position.z) < .58f) {
            --lives_;
            position_ = {0, .38f, 4};
            immune_until_ = elapsed_ + 1.5;
            if (!lives_) {
                ending_ = true;
                host().scenes().replace(make_result(host(), resources(), false));
            }
        }
    }
    void synchronize() override { meshes_.synchronize(); }
    void draw() override {
        graphics().heading("RELAY", "Collect six amber cells, then reach the green exit.");
        auto& hud = graphics().hud();
        hud.text(hud.width() - 250, 29,
                 std::to_string(collected_) + " / 6    " + std::to_string(lives_) + " lives", 22,
                 {.95f, .77f, .39f});
        graphics().footer("WASD / arrows  Move     P / Esc  Pause     R  Restart");
    }

  private:
    world world_;
    world_renderer meshes_;
    entity player_;
    float3 position_{0, .38f, 4};
    double elapsed_{}, immune_until_{};
    unsigned collected_{}, lives_{3};
    bool ending_{};
};
std::unique_ptr<runtime_scene> make_game(application& host, std::shared_ptr<relay_resources> resources) {
    return std::make_unique<game_screen>(host, std::move(resources));
}
std::unique_ptr<runtime_scene> make_menu(application& host, std::shared_ptr<relay_resources> resources) {
    return std::make_unique<menu_screen>(host, std::move(resources));
}
std::unique_ptr<runtime_scene> make_result(application& host, std::shared_ptr<relay_resources> resources,
                                           bool won) {
    return std::make_unique<result_screen>(host, std::move(resources), won);
}
}
int main(int argc, char** argv) {
    try {
        application host({.title = "sengine / relay", .width = 1200, .height = 800});
        auto resources = std::make_shared<relay_resources>(host.display(), window::executable_directory(),
                                                           argc > 1 ? argv[1] : "");
        host.scenes().push(resources->capturing() ? make_game(host, resources) : make_menu(host, resources));
        host.run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
