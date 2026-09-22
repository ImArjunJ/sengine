#pragma once
#include "explorer.hpp"
#include "math.hpp"
#include "window.hpp"
#include <filesystem>
#include <memory>
namespace sengine {
class native_hud;
struct fog_options {
    bool enabled{};
    float distance{}, density{}, cutoff{320}, falloff{};
    float3 color{};
};
struct render_options {
    float4 clear_color{.25f, .32f, .36f, 1};
    float aperture{8}, shutter{1.f / 90}, sensitivity{100};
    bool temporal_aa{true}, occlusion{true}, soft_shadows{true}, depth_of_field{};
    float occlusion_radius{.35f}, occlusion_power{1.2f}, occlusion_resolution{1};
    unsigned occlusion_quality{2};
    float focus_distance{1}, blur_scale{.3f};
    unsigned foreground_blur{3}, background_blur{12};
    fog_options fog;
};
struct backend_access;
class renderer {
  public:
    explicit renderer(window&);
    ~renderer();
    renderer(const renderer&) = delete;
    renderer& operator=(const renderer&) = delete;
    void configure(const render_options&);
    void visible_layers(std::uint8_t);
    void focus_distance(float);
    bool frame(const camera_pose&, unsigned width, unsigned height, float near_plane, float far_plane,
               native_hud* overlay = nullptr, const std::filesystem::path& capture = {},
               bool capture_overlay = true);

  private:
    friend struct backend_access;
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
