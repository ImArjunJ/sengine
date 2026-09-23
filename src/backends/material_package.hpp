#pragma once
#include <backend/DriverEnums.h>
#include <cstddef>
#include <span>

namespace sengine::filament_detail {
void validate_material_package(std::span<const std::byte>, filament::backend::Backend);
}
