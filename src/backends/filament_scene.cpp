#include "backends/filament_scene_state.hpp"
#include "backends/filament_values.hpp"
#include <filament/LightManager.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/materials/uberarchive.h>
namespace sengine {
using namespace filament_detail;
scene::impl::impl(renderer& graphics)
    : engine(backend_access::engine(graphics)), target(backend_access::scene(graphics)),
      names(std::make_unique<utils::NameComponentManager>(utils::EntityManager::get())) {
    provider.reset(filament::gltfio::createUbershaderProvider(&engine, UBERARCHIVE_DEFAULT_DATA,
                                                              UBERARCHIVE_DEFAULT_SIZE));
    if (!provider)
        throw std::runtime_error("Cannot initialize scene loader");
    loader.reset(filament::gltfio::AssetLoader::create(
        {.engine = &engine, .materials = provider.get(), .names = names.get()}));
    textures.reset(filament::gltfio::createStbProvider(&engine));
    if (!provider || !loader || !textures)
        throw std::runtime_error("Cannot initialize scene loader");
}
scene::impl::~impl() {
    engine.flushAndWait();
    environment.reset();
    for (auto entity : owned_nodes) {
        target.remove(entity);
        engine.destroy(entity);
        utils::EntityManager::get().destroy(entity);
    }
    for (auto& record : assets) {
        target.removeEntities(record.asset->getEntities(), record.asset->getEntityCount());
        loader->destroyAsset(record.asset);
    }
    for (auto& mesh : meshes) {
        engine.destroy(mesh.vertices);
        engine.destroy(mesh.indices);
        if (mesh.morphs)
            engine.destroy(mesh.morphs);
    }
    for (auto& material : materials)
        if (material.owned)
            engine.destroy(material.material);
    for (auto* shader : shaders)
        engine.destroy(shader);
}
scene::impl& scene_data(scene& s) {
    return *s.impl_;
}
scene::scene(renderer& graphics) : impl_(std::make_unique<impl>(graphics)) {}
scene::~scene() = default;
scene_asset load_scene(scene& s, const std::filesystem::path& path, bool visible) {
    auto& p = scene_data(s);
    auto data = bytes(path);
    auto* asset = p.loader->createAsset(data.data(), std::uint32_t(data.size()));
    if (!asset)
        throw std::runtime_error("Invalid scene: " + path.string());
    try {
        const auto source = path.string();
        filament::gltfio::ResourceConfiguration options{};
        options.engine = &p.engine;
        options.gltfPath = source.c_str();
        options.normalizeSkinningWeights = true;
        filament::gltfio::ResourceLoader resources(options);
        resources.addTextureProvider("image/png", p.textures.get());
        resources.addTextureProvider("image/jpeg", p.textures.get());
        for (std::size_t i = 0; i < asset->getResourceUriCount(); ++i) {
            const auto* uri = asset->getResourceUris()[i];
            resources.addResourceData(uri, upload(bytes(path.parent_path() / uri)));
        }
        if (!resources.loadResources(asset))
            throw std::runtime_error("Scene upload failed: " + path.string());
        scene::impl::asset_record record{asset, {}};
        for (std::size_t i = 0; i < asset->getEntityCount(); ++i)
            record.nodes.push_back(value(asset->getEntities()[i]));
        p.assets.push_back(std::move(record));
    } catch (...) {
        p.loader->destroyAsset(asset);
        throw;
    }
    if (visible)
        p.target.addEntities(asset->getEntities(), asset->getEntityCount());
    return {p.assets.size() - 1};
}
std::span<const scene_node> nodes(scene& s, scene_asset a) {
    return scene_data(s).assets.at(a.value).nodes;
}
std::string node_name(scene& s, scene_asset a, scene_node n) {
    const auto* name = scene_data(s).assets.at(a.value).asset->getName(native(n));
    return name ? name : "";
}
scene_node root(scene& s, scene_asset a) {
    return value(scene_data(s).assets.at(a.value).asset->getRoot());
}
scene_instance instantiate(scene& s, scene_asset a) {
    auto& p = scene_data(s);
    auto& record = p.assets.at(a.value);
    auto* instance = record.instanced ? p.loader->createInstance(record.asset) : record.asset->getInstance();
    if (!instance)
        throw std::runtime_error("Cannot instance scene");
    p.instances.push_back(instance);
    record.instanced = true;
    return {p.instances.size() - 1};
}
scene_node root(scene& s, scene_instance i) {
    return value(scene_data(s).instances.at(i.value)->getRoot());
}
void visible(scene& s, scene_instance i, bool enabled) {
    auto& p = scene_data(s);
    auto* instance = p.instances.at(i.value);
    if (enabled)
        p.target.addEntities(instance->getEntities(), instance->getEntityCount());
    else
        p.target.removeEntities(instance->getEntities(), instance->getEntityCount());
}
void visible(scene& s, scene_node node, bool enabled) {
    auto& p = scene_data(s);
    if (enabled)
        p.target.addEntity(native(node));
    else
        p.target.remove(native(node));
}
void visible(scene& s, std::span<const scene_node> nodes, bool enabled) {
    for (auto n : nodes)
        visible(s, n, enabled);
}
bool renderable(scene& s, scene_node n) {
    return scene_data(s).engine.getRenderableManager().hasComponent(native(n));
}
std::vector<scene_node> renderable_descendants(scene& s, scene_node n) {
    auto& tm = scene_data(s).engine.getTransformManager();
    std::vector<scene_node> result;
    std::vector<utils::Entity> pending;
    if (n)
        pending.push_back(native(n));
    while (!pending.empty()) {
        const auto node = pending.back();
        pending.pop_back();
        if (renderable(s, value(node)))
            result.push_back(value(node));
        const auto transform = tm.getInstance(node);
        if (transform) {
            const auto first_child = pending.size();
            for (auto child : tm.getChildrenRange(transform))
                pending.push_back(tm.getEntity(child));
            std::reverse(pending.begin() + first_child, pending.end());
        }
    }
    return result;
}
mat4 local_transform(scene& s, scene_node n) {
    auto& tm = scene_data(s).engine.getTransformManager();
    return value(tm.getTransform(tm.getInstance(native(n))));
}
mat4 world_transform(scene& s, scene_node n) {
    auto& tm = scene_data(s).engine.getTransformManager();
    return value(tm.getWorldTransform(tm.getInstance(native(n))));
}
scene_node parent(scene& s, scene_node n) {
    auto& tm = scene_data(s).engine.getTransformManager();
    return value(tm.getParent(tm.getInstance(native(n))));
}
void set_transform(scene& s, scene_node n, const mat4& m) {
    auto& tm = scene_data(s).engine.getTransformManager();
    tm.setTransform(tm.getInstance(native(n)), native(m));
}
void begin_transforms(scene& s) {
    scene_data(s).engine.getTransformManager().openLocalTransformTransaction();
}
void end_transforms(scene& s) {
    scene_data(s).engine.getTransformManager().commitLocalTransformTransaction();
}
bounds node_bounds(scene& s, scene_node n) {
    auto& rm = scene_data(s).engine.getRenderableManager();
    const auto b = rm.getAxisAlignedBoundingBox(rm.getInstance(native(n)));
    return {value(b.center), value(b.halfExtent)};
}
void cast_shadows(scene& s, scene_node n, bool enabled) {
    auto& rm = scene_data(s).engine.getRenderableManager();
    auto i = rm.getInstance(native(n));
    if (i)
        rm.setCastShadows(i, enabled);
}
void layers(scene& s, scene_node n, std::uint8_t mask) {
    auto& rm = scene_data(s).engine.getRenderableManager();
    auto i = rm.getInstance(native(n));
    if (i)
        rm.setLayerMask(i, 0xff, mask);
}
scene_node add_sun(scene& s, const sun_options& options) {
    using namespace filament;
    auto& p = scene_data(s);
    auto entity = utils::EntityManager::get().create();
    p.owned_nodes.push_back(entity);
    LightManager::ShadowOptions shadows;
    shadows.mapSize = options.shadow_size;
    shadows.shadowCascades = options.cascades;
    std::copy(options.splits.begin(), options.splits.end(), shadows.cascadeSplitPositions);
    shadows.shadowFar = options.shadow_far;
    shadows.shadowFarHint = options.shadow_hint;
    shadows.stable = options.stable;
    shadows.lispsm = false;
    LightManager::Builder(LightManager::Type::SUN)
        .color(native(options.color))
        .intensity(options.intensity)
        .direction(native(options.direction))
        .sunAngularRadius(options.angular_radius)
        .castShadows(true)
        .shadowOptions(shadows)
        .build(p.engine, entity);
    p.target.addEntity(entity);
    return value(entity);
}
void shadow_resolution(scene& s, scene_node n, unsigned size) {
    auto& lights = scene_data(s).engine.getLightManager();
    auto i = lights.getInstance(native(n));
    auto options = lights.getShadowOptions(i);
    if (options.mapSize != size) {
        options.mapSize = size;
        lights.setShadowOptions(i, options);
    }
}
void set_environment(scene& s, const environment_options& options) {
    auto& p = scene_data(s);
    p.environment.reset();
    p.environment = std::make_unique<filament_environment>(p.engine, p.target, options);
}
}
