#include "gltf_source.hpp"

#include "filament_scene_state.hpp"
#include "filament_values.hpp"
#include <atomic>
#include <gltfio/Animator.h>
#include <gltfio/ResourceLoader.h>
#include <map>
#include <thread>

namespace sengine {
using namespace filament_detail;
namespace {
std::atomic<std::uint64_t> next_model_instance{1};
struct render_state {
    std::vector<filament::MaterialInstance*> materials;
    std::uint8_t layers{};
    bool shadows{};
};
struct model_slot {
    filament::gltfio::FilamentInstance* native{};
    std::vector<utils::Entity> nodes;
    bool leased{};
};
}
struct model::impl {
  public:
    impl(scene& scene, const std::filesystem::path& path)
        : resources(scene_data(scene)), source(path), path(path) {
        for (const auto& node : source.nodes)
            separate_instances |= !node.pose.weights.empty();
        asset = load_asset();
        try {
            for (const auto& clip : source.clips)
                clips.push_back(clip.info);
            auto& slot = add_slot(asset, asset->getInstance());
            defaults.resize(slot.nodes.size());
            auto& renderables = resources.engine.getRenderableManager();
            for (std::size_t i = 0; i < slot.nodes.size(); ++i) {
                auto instance = renderables.getInstance(slot.nodes[i]);
                if (!instance)
                    continue;
                auto& state = defaults[i];
                state.layers = renderables.getLayerMask(instance);
                state.shadows = renderables.isShadowCaster(instance);
                for (std::size_t j = 0; j < renderables.getPrimitiveCount(instance); ++j)
                    state.materials.push_back(const_cast<filament::MaterialInstance*>(
                        renderables.getMaterialInstanceAt(instance, j)));
            }
            assets.push_back(asset);
        } catch (...) {
            resources.loader->destroyAsset(asset);
            throw;
        }
    }
    ~impl() {
        for (auto it = assets.rbegin(); it != assets.rend(); ++it) {
            resources.target.removeEntities((*it)->getEntities(), (*it)->getEntityCount());
            resources.loader->destroyAsset(*it);
        }
    }
    filament::gltfio::FilamentAsset* load_asset() {
        auto* result = resources.loader->createAsset(source.bytes.data(), std::uint32_t(source.bytes.size()));
        if (!result)
            throw std::runtime_error("Invalid model: " + path.string());
        try {
            const auto location = path.string();
            filament::gltfio::ResourceConfiguration options{};
            options.engine = &resources.engine;
            options.normalizeSkinningWeights = true;
            filament::gltfio::ResourceLoader loader(options);
            loader.addTextureProvider("image/png", resources.textures.get());
            loader.addTextureProvider("image/jpeg", resources.textures.get());
            for (std::size_t i = 0; i < result->getResourceUriCount(); ++i) {
                const auto* uri = result->getResourceUris()[i];
                if (std::string_view(uri).starts_with("data:"))
                    continue;
                if (separate_instances) {
                    auto found = resource_data.find(uri);
                    if (found == resource_data.end())
                        found = resource_data
                                    .emplace(uri, gltf_detail::read_file(
                                                      gltf_detail::resource_path(path.parent_path(), uri)))
                                    .first;
                    loader.addResourceData(uri, upload(found->second));
                } else
                    loader.addResourceData(uri, upload(gltf_detail::read_file(
                                                    gltf_detail::resource_path(path.parent_path(), uri))));
            }
            if (!loader.loadResources(result))
                throw std::runtime_error("Cannot upload model: " + location);
            return result;
        } catch (...) {
            resources.loader->destroyAsset(result);
            throw;
        }
    }
    void require_owner() const {
        if (owner != std::this_thread::get_id())
            throw std::logic_error("Models belong to their creating thread");
    }
    model_slot& acquire() {
        require_owner();
        for (auto& slot : slots)
            if (!slot->leased) {
                slot->leased = true;
                return *slot;
            }
        if (separate_instances) {
            auto* imported = load_asset();
            try {
                assets.reserve(assets.size() + 1);
                auto& slot = add_slot(imported, imported->getInstance());
                assets.push_back(imported);
                slot.leased = true;
                return slot;
            } catch (...) {
                resources.loader->destroyAsset(imported);
                throw;
            }
        }
        auto* instance = resources.loader->createInstance(asset);
        if (!instance)
            throw std::runtime_error("Cannot create model instance");
        auto& slot = add_slot(asset, instance);
        slot.leased = true;
        return slot;
    }
    model_slot& add_slot(filament::gltfio::FilamentAsset* owner,
                         filament::gltfio::FilamentInstance* instance) {
        auto slot = std::make_unique<model_slot>();
        slot->native = instance;
        slot->nodes.resize(source.nodes.size());
        for (std::size_t i = 0; i < instance->getEntityCount(); ++i) {
            const auto entity = instance->getEntities()[i];
            const auto* extras = owner->getExtras(entity);
            if (!extras)
                throw std::runtime_error("Imported node metadata is missing");
            const auto index = nlohmann::json::parse(extras).at("sengine_node").get<std::size_t>();
            if (index >= slot->nodes.size() || slot->nodes[index])
                throw std::runtime_error("Invalid imported node identity");
            slot->nodes[index] = entity;
        }
        slots.push_back(std::move(slot));
        return *slots.back();
    }

