#include "backends/filament_scene_state.hpp"
#include "backends/filament_values.hpp"
#include <math/mat3.h>
#include <math/norm.h>
namespace sengine {
using namespace filament_detail;
namespace {
using namespace filament;
math::quatf tangent_frame(math::float3 normal, math::float3 tangent, bool reverse) {
    normal = normalize(normal);
    if (length(tangent) < .00001f) {
        const auto reference = std::abs(normal.y) < .98f ? math::float3{0, 1, 0} : math::float3{1, 0, 0};
        tangent = normalize(cross(reference, normal));
    }
    auto bitangent = reverse ? cross(tangent, normal) : cross(normal, tangent);
    return math::mat3f::packTangentFrame({tangent, bitangent, normal});
}
struct vertex {
    math::float3 position;
    math::quatf tangent;
    math::float2 uv;
    math::float4 color;
};
void validate_mesh(const mesh_data& source) {
    if (source.vertices.empty() || source.indices.empty())
        throw std::invalid_argument("Empty mesh");
    for (auto i : source.indices)
        if (i >= source.vertices.size())
            throw std::out_of_range("Mesh index outside vertices");
    for (const auto& target : source.morphs)
        if (target.positions.size() != source.vertices.size() ||
            target.normals.size() != source.vertices.size())
            throw std::invalid_argument("Morph vertex count mismatch");
}
std::vector<vertex> convert_vertices(const mesh_data& source) {
    std::vector<vertex> vertices;
    vertices.reserve(source.vertices.size());
    for (const auto& v : source.vertices)
        vertices.push_back({native(v.position),
                            tangent_frame(native(v.normal), native(v.tangent), source.reverse_bitangent),
                            {v.uv.x, v.uv.y},
                            native(v.color)});
    return vertices;
}
void upload_geometry(scene::impl& p, scene::impl::mesh_record& mesh, const mesh_data& source) {
    auto vertices = convert_vertices(source);
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
}
void upload_morphs(scene::impl& p, scene::impl::mesh_record& mesh, const mesh_data& source) {
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
}
}
mesh_id upload_mesh(scene& s, const mesh_data& source) {
    validate_mesh(source);
    auto& state = scene_data(s);
    auto& mesh = state.meshes.emplace_back();
    mesh.volume = source.volume;
    upload_geometry(state, mesh, source);
    upload_morphs(state, mesh, source);
    return {state.meshes.size() - 1};
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
    const auto instance = rm.getInstance(native(n));
    if (!instance || weights.size() > rm.getMorphTargetCount(instance))
        throw std::invalid_argument("Wrong number of mesh morph weights");
    for (float weight : weights)
        if (!std::isfinite(weight))
            throw std::invalid_argument("Mesh morph weights must be finite");
    if (!weights.empty())
        rm.setMorphWeights(instance, weights.data(), weights.size());
}
}
