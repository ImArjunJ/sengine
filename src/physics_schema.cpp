#include "physics_validation.hpp"
#include "sengine/scene_schema.hpp"
#include <numbers>

namespace sengine {
namespace physics_detail {
void validate(float3 value) {
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
        throw std::invalid_argument("Physics vectors must be finite");
}
void validate(const collider& value) {
    validate(value.half_extent);
    if (value.kind != collider_kind::box && value.kind != collider_kind::sphere &&
        value.kind != collider_kind::capsule)
        throw std::invalid_argument("Unknown collision shape");
    if (value.half_extent.x <= 0 || value.half_extent.y <= 0 || value.half_extent.z <= 0 ||
        !std::isfinite(value.radius) || value.radius <= 0 || !std::isfinite(value.half_height) ||
        value.half_height < 0)
        throw std::invalid_argument("Collision dimensions must be positive");
}
void validate(const rigid_body& value) {
    validate(value.geometry());
    validate(value.linear_velocity);
    validate(value.angular_velocity);
    if (value.motion != body_motion::fixed && value.motion != body_motion::dynamic &&
        value.motion != body_motion::kinematic)
        throw std::invalid_argument("Unknown body motion");
    if (!std::isfinite(value.mass) || value.mass <= 0 || !std::isfinite(value.friction) ||
        value.friction < 0 || !std::isfinite(value.restitution) || value.restitution < 0 ||
        value.restitution > 1 || !std::isfinite(value.gravity_scale) ||
        !std::isfinite(value.linear_damping) || value.linear_damping < 0 ||
        !std::isfinite(value.angular_damping) || value.angular_damping < 0 || value.layer >= 32)
        throw std::invalid_argument("Invalid body properties");
}
void validate(const character_body& value) {
    validate(value.linear_velocity);
    if (!std::isfinite(value.radius) || value.radius <= 0 || !std::isfinite(value.half_height) ||
        value.half_height < 0 || !std::isfinite(value.mass) || value.mass <= 0 ||
        !std::isfinite(value.max_slope) || value.max_slope <= 0 ||
        value.max_slope >= std::numbers::pi_v<float> / 2 || !std::isfinite(value.step_height) ||
        value.step_height < 0 || !std::isfinite(value.jump_speed) || value.jump_speed < 0 ||
        value.layer >= 32)
        throw std::invalid_argument("Invalid character properties");
}
}
collision_layers::collision_layers() {
    masks_.fill(0xffffffffu);
}
void collision_layers::enable(unsigned first, unsigned second, bool enabled) {
    if (first >= 32 || second >= 32)
        throw std::out_of_range("Collision layers range from 0 to 31");
    if (enabled) {
        masks_[first] |= 1u << second;
        masks_[second] |= 1u << first;
    } else {
        masks_[first] &= ~(1u << second);
        masks_[second] &= ~(1u << first);
    }
}
bool collision_layers::collides(unsigned first, unsigned second) const {
    if (first >= 32 || second >= 32)
        throw std::out_of_range("Collision layers range from 0 to 31");
    return (masks_[first] & (1u << second)) != 0;
}
void register_physics_components(scene_registry& registry) {
    component_schema<rigid_body> body;
    body.enumeration("motion", &rigid_body::motion,
                     {{"static", body_motion::fixed},
                      {"dynamic", body_motion::dynamic},
                      {"kinematic", body_motion::kinematic}},
                     false)
        .enumeration("shape", &rigid_body::shape,
                     {{"box", collider_kind::box},
                      {"sphere", collider_kind::sphere},
                      {"capsule", collider_kind::capsule}},
                     false)
        .field("half_extent", &rigid_body::half_extent, false)
        .field("radius", &rigid_body::radius, false)
        .field("half_height", &rigid_body::half_height, false)
        .field("mass", &rigid_body::mass, false)
        .field("friction", &rigid_body::friction, false)
        .field("restitution", &rigid_body::restitution, false)
        .field("gravity_scale", &rigid_body::gravity_scale, false)
        .field("linear_damping", &rigid_body::linear_damping, false)
        .field("angular_damping", &rigid_body::angular_damping, false)
        .field("linear_velocity", &rigid_body::linear_velocity, false)
        .field("angular_velocity", &rigid_body::angular_velocity, false)
        .field("layer", &rigid_body::layer, false)
        .field("sensor", &rigid_body::sensor, false)
        .field("continuous", &rigid_body::continuous, false)
        .validate(physics_detail::validate);
    registry.add("physics.body", std::move(body));
    component_schema<character_body> character;
    character.field("radius", &character_body::radius, false)
        .field("half_height", &character_body::half_height, false)
        .field("mass", &character_body::mass, false)
        .field("max_slope", &character_body::max_slope, false)
        .field("step_height", &character_body::step_height, false)
        .field("jump_speed", &character_body::jump_speed, false)
        .field("linear_velocity", &character_body::linear_velocity, false)
        .field("layer", &character_body::layer, false)
        .validate(physics_detail::validate);
    registry.add("physics.character", std::move(character));
}
}
