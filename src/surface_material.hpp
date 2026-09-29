#pragma once
#include "sengine/scene.hpp"
#include <cstddef>
#include <span>

namespace sengine::detail {
std::span<const std::byte> surface_package();
material_id make_surface(scene&);
}
