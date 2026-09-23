#pragma once
#include "scene.hpp"
#include "world.hpp"

namespace sengine {
class world_renderer {
  public:
    world_renderer(world&, scene&);
    ~world_renderer();
    world_renderer(const world_renderer&) = delete;
    world_renderer& operator=(const world_renderer&) = delete;
    void mesh(entity, mesh_id, material_id, mat4 offset = {});
    bool remove(entity);
    void show(entity, bool visible);
    void offset(entity, const mat4&);
    void synchronize();
    std::size_t size() const noexcept;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
