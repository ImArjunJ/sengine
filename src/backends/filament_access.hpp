#pragma once
#include "sengine/renderer.hpp"
namespace filament {
class Engine;
class Scene;
class View;
class Camera;
class Renderer;
}
namespace sengine {
struct backend_access {
    static filament::Engine& engine(renderer&);
    static filament::Scene& scene(renderer&);
    static filament::View& view(renderer&);
    static filament::Camera& camera(renderer&);
    static filament::Renderer& drawing(renderer&);
};
}
