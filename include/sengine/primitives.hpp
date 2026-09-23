#pragma once
#include "scene.hpp"
namespace sengine {
mesh_data box_mesh(float3 half_extent = float3{.5f});
mesh_data plane_mesh(float2 size = {1, 1}, unsigned columns = 1, unsigned rows = 1);
mesh_data sphere_mesh(float radius = .5f, unsigned segments = 32, unsigned rings = 16);
}