  public:
    scene::impl& resources;
    gltf_detail::gltf_source source;
    std::filesystem::path path;
    std::map<std::string, std::vector<std::uint8_t>, std::less<>> resource_data;
    std::vector<filament::gltfio::FilamentAsset*> assets;
    bool separate_instances{};
    filament::gltfio::FilamentAsset* asset{};
    std::vector<std::unique_ptr<model_slot>> slots;
    std::vector<model_clip> clips;
    std::vector<render_state> defaults;
    std::thread::id owner{std::this_thread::get_id()};
};
struct model_instance::impl {
  public:
    explicit impl(std::shared_ptr<model::impl> model)
        : resource(std::move(model)), slot(resource->acquire()) {
        try {
            const auto owner = next_model_instance.fetch_add(1, std::memory_order_relaxed);
            if (!owner)
                throw std::overflow_error("Model instance identity exhausted");
            identity = owner;
            shown.assign(slot.nodes.size(), true);
            children.resize(slot.nodes.size());
            auto& renderables = resource->resources.engine.getRenderableManager();
            for (std::size_t i = 0; i < slot.nodes.size(); ++i) {
                auto instance = renderables.getInstance(slot.nodes[i]);
                if (!instance)
                    continue;
                const auto& state = resource->defaults[i];
                renderables.setLayerMask(instance, 0xff, state.layers);
                renderables.setCastShadows(instance, state.shadows);
                for (std::size_t j = 0; j < state.materials.size(); ++j)
                    renderables.setMaterialInstanceAt(instance, j, state.materials[j]);
            }
            for (std::size_t i = 0; i < slot.nodes.size(); ++i) {
                if (!slot.nodes[i])
                    continue;
                const auto& data = resource->source.nodes[i];
                model_node_info info{{owner, std::uint32_t(i)}, data.name, {}, data.morphs};
                if (data.parent && slot.nodes[*data.parent]) {
                    info.parent = model_node{owner, std::uint32_t(*data.parent)};
                }
                for (auto child : data.children)
                    if (slot.nodes[child])
                        children[i].push_back({owner, std::uint32_t(child)});
                nodes.push_back(std::move(info));
            }
            pose = resource->source.rest();
            set_root({});
            apply_pose();
            apply_visibility();
            slot.native->getAnimator()->updateBoneMatrices();
        } catch (...) {
            release();
            throw;
        }
    }
    ~impl() { release(); }
    void release() noexcept {
        resource->resources.target.removeEntities(slot.native->getEntities(), slot.native->getEntityCount());
        slot.leased = false;
    }
    utils::Entity require(model_node node) const {
        resource->require_owner();
        if (node.owner != identity || node.index >= slot.nodes.size() || !slot.nodes[node.index])
            throw std::invalid_argument("Stale or foreign model node");
        return slot.nodes[node.index];
    }
    filament::RenderableManager::Instance require_renderable(model_node node) const {
        const auto entity = require(node);
        const auto instance = resource->resources.engine.getRenderableManager().getInstance(entity);
        if (!instance)
            throw std::invalid_argument("Model node has no renderable");
        return instance;
    }
    void set_root(const mat4& transform) {
        gltf_detail::validate_transform(transform);
        auto& manager = resource->resources.engine.getTransformManager();
        manager.setTransform(manager.getInstance(slot.native->getRoot()), native(transform));
    }
    void apply_visibility() {
        auto& target = resource->resources.target;
        for (std::size_t i = 0; i < slot.nodes.size(); ++i) {
            if (!slot.nodes[i])
                continue;
            bool enabled = visible && shown[i];
            for (auto parent = resource->source.nodes[i].parent; enabled && parent;
                 parent = resource->source.nodes[*parent].parent)
                enabled = shown[*parent];
            if (enabled)
                target.addEntity(slot.nodes[i]);
            else
                target.remove(slot.nodes[i]);
        }
    }
    void apply_pose() {
        auto& transforms = resource->resources.engine.getTransformManager();
        auto& renderables = resource->resources.engine.getRenderableManager();
        transforms.openLocalTransformTransaction();
        for (std::size_t i = 0; i < slot.nodes.size(); ++i) {
            if (!slot.nodes[i])
                continue;
            transforms.setTransform(transforms.getInstance(slot.nodes[i]), native(pose[i].transform));
            if (!pose[i].weights.empty()) {
                const auto instance = renderables.getInstance(slot.nodes[i]);
                renderables.setMorphWeights(instance, pose[i].weights.data(), pose[i].weights.size());
            }
        }
        transforms.commitLocalTransformTransaction();
    }

