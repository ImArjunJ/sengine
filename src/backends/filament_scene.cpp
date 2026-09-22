#include "filament_access.hpp"
#include "filament_environment.hpp"
#include "sengine/scene.hpp"
#include <algorithm>
#include <backend/BufferDescriptor.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/MorphTargetBuffer.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <fstream>
#include <gltfio/Animator.h>
#include <gltfio/AssetLoader.h>
#include <gltfio/FilamentAsset.h>
#include <gltfio/FilamentInstance.h>
#include <gltfio/MaterialProvider.h>
#include <gltfio/ResourceLoader.h>
#include <gltfio/TextureProvider.h>
#include <gltfio/materials/uberarchive.h>
#include <math/mat3.h>
#include <math/norm.h>
#include <stdexcept>
#include <utils/EntityManager.h>
#include <utils/NameComponentManager.h>
namespace sengine {
namespace {
using namespace filament;
math::float3 native(float3 v) {
    return {v.x, v.y, v.z};
}
math::float4 native(float4 v) {
    return {v.x, v.y, v.z, v.w};
}
math::mat4f native(const mat4& m) {
    return {native(m[0]), native(m[1]), native(m[2]), native(m[3])};
}
float3 value(math::float3 v) {
    return {v.x, v.y, v.z};
}
mat4 value(const math::mat4f& m) {
    mat4 result;
    for (unsigned i = 0; i < 4; ++i)
        result[i] = {m[i].x, m[i].y, m[i].z, m[i].w};
    return result;
}
utils::Entity native(scene_node n) {
    return utils::Entity::import(n.value);
}
scene_node value(utils::Entity n) {
    return {n.getId()};
}
template <class element> backend::BufferDescriptor upload(std::vector<element> values) {
    auto* storage = new std::vector<element>(std::move(values));
    return {storage->data(), storage->size() * sizeof(element),
            [](void*, size_t, void* user) { delete static_cast<std::vector<element>*>(user); }, storage};
}
std::vector<std::uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Content unavailable: " + path.string());
    std::vector<std::uint8_t> result((std::istreambuf_iterator<char>(file)), {});
    if (result.empty())
        throw std::runtime_error("Content empty: " + path.string());
    return result;
}
math::quatf tangent_frame(math::float3 normal, math::float3 tangent, bool reverse) {
    normal = normalize(normal);
    if (length(tangent) < .00001f) {
        const auto reference = std::abs(normal.y) < .98f ? math::float3{0, 1, 0} : math::float3{1, 0, 0};
        tangent = normalize(cross(reference, normal));
    }
    auto bitangent = reverse ? cross(tangent, normal) : cross(normal, tangent);
    return math::mat3f::packTangentFrame({tangent, bitangent, normal});
}
}
struct scene::impl {
    filament::Engine& engine;
    filament::Scene& target;
    std::unique_ptr<utils::NameComponentManager> names;
    filament::gltfio::MaterialProvider* provider{};
    filament::gltfio::AssetLoader* loader{};
    std::unique_ptr<filament::gltfio::TextureProvider> textures;
    struct asset_record {
        filament::gltfio::FilamentAsset* asset{};
        std::vector<scene_node> nodes;
        bool instanced{};
    };
    struct mesh_record {
        filament::VertexBuffer* vertices{};
        filament::IndexBuffer* indices{};
        filament::MorphTargetBuffer* morphs{};
        bounds volume;
    };
    struct material_record {
        filament::MaterialInstance* material{};
        bool owned{};
    };
    std::vector<asset_record> assets;
    std::vector<filament::gltfio::FilamentInstance*> instances;
    std::vector<mesh_record> meshes;
    std::vector<filament::Material*> shaders;
    std::vector<material_record> materials;
    std::vector<utils::Entity> owned_nodes;
    std::unique_ptr<filament_environment> environment;
    impl(renderer& graphics)
        : engine(backend_access::engine(graphics)), target(backend_access::scene(graphics)),
          names(std::make_unique<utils::NameComponentManager>(utils::EntityManager::get())) {
        provider = filament::gltfio::createUbershaderProvider(&engine, UBERARCHIVE_DEFAULT_DATA,
                                                              UBERARCHIVE_DEFAULT_SIZE);
        loader = filament::gltfio::AssetLoader::create(
            {.engine = &engine, .materials = provider, .names = names.get()});
        textures.reset(filament::gltfio::createStbProvider(&engine));
        if (!provider || !loader || !textures)
            throw std::runtime_error("Cannot initialize scene loader");
    }
    ~impl() {
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
        filament::gltfio::AssetLoader::destroy(&loader);
        textures.reset();
        provider->destroyMaterials();
        delete provider;
    }
};
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
    auto collect = [&](auto&& self, utils::Entity node) -> void {
        if (renderable(s, value(node)))
            result.push_back(value(node));
        auto transform = tm.getInstance(node);
        if (transform)
            for (auto child : tm.getChildrenRange(transform))
                self(self, tm.getEntity(child));
    };
    if (n)
        collect(collect, native(n));
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
material_id material_at(scene& s, scene_node n, unsigned slot) {
    auto& p = scene_data(s);
    auto& rm = p.engine.getRenderableManager();
    auto* material =
        const_cast<filament::MaterialInstance*>(rm.getMaterialInstanceAt(rm.getInstance(native(n)), slot));
    p.materials.push_back({material, false});
    return {p.materials.size() - 1};
}
material_id load_material(scene& s, const std::filesystem::path& path) {
    auto& p = scene_data(s);
    auto data = bytes(path.string() + ".filamat");
    auto* shader = filament::Material::Builder().package(data.data(), data.size()).build(p.engine);
    if (!shader)
        throw std::runtime_error("Invalid material: " + path.string());
    p.shaders.push_back(shader);
    p.materials.push_back({shader->createInstance(), true});
    return {p.materials.size() - 1};
}
material_id duplicate_material(scene& s, material_id source, const std::string& name) {
    auto& p = scene_data(s);
    auto* material =
        filament::MaterialInstance::duplicate(p.materials.at(source.value).material, name.c_str());
    p.materials.push_back({material, true});
    return {p.materials.size() - 1};
}
void set_material(scene& s, scene_node n, material_id material, unsigned slot) {
    auto& p = scene_data(s);
    auto& rm = p.engine.getRenderableManager();
    rm.setMaterialInstanceAt(rm.getInstance(native(n)), slot, p.materials.at(material.value).material);
}
void set_parameter(scene& s, material_id m, const std::string& name, float v) {
    scene_data(s).materials.at(m.value).material->setParameter(name.c_str(), v);
}
void set_color(scene& s, material_id m, const std::string& name, float4 v, bool srgb) {
    scene_data(s).materials.at(m.value).material->setParameter(
        name.c_str(), srgb ? filament::RgbaType::sRGB : filament::RgbaType::LINEAR, native(v));
}
mesh_id upload_mesh(scene& s, const mesh_data& source) {
    using namespace filament;
    if (source.vertices.empty() || source.indices.empty())
        throw std::invalid_argument("Empty mesh");
    for (auto i : source.indices)
        if (i >= source.vertices.size())
            throw std::out_of_range("Mesh index outside vertices");
    for (const auto& target : source.morphs)
        if (target.positions.size() != source.vertices.size() ||
            target.normals.size() != source.vertices.size())
            throw std::invalid_argument("Morph vertex count mismatch");
    auto& p = scene_data(s);
    p.meshes.emplace_back();
    auto& mesh = p.meshes.back();
    mesh.volume = source.volume;
    struct vertex {
        math::float3 position;
        math::quatf tangent;
        math::float2 uv;
        math::float4 color;
    };
    std::vector<vertex> vertices;
    vertices.reserve(source.vertices.size());
    for (const auto& v : source.vertices)
        vertices.push_back({native(v.position),
                            tangent_frame(native(v.normal), native(v.tangent), source.reverse_bitangent),
                            {v.uv.x, v.uv.y},
                            native(v.color)});
    mesh.vertices = VertexBuffer::Builder()
                        .vertexCount(vertices.size())
                        .bufferCount(1)
                        .attribute(VertexAttribute::POSITION, 0, VertexBuffer::AttributeType::FLOAT3,
                                   offsetof(vertex, position), sizeof(vertex))
                        .attribute(VertexAttribute::TANGENTS, 0, VertexBuffer::AttributeType::FLOAT4,
                                   offsetof(vertex, tangent), sizeof(vertex))
                        .attribute(VertexAttribute::UV0, 0, VertexBuffer::AttributeType::FLOAT2,
                                   offsetof(vertex, uv), sizeof(vertex))
                        .attribute(VertexAttribute::UV1, 0, VertexBuffer::AttributeType::FLOAT2,
                                   offsetof(vertex, uv), sizeof(vertex))
                        .attribute(VertexAttribute::COLOR, 0, VertexBuffer::AttributeType::FLOAT4,
                                   offsetof(vertex, color), sizeof(vertex))
                        .build(p.engine);
    mesh.indices = IndexBuffer::Builder()
                       .indexCount(source.indices.size())
                       .bufferType(IndexBuffer::IndexType::UINT)
                       .build(p.engine);
    mesh.vertices->setBufferAt(p.engine, 0, upload(std::move(vertices)));
    mesh.indices->setBuffer(p.engine, upload(source.indices));
    if (!source.morphs.empty()) {
        mesh.morphs = MorphTargetBuffer::Builder()
                          .count(source.morphs.size())
                          .vertexCount(source.vertices.size())
                          .build(p.engine);
        for (unsigned target = 0; target < source.morphs.size(); ++target) {
            const auto& data = source.morphs[target];
            std::vector<math::float3> positions;
            std::vector<math::short4> tangents;
            for (std::size_t i = 0; i < data.positions.size(); ++i) {
                positions.push_back(native(data.positions[i]));
                auto q = tangent_frame(native(data.normals[i]), native(source.vertices[i].tangent),
                                       source.reverse_bitangent);
                tangents.push_back(math::packSnorm16(math::float4{q.x, q.y, q.z, q.w}));
            }
            mesh.morphs->setPositionsAt(p.engine, target, positions.data(), positions.size());
            mesh.morphs->setTangentsAt(p.engine, target, tangents.data(), tangents.size());
        }
    }
    return {p.meshes.size() - 1};
}
scene_node create_mesh(scene& s, mesh_id id, material_id material, bool shadows) {
    using namespace filament;
    auto& p = scene_data(s);
    auto& mesh = p.meshes.at(id.value);
    auto entity = utils::EntityManager::get().create();
    p.owned_nodes.push_back(entity);
    p.engine.getTransformManager().create(entity);
    RenderableManager::Builder builder(1);
    builder.boundingBox({native(mesh.volume.center), native(mesh.volume.half_extent)})
        .material(0, p.materials.at(material.value).material)
        .geometry(0, RenderableManager::PrimitiveType::TRIANGLES, mesh.vertices, mesh.indices)
        .castShadows(shadows)
        .receiveShadows(true);
    if (mesh.morphs)
        builder.morphing(mesh.morphs).morphing(0, 0, 0);
    builder.build(p.engine, entity);
    return value(entity);
}
void morph_weights(scene& s, scene_node n, std::span<const float> weights) {
    auto& rm = scene_data(s).engine.getRenderableManager();
    rm.setMorphWeights(rm.getInstance(native(n)), weights.data(), weights.size());
}
namespace {
filament::gltfio::Animator* animator(scene& s, scene_asset a) {
    return scene_data(s).assets.at(a.value).asset->getInstance()->getAnimator();
}
}
std::vector<std::string> animation_names(scene& s, scene_asset a) {
    auto* anim = animator(s, a);
    std::vector<std::string> result;
    for (std::size_t i = 0; i < anim->getAnimationCount(); ++i)
        result.emplace_back(anim->getAnimationName(i));
    return result;
}
float animation_duration(scene& s, scene_asset a, std::size_t i) {
    return animator(s, a)->getAnimationDuration(i);
}
void animate(scene& s, scene_asset a, std::size_t i, float time) {
    animator(s, a)->applyAnimation(i, time);
}
void blend_animation(scene& s, scene_asset a, std::size_t i, float time, float alpha) {
    animator(s, a)->applyCrossFade(i, time, alpha);
}
void update_bones(scene& s, scene_asset a) {
    animator(s, a)->updateBoneMatrices();
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
