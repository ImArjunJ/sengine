#pragma once
#include "sengine/scene.hpp"
namespace filament {
class Engine;
class Scene;
}
namespace sengine {
class filament_environment {
  public:
    filament_environment(filament::Engine&, filament::Scene&, const environment_options&);
    ~filament_environment();

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
