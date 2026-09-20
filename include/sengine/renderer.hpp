#pragma once
#include "explorer.hpp"
#include <filesystem>
#include <memory>

namespace filament {
class Engine;
class Scene;
class View;
class Camera;
class Renderer;
}
namespace sengine {
class native_hud;
class renderer {
  public:
    renderer(void* window, void* shared_context = nullptr);
    ~renderer();
    renderer(const renderer&) = delete;
    renderer& operator=(const renderer&) = delete;
    filament::Engine& engine();
    filament::Scene& scene();
    filament::View& view();
    filament::Camera& camera();
    filament::Renderer& backend();
    bool frame(const camera_pose&, unsigned width, unsigned height, float near_plane, float far_plane,
               native_hud* overlay = nullptr, const std::filesystem::path& capture = {},
               bool capture_overlay = true);

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
