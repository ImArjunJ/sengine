#pragma once
#include "buffer_storage.hpp"
#include "sengine/scene.hpp"
#include <backend/BufferDescriptor.h>
#include <fstream>
#include <math/mat4.h>
#include <stdexcept>
#include <utils/Entity.h>
namespace sengine::filament_detail {
using namespace filament;
inline math::float3 native(float3 v) {
    return {v.x, v.y, v.z};
}
inline math::float4 native(float4 v) {
    return {v.x, v.y, v.z, v.w};
}
inline math::mat4f native(const mat4& m) {
    return {native(m[0]), native(m[1]), native(m[2]), native(m[3])};
}
inline float3 value(math::float3 v) {
    return {v.x, v.y, v.z};
}
inline mat4 value(const math::mat4f& m) {
    mat4 result;
    for (unsigned i = 0; i < 4; ++i)
        result[i] = {m[i].x, m[i].y, m[i].z, m[i].w};
    return result;
}
inline utils::Entity native(scene_node n) {
    return utils::Entity::import(n.value);
}
inline scene_node value(utils::Entity n) {
    return {n.getId()};
}
template <class element> backend::BufferDescriptor upload(std::vector<element> values) {
    auto* storage = new std::vector<element>(std::move(values));
    return {storage->data(), storage->size() * sizeof(element), filament_detail::release_vector<element>,
            storage};
}
inline std::vector<std::uint8_t> bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("Content unavailable: " + path.string());
    std::vector<std::uint8_t> result((std::istreambuf_iterator<char>(file)), {});
    if (result.empty())
        throw std::runtime_error("Content empty: " + path.string());
    return result;
}
}
