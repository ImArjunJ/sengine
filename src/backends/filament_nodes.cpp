#include "backends/filament_scene_state.hpp"
#include "backends/filament_values.hpp"
#include <algorithm>
namespace sengine {
using namespace filament_detail;
scene_node create_node(scene& scene, scene_node parent) {
    auto& state = scene_data(scene);
    auto& transforms = state.engine.getTransformManager();
    if (parent && !transforms.hasComponent(native(parent)))
        throw std::invalid_argument("Invalid transform parent");
    state.owned_nodes.reserve(state.owned_nodes.size() + 1);
    const auto node = utils::EntityManager::get().create();
    state.owned_nodes.push_back(node);
    transforms.create(node);
    if (parent)
        transforms.setParent(transforms.getInstance(node), transforms.getInstance(native(parent)));
    return value(node);
}
void set_parent(scene& scene, scene_node child, scene_node parent, bool preserve_world) {
    auto& transforms = scene_data(scene).engine.getTransformManager();
    const auto instance = transforms.getInstance(native(child));
    const auto parent_instance = transforms.getInstance(native(parent));
    if (!instance || (parent && !parent_instance))
        throw std::invalid_argument("Invalid transform hierarchy");
    for (auto ancestor = parent; ancestor;
         ancestor = value(transforms.getParent(transforms.getInstance(native(ancestor)))))
        if (ancestor.value == child.value)
            throw std::invalid_argument("Transform hierarchy cycle");
    const auto local = preserve_world ? inverse(parent ? world_transform(scene, parent) : mat4{}) *
                                            world_transform(scene, child)
                                      : local_transform(scene, child);
    transforms.setParent(instance, parent_instance);
    transforms.setTransform(instance, native(local));
}
void destroy_node(scene& scene, scene_node node) {
    auto& state = scene_data(scene);
    const auto native_node = native(node);
    const auto found = std::ranges::find(state.owned_nodes, native_node);
    if (found == state.owned_nodes.end())
        throw std::invalid_argument("Node is stale, foreign, or owned by an imported asset");
    auto& transforms = state.engine.getTransformManager();
    const auto transform = transforms.getInstance(native_node);
    if (transform && transforms.getChildCount(transform))
        throw std::logic_error("Reparent or destroy children before destroying their node");
    state.target.remove(native_node);
    state.engine.destroy(native_node);
    utils::EntityManager::get().destroy(native_node);
    state.owned_nodes.erase(found);
}
}
