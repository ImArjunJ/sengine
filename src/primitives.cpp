#include "sengine/primitives.hpp"
#include <numbers>
#include <stdexcept>
namespace sengine {
namespace {
void positive(float value) {
    if (!std::isfinite(value) || value <= 0)
        throw std::invalid_argument("Mesh dimensions must be positive and finite");
}
void append_face(mesh_data& mesh, float3 center, float3 horizontal, float3 vertical, float3 normal) {
    const auto first = std::uint32_t(mesh.vertices.size());
    const auto tangent = normalize(horizontal);
    mesh.vertices.push_back({center - horizontal - vertical, normal, tangent, {0, 0}});
    mesh.vertices.push_back({center + horizontal - vertical, normal, tangent, {1, 0}});
    mesh.vertices.push_back({center + horizontal + vertical, normal, tangent, {1, 1}});
    mesh.vertices.push_back({center - horizontal + vertical, normal, tangent, {0, 1}});
    for (auto index : {0u, 1u, 2u, 0u, 2u, 3u})
        mesh.indices.push_back(first + index);
}
}
mesh_data box_mesh(float3 half_extent) {
    positive(half_extent.x);
    positive(half_extent.y);
    positive(half_extent.z);
    mesh_data mesh;
    mesh.volume = {{}, half_extent};
    mesh.vertices.reserve(24);
    mesh.indices.reserve(36);
    for (auto normal : {float3{1, 0, 0}, float3{-1, 0, 0}, float3{0, 1, 0}, float3{0, -1, 0}, float3{0, 0, 1},
                        float3{0, 0, -1}}) {
        const auto reference = std::abs(normal.y) > .9f ? float3{0, 0, 1} : float3{0, 1, 0};
        const auto horizontal = cross(reference, normal);
        const auto vertical = cross(normal, horizontal);
        append_face(mesh, normal * half_extent, horizontal * half_extent, vertical * half_extent, normal);
    }
    return mesh;
}
mesh_data plane_mesh(float2 size, unsigned columns, unsigned rows) {
    positive(size.x);
    positive(size.y);
    if (!columns || !rows || columns > 2048 || rows > 2048)
        throw std::invalid_argument("Invalid plane subdivisions");
    mesh_data mesh;
    mesh.volume = {{}, {size.x * .5f, 0, size.y * .5f}};
    mesh.vertices.reserve(std::size_t(columns + 1) * (rows + 1));
    mesh.indices.reserve(std::size_t(columns) * rows * 6);
    for (unsigned row = 0; row <= rows; ++row)
        for (unsigned column = 0; column <= columns; ++column) {
            const float u = float(column) / columns, v = float(row) / rows;
            mesh.vertices.push_back(
                {{(u - .5f) * size.x, 0, (.5f - v) * size.y}, {0, 1, 0}, {1, 0, 0}, {u, v}});
        }
    for (unsigned row = 0; row < rows; ++row)
        for (unsigned column = 0; column < columns; ++column) {
            const auto a = row * (columns + 1) + column, b = a + columns + 1;
            for (auto index : {a, a + 1, b + 1, a, b + 1, b})
                mesh.indices.push_back(index);
        }
    return mesh;
}
mesh_data sphere_mesh(float radius, unsigned segments, unsigned rings) {
    positive(radius);
    if (segments < 3 || rings < 2 || segments > 2048 || rings > 2048)
        throw std::invalid_argument("Invalid sphere subdivisions");
    mesh_data mesh;
    mesh.volume = {{}, float3{radius}};
    mesh.vertices.reserve(std::size_t(segments + 1) * (rings + 1));
    mesh.indices.reserve(std::size_t(segments) * (rings - 1) * 6);
    for (unsigned ring = 0; ring <= rings; ++ring) {
        const float v = float(ring) / rings, latitude = v * std::numbers::pi_v<float>;
        for (unsigned segment = 0; segment <= segments; ++segment) {
            const float u = float(segment) / segments, longitude = u * 2 * std::numbers::pi_v<float>;
            const float3 normal{std::sin(latitude) * std::cos(longitude), std::cos(latitude),
                                std::sin(latitude) * std::sin(longitude)};
            mesh.vertices.push_back(
                {normal * radius, normal, {-std::sin(longitude), 0, std::cos(longitude)}, {u, v}});
        }
    }
    for (unsigned ring = 0; ring < rings; ++ring)
        for (unsigned segment = 0; segment < segments; ++segment) {
            const auto a = ring * (segments + 1) + segment, b = a + segments + 1;
            if (ring)
                for (auto index : {a, a + 1, b + 1})
                    mesh.indices.push_back(index);
            if (ring + 1 != rings)
                for (auto index : {a, b + 1, b})
                    mesh.indices.push_back(index);
        }
    return mesh;
}
}
