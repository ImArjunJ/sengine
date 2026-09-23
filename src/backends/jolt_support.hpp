#pragma once
#include <Jolt/Jolt.h>

#include "../physics_validation.hpp"
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/RegisterTypes.h>

namespace sengine::physics_detail {
inline JPH::Vec3 native(float3 value) {
    return {value.x, value.y, value.z};
}
inline float3 value(JPH::Vec3Arg vector) {
    return {vector.GetX(), vector.GetY(), vector.GetZ()};
}
inline quaternion value(JPH::QuatArg rotation) {
    return {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()};
}
inline JPH::Quat native(quaternion rotation) {
    const double magnitude = std::hypot(std::hypot(double(rotation.x), double(rotation.y)),
                                        std::hypot(double(rotation.z), double(rotation.w)));
    if (!std::isfinite(magnitude) || magnitude < 1e-12)
        throw std::invalid_argument("Physics rotations must be finite and nonzero");
    return {float(rotation.x / magnitude), float(rotation.y / magnitude), float(rotation.z / magnitude),
            float(rotation.w / magnitude)};
}
struct world_pose {
    physics_pose pose;
    float3 scale;
};
inline bool same_scale(float3 first, float3 second) {
    return length(first - second) <= 1e-5f * std::max(length(first), length(second));
}
inline world_pose decompose(const mat4& transform) {
    validate(transform[3].xyz());
    const float3 scale{length(transform[0].xyz()), length(transform[1].xyz()), length(transform[2].xyz())};
    validate(scale);
    if (std::min({scale.x, scale.y, scale.z}) < 1e-6f)
        throw std::invalid_argument("Physics transforms need nonzero scale");
    const auto x = transform[0].xyz() / scale.x, y = transform[1].xyz() / scale.y,
               z = transform[2].xyz() / scale.z;
    if (std::abs(dot(x, y)) > 1e-4f || std::abs(dot(y, z)) > 1e-4f || std::abs(dot(x, z)) > 1e-4f ||
        dot(cross(x, y), z) < .9999f)
        throw std::invalid_argument("Physics transforms cannot contain shear or reflection");
    const mat4 orientation{{float4{x, 0}, float4{y, 0}, float4{z, 0}, float4{0, 0, 0, 1}}};
    return {{transform[3].xyz(), rotation_of(orientation)}, scale};
}
inline JPH::RefConst<JPH::Shape> make_shape(collider geometry, float3 scale = float3{1}) {
    validate(geometry);
    validate(scale);
    JPH::ShapeSettings::ShapeResult result;
    if (geometry.kind == collider_kind::box) {
        const auto extent = geometry.half_extent * scale;
        validate(extent);
        result = JPH::BoxShapeSettings(native(extent),
                                       std::min({.05f, extent.x * .1f, extent.y * .1f, extent.z * .1f}))
                     .Create();
    } else {
        if (std::abs(scale.x - scale.y) > 1e-4f || std::abs(scale.x - scale.z) > 1e-4f)
            throw std::invalid_argument("Spheres and capsules require uniform world scale");
        const float radius = geometry.radius * scale.x, height = geometry.half_height * scale.x;
        if (!std::isfinite(radius) || !std::isfinite(height))
            throw std::invalid_argument("Collision dimensions are out of range");
        if (geometry.kind == collider_kind::sphere)
            result = JPH::SphereShapeSettings(radius).Create();
        else
            result = JPH::CapsuleShapeSettings(height, radius).Create();
    }
    if (result.HasError())
        throw std::invalid_argument(result.GetError().c_str());
    return result.Get();
}
class jolt_environment {
  public:
    jolt_environment() {
        if (JPH::Factory::sInstance)
            throw std::logic_error("Jolt is already initialized outside sengine");
        JPH::RegisterDefaultAllocator();
        factory_ = std::make_unique<JPH::Factory>();
        JPH::Factory::sInstance = factory_.get();
        JPH::RegisterTypes();
    }
    ~jolt_environment() {
        JPH::UnregisterTypes();
        JPH::Factory::sInstance = nullptr;
    }

  private:
    std::unique_ptr<JPH::Factory> factory_;
};
inline jolt_environment& environment() {
    static jolt_environment instance;
    return instance;
}
class broad_layers final : public JPH::BroadPhaseLayerInterface {
  public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(layer >= 32 ? 1 : 0);
    }
};
class broad_filter final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broad) const override {
        return layer >= 32 || broad.GetValue() == 1;
    }
};
class pair_filter final : public JPH::ObjectLayerPairFilter {
  public:
    explicit pair_filter(const collision_layers& layers) : layers_(layers) {}
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        return (first >= 32 || second >= 32) && layers_.collides(first % 32, second % 32);
    }

  private:
    const collision_layers& layers_;
};
}
