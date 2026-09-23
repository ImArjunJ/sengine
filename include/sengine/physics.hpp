#pragma once
#include "world.hpp"
#include <array>
#include <optional>
#include <span>

namespace sengine {
enum class body_motion { fixed, dynamic, kinematic };
enum class collider_kind { box, sphere, capsule };
struct collider {
    collider_kind kind{collider_kind::box};
    float3 half_extent{.5f};
    float radius{.5f}, half_height{.5f};

  public:
    bool operator==(const collider&) const = default;
};
struct rigid_body {
    body_motion motion{body_motion::fixed};
    collider_kind shape{collider_kind::box};
    float3 half_extent{.5f};
    float radius{.5f}, half_height{.5f};
    float mass{1}, friction{.5f}, restitution{}, gravity_scale{1};
    float linear_damping{.05f}, angular_damping{.05f};
    float3 linear_velocity{}, angular_velocity{};
    unsigned layer{};
    bool sensor{}, continuous{true};

  public:
    collider geometry() const { return {shape, half_extent, radius, half_height}; }
    bool operator==(const rigid_body&) const = default;
};
struct character_body {
    float radius{.3f}, half_height{.6f}, mass{80}, max_slope{.8f}, step_height{.3f}, jump_speed{5};
    float3 linear_velocity{};
    unsigned layer{};

  public:
    bool operator==(const character_body&) const = default;
};
class collision_layers {
  public:
    collision_layers();
    void enable(unsigned first, unsigned second, bool enabled = true);
    bool collides(unsigned first, unsigned second) const;

  private:
    std::array<std::uint32_t, 32> masks_;
};
struct physics_options {
    float3 gravity{0, -9.81f, 0};
    double fixed_step{1.0 / 60};
    unsigned max_bodies{16384}, worker_threads{};
    collision_layers layers;
};
struct physics_pose {
    float3 position{};
    quaternion orientation;
};
struct query_filter {
    std::uint32_t layers{0xffffffffu};
    entity ignore;
    bool sensors{true};
};
struct physics_hit {
    entity target;
    float fraction{};
    float3 point, normal;
};
enum class collision_phase { begin, persist, end };
struct collision_event {
    entity first, second;
    collision_phase phase{};
    float3 point, normal;
    bool sensor{};
};
struct character_state {
    bool grounded{};
    float3 velocity;
    entity ground;
};
class scene_registry;
void register_physics_components(scene_registry&);
class physics_world {
  public:
    explicit physics_world(world&, physics_options = {});
    ~physics_world();
    physics_world(const physics_world&) = delete;
    physics_world& operator=(const physics_world&) = delete;
    void synchronize();
    void step();
    double fixed_step() const noexcept;
    std::size_t size() const noexcept;
    void teleport(entity, physics_pose);
    void velocity(entity, float3 linear, float3 angular = {});
    float3 velocity(entity) const;
    void impulse(entity, float3);
    void force(entity, float3);
    void move_kinematic(entity, physics_pose);
    void walk(entity, float2 horizontal_velocity, bool jump = false);
    character_state character(entity) const;
    std::span<const collision_event> events() const noexcept;
    std::optional<physics_hit> raycast(float3 origin, float3 displacement, query_filter = {}) const;
    std::optional<physics_hit> sweep(const collider&, physics_pose, float3 displacement,
                                     query_filter = {}) const;
    std::vector<entity> overlap(const collider&, physics_pose, query_filter = {}) const;

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
