#include "gltf_source.hpp"

#include "filament_scene_state.hpp"
#include "filament_values.hpp"
#include <atomic>
#include <gltfio/Animator.h>
#include <gltfio/ResourceLoader.h>
#include <thread>

namespace sengine {
using namespace filament_detail;
namespace {
std::atomic<std::uint64_t> next_model_instance{1};
struct model_slot {
    filament::gltfio::FilamentInstance* native{};
    std::vector<utils::Entity> nodes;
    bool leased{};
};
}
struct model::impl {
  public:
    impl(scene& scene, const std::filesystem::path& path) : resources(scene_data(scene)), source(path) {
        asset = resources.loader->createAsset(source.bytes.data(), std::uint32_t(source.bytes.size()));
        if (!asset)
            throw std::runtime_error("Invalid model: " + path.string());
        try {
            const auto location = path.string();
            filament::gltfio::ResourceLoader loader({.engine = &resources.engine,
                                                     .gltfPath = location.c_str(),
                                                     .normalizeSkinningWeights = true});
            loader.addTextureProvider("image/png", resources.textures.get());
            loader.addTextureProvider("image/jpeg", resources.textures.get());
            for (std::size_t i = 0; i < asset->getResourceUriCount(); ++i) {
                const auto* uri = asset->getResourceUris()[i];
                if (!std::string_view(uri).starts_with("data:"))
                    loader.addResourceData(uri, upload(gltf_detail::read_file(
                                                    gltf_detail::resource_path(path.parent_path(), uri))));
            }
            if (!loader.loadResources(asset))
                throw std::runtime_error("Cannot upload model: " + location);
            for (const auto& clip : source.clips)
                clips.push_back(clip.info);
            add_slot(asset->getInstance());
        } catch (...) {
            resources.loader->destroyAsset(asset);
            throw;
        }
    }
    ~impl() {
        resources.target.removeEntities(asset->getEntities(), asset->getEntityCount());
        resources.loader->destroyAsset(asset);
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
        auto* instance = resources.loader->createInstance(asset);
        if (!instance)
            throw std::runtime_error("Cannot create model instance");
        auto& slot = add_slot(instance);
        slot.leased = true;
        return slot;
    }
    model_slot& add_slot(filament::gltfio::FilamentInstance* instance) {
        auto slot = std::make_unique<model_slot>();
        slot->native = instance;
        slot->nodes.resize(source.nodes.size());
        for (std::size_t i = 0; i < instance->getEntityCount(); ++i) {
            const auto entity = instance->getEntities()[i];
            const auto* extras = asset->getExtras(entity);
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
    filament::gltfio::FilamentAsset* asset{};
    std::vector<std::unique_ptr<model_slot>> slots;
    std::vector<model_clip> clips;
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
            for (std::size_t i = 0; i < slot.nodes.size(); ++i) {
                if (!slot.nodes[i])
                    continue;
                const auto& data = resource->source.nodes[i];
                model_node_info info{{owner, std::uint32_t(i)}, data.name, {}, data.morphs};
                if (data.parent && slot.nodes[*data.parent])
                    info.parent = model_node{owner, std::uint32_t(*data.parent)};
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
    impl_->visible = enabled;
    impl_->visibility_dirty = true;
}
void model_instance::show(model_node node, bool enabled) {
    impl_->require(node);
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
                next[i].channels = blend(previous.channels, next[i].channels, fraction);
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
void model_instance::synchronize() {
    impl_->resource->require_owner();
    if (impl_->visibility_dirty) {
        impl_->apply_visibility();
        impl_->visibility_dirty = false;
    }
    impl_->slot.native->getAnimator()->updateBoneMatrices();
}
}
