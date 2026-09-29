#pragma once
#include "application.hpp"
#include "game_object.hpp"
#include "physics.hpp"
#include "renderer.hpp"
#include "scene_loader.hpp"
#include "world_renderer.hpp"

namespace sengine {
struct game_scene_options {
    std::filesystem::path asset_root;
    render_options rendering;
    camera_view camera{.eye = {4, 3, 6}, .direction = {-4, -2, -6}};
    float near_plane{.1f}, far_plane{1000};
    std::uint8_t visible_layers{0xff};
    bool daylight{true};
};
class game_scene : public runtime_scene {
  public:
    explicit game_scene(application&, game_scene_options = {});
    ~game_scene() override;
    application& host() noexcept;
    renderer& graphics() noexcept;
    void configure(const render_options&);
    void visible_layers(std::uint8_t);
    asset_store& assets() noexcept;
    scene_registry& components() noexcept;
    world& entities() noexcept;
    scene& resources() noexcept;
    world_renderer& models() noexcept;
    const camera_view& camera() const noexcept;
    void camera(const camera_view&);
    void clip_planes(float near_plane, float far_plane);
    void load(const std::string& uri);
    void reload();
    game_object create(std::string name = {});
    game_object create_model(const std::string& source, std::string name = {});
    game_object create_mesh(const geometry&, const material&, std::string name = {});
    game_object create_mesh(const mesh_data&, const material&, std::string name = {});
    game_object create_light(const light_options&, std::string name = {});
    game_object find(const std::string& name) const;
    game_object object(entity) const;
    material surface(surface_options = {});
    geometry upload(const mesh_data&);
    void sun(const sun_options&);
    void environment(const environment_options&);
    physics_world& enable_physics(physics_options = {});
    physics_world& physics();
    void disable_physics() noexcept;
    void overlay(native_hud&) noexcept;
    void clear_overlay() noexcept;
    void capture(std::filesystem::path);
    bool capture_pending() const noexcept;
    void quit() noexcept;

  protected:
    virtual void presented(const runtime_frame&) {}

  private:
    void run_fixed_update(runtime_step) final;
    void run_update(const runtime_frame&) final;
    void run_render(const runtime_frame&) final;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
