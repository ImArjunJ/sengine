#pragma once
#include "sengine/physics.hpp"

namespace sengine::physics_detail {
void validate(float3);
void validate(const collider&);
void validate(const rigid_body&);
void validate(const character_body&);
}