  public:
    std::shared_ptr<model::impl> resource;
    model_slot& slot;
    std::uint64_t identity{};
    std::vector<model_node_info> nodes;
    std::vector<std::vector<model_node>> children;
    std::vector<bool> shown;
    std::vector<gltf_detail::node_pose> pose, previous, scratch;
    double fade_duration{}, fade_elapsed{};
    bool visible{}, visibility_dirty{};
};
model::model(scene& resources, const std::filesystem::path& path)
    : impl_(std::make_shared<impl>(resources, path)) {}
std::span<const model_clip> model::clips() const {
    impl_->require_owner();
    return impl_->clips;
}
std::size_t model::instance_count() const {
    impl_->require_owner();
    std::size_t count{};
    for (const auto& slot : impl_->slots)
        count += slot->leased;
    return count;
}
std::size_t model::capacity() const {
    impl_->require_owner();
    return impl_->slots.size();
}
model_instance::model_instance(const model& source) : impl_(std::make_unique<impl>(source.impl_)) {}
model_instance::~model_instance() = default;
std::span<const model_node_info> model_instance::nodes() const {
    impl_->resource->require_owner();
    return impl_->nodes;
}
std::span<const model_clip> model_instance::clips() const {
    impl_->resource->require_owner();
    return impl_->resource->clips;
}
model_node model_instance::node(std::size_t index) const {
    if (index > UINT32_MAX)
        throw std::out_of_range("Model node index is too large");
    model_node result{impl_->identity, std::uint32_t(index)};
    impl_->require(result);
    return result;
}
model_node model_instance::find(std::string_view name) const {
    impl_->resource->require_owner();
    model_node result;
    for (const auto& node : impl_->nodes) {
        if (node.name != name)
            continue;
        if (result)
            throw std::invalid_argument("Ambiguous model node name: " + std::string(name));
        result = node.id;
    }
    if (!result)
        throw std::out_of_range("Unknown model node: " + std::string(name));
    return result;
}
std::optional<model_node> model_instance::parent(model_node node) const {
    impl_->require(node);
    const auto parent = impl_->resource->source.nodes[node.index].parent;
    if (parent && impl_->slot.nodes[*parent])
        return model_node{impl_->identity, std::uint32_t(*parent)};
    return {};
}
std::span<const model_node> model_instance::children(model_node node) const {
    impl_->require(node);
    return impl_->children[node.index];
}
bool model_instance::renderable(model_node node) const {
    return impl_->resource->resources.engine.getRenderableManager().hasComponent(impl_->require(node));
}
sengine::bounds model_instance::bounds(model_node node) const {
    const auto instance = impl_->require_renderable(node);
    const auto bounds =
        impl_->resource->resources.engine.getRenderableManager().getAxisAlignedBoundingBox(instance);
    return {value(bounds.center), value(bounds.halfExtent)};
}
material_id model_instance::copy_material(model_node node, unsigned slot) const {
    const auto instance = impl_->require_renderable(node);
    auto& resources = impl_->resource->resources;
    auto& renderables = resources.engine.getRenderableManager();
    if (slot >= renderables.getPrimitiveCount(instance))
        throw std::out_of_range("Invalid model material slot");
    resources.materials.reserve(resources.materials.size() + 1);
    auto* material = filament::MaterialInstance::duplicate(renderables.getMaterialInstanceAt(instance, slot));
    if (!material)
        throw std::runtime_error("Cannot copy model material");
    resources.materials.push_back({material, true, impl_->resource});
    return {resources.materials.size() - 1};
}
void model_instance::material(model_node node, material_id material, unsigned slot) {
    const auto instance = impl_->require_renderable(node);
    auto& resources = impl_->resource->resources;
    auto& renderables = resources.engine.getRenderableManager();
    if (slot >= renderables.getPrimitiveCount(instance))
        throw std::out_of_range("Invalid model material slot");
    renderables.setMaterialInstanceAt(instance, slot, resources.materials.at(material.value).material);
}
void model_instance::cast_shadows(model_node node, bool enabled) {
    impl_->resource->resources.engine.getRenderableManager().setCastShadows(impl_->require_renderable(node),
                                                                            enabled);
}
void model_instance::layers(model_node node, std::uint8_t mask) {
    impl_->resource->resources.engine.getRenderableManager().setLayerMask(impl_->require_renderable(node),
                                                                          0xff, mask);
}
std::size_t model_instance::clip(std::string_view name) const {
    std::optional<std::size_t> result;
    for (std::size_t i = 0; i < clips().size(); ++i) {
        if (clips()[i].name != name)
            continue;
        if (result)
            throw std::invalid_argument("Ambiguous model clip name: " + std::string(name));
        result = i;
    }
    if (!result)
        throw std::out_of_range("Unknown model clip: " + std::string(name));
    return *result;
}
void model_instance::transform(const mat4& transform) {
    impl_->resource->require_owner();
    impl_->set_root(transform);
}
void model_instance::visible(bool enabled) {
    impl_->resource->require_owner();
    if (impl_->visible == enabled)
        return;
    impl_->visible = enabled;
    impl_->visibility_dirty = true;
}
void model_instance::show(model_node node, bool enabled) {
    impl_->require(node);
    if (impl_->shown[node.index] == enabled)
        return;
    impl_->shown[node.index] = enabled;
    impl_->visibility_dirty = true;
}
mat4 model_instance::local_transform(model_node node) const {
    impl_->require(node);
    return impl_->pose[node.index].transform;
}
mat4 model_instance::world_transform(model_node node) const {
    const auto entity = impl_->require(node);
    auto& transforms = impl_->resource->resources.engine.getTransformManager();
    return value(transforms.getWorldTransform(transforms.getInstance(entity)));
}
void model_instance::local_transform(model_node node, const mat4& transform) {
    const auto entity = impl_->require(node);
    gltf_detail::validate_transform(transform);
    impl_->pose[node.index].transform = transform;
    impl_->pose[node.index].channels = gltf_detail::decompose(transform);
    auto& manager = impl_->resource->resources.engine.getTransformManager();
    manager.setTransform(manager.getInstance(entity), native(transform));
}
void model_instance::world_transform(model_node node, const mat4& transform) {
    const auto entity = impl_->require(node);
    auto& manager = impl_->resource->resources.engine.getTransformManager();
    const auto parent = manager.getInstance(manager.getParent(manager.getInstance(entity)));
    local_transform(node, inverse(value(manager.getWorldTransform(parent))) * transform);
}
std::span<const float> model_instance::morph_weights(model_node node) const {
    impl_->require(node);
    return impl_->pose[node.index].weights;
}
void model_instance::morph_weights(model_node node, std::span<const float> weights) {
    const auto entity = impl_->require(node);
    auto& target = impl_->pose[node.index].weights;
    if (weights.size() != target.size())
        throw std::invalid_argument("Wrong number of morph weights");
    for (float weight : weights)
        if (!std::isfinite(weight))
            throw std::invalid_argument("Morph weights must be finite");
    target.assign(weights.begin(), weights.end());
    if (!target.empty()) {
        auto& manager = impl_->resource->resources.engine.getRenderableManager();
        manager.setMorphWeights(manager.getInstance(entity), target.data(), target.size());
    }
}
void model_instance::reset_pose() {
    impl_->resource->require_owner();
    impl_->pose = impl_->resource->source.rest();
    impl_->previous.clear();
    impl_->fade_duration = impl_->fade_elapsed = 0;
    impl_->apply_pose();
}
void model_instance::transition(double seconds) {
    impl_->resource->require_owner();
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Invalid animation transition");
    impl_->previous = seconds > 0 ? impl_->pose : std::vector<gltf_detail::node_pose>{};
    impl_->fade_duration = seconds;
    impl_->fade_elapsed = 0;
}
void model_instance::animate(std::size_t clip, double time, double elapsed) {
    impl_->resource->require_owner();
    if (!std::isfinite(elapsed) || elapsed < 0)
        throw std::invalid_argument("Invalid animation delta");
    auto& next = impl_->scratch;
    impl_->resource->source.sample(clip, time, next);
    if (!impl_->previous.empty()) {
        const double progressed = std::min(impl_->fade_duration, impl_->fade_elapsed + elapsed);
        const float fraction = float(progressed / impl_->fade_duration);
        for (std::size_t i = 0; i < next.size(); ++i) {
            const auto& previous = impl_->previous[i];
            if (previous.transform != next[i].transform) {
                next[i].channels = sengine::blend(previous.channels, next[i].channels, fraction);
                next[i].transform = next[i].channels.matrix();
            }
            for (std::size_t j = 0; j < next[i].weights.size(); ++j)
                next[i].weights[j] = std::lerp(previous.weights[j], next[i].weights[j], fraction);
        }
        impl_->fade_elapsed = progressed;
        if (progressed == impl_->fade_duration)
            impl_->previous.clear();
    }
    impl_->pose.swap(next);
    impl_->apply_pose();
}
void model_instance::blend(std::size_t clip, double time, float weight) {
    impl_->resource->require_owner();
    if (!std::isfinite(weight) || weight < 0 || weight > 1)
        throw std::invalid_argument("Invalid animation blend weight");
    auto& next = impl_->scratch;
    impl_->resource->source.sample(clip, time, next);
    for (std::size_t i = 0; i < next.size(); ++i) {
        const auto& current = impl_->pose[i];
        if (current.transform != next[i].transform) {
            next[i].channels = sengine::blend(current.channels, next[i].channels, weight);
            next[i].transform = next[i].channels.matrix();
        }
        for (std::size_t j = 0; j < next[i].weights.size(); ++j)
            next[i].weights[j] = std::lerp(current.weights[j], next[i].weights[j], weight);
    }
    impl_->pose.swap(next);
    impl_->apply_pose();
}
void model_instance::synchronize() {
    impl_->resource->require_owner();
    if (impl_->visibility_dirty) {
        impl_->apply_visibility();
        impl_->visibility_dirty = false;
    }
    impl_->slot.native->getAnimator()->updateBoneMatrices();
}
}
