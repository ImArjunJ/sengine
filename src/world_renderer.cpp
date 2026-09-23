#include "sengine/world_renderer.hpp"
#include <map>
#include <stdexcept>

namespace sengine {
namespace {
class mesh_binding {
  public:
    mesh_binding(scene& target, mesh_id mesh, material_id material, mat4 offset)
        : scene_(target), node_(create_mesh(target, mesh, material)), offset_(offset) {}
    ~mesh_binding() { destroy_node(scene_, node_); }
    mesh_binding(const mesh_binding&) = delete;
    mesh_binding& operator=(const mesh_binding&) = delete;
    void show(bool enabled) { shown_ = enabled; }
    void offset(const mat4& value) { offset_ = value; }
    void synchronize(const mat4& transform) {
        const auto next = transform * offset_;
        if (!initialized_ || next != transform_) {
            set_transform(scene_, node_, next);
            transform_ = next;
        }
        if (!initialized_ || visible_ != shown_) {
            visible(scene_, node_, shown_);
            visible_ = shown_;
        }
        initialized_ = true;
    }

  private:
    scene& scene_;
    scene_node node_;
    mat4 offset_, transform_;
    bool initialized_{}, visible_{}, shown_{true};
};
class transform_scope {
  public:
    explicit transform_scope(scene& target) : scene_(target) { begin_transforms(scene_); }
    ~transform_scope() { end_transforms(scene_); }
    transform_scope(const transform_scope&) = delete;
    transform_scope& operator=(const transform_scope&) = delete;

  private:
    scene& scene_;
};
}
struct world_renderer::impl {
    world& entities;
    scene& resources;
    std::map<entity, mesh_binding> bindings;

  public:
    impl(world& entities, scene& resources) : entities(entities), resources(resources) {}
    mesh_binding& require(entity id) {
        if (!entities.alive(id))
            throw std::invalid_argument("Cannot render a stale or foreign entity");
        const auto found = bindings.find(id);
        if (found == bindings.end())
            throw std::out_of_range("Entity has no mesh binding");
        return found->second;
    }
};
world_renderer::world_renderer(world& entities, scene& resources)
    : impl_(std::make_unique<impl>(entities, resources)) {}
world_renderer::~world_renderer() = default;
void world_renderer::mesh(entity id, mesh_id mesh, material_id material, mat4 offset) {
    if (!impl_->entities.alive(id))
        throw std::invalid_argument("Cannot render a stale or foreign entity");
    if (!impl_->bindings.try_emplace(id, impl_->resources, mesh, material, offset).second)
        throw std::invalid_argument("Entity already has a mesh binding");
}
bool world_renderer::remove(entity id) {
    return impl_->bindings.erase(id) != 0;
}
void world_renderer::show(entity id, bool visible) {
    impl_->require(id).show(visible);
}
void world_renderer::offset(entity id, const mat4& offset) {
    impl_->require(id).offset(offset);
}
void world_renderer::synchronize() {
    const transform_scope scope(impl_->resources);
    for (auto it = impl_->bindings.begin(); it != impl_->bindings.end();) {
        if (!impl_->entities.alive(it->first)) {
            it = impl_->bindings.erase(it);
        } else {
            it->second.synchronize(impl_->entities.world_transform(it->first));
            ++it;
        }
    }
}
std::size_t world_renderer::size() const noexcept {
    return impl_->bindings.size();
}
}
