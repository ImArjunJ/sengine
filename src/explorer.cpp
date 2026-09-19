#include "sengine/explorer.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <numbers>
#include <stdexcept>

namespace sengine {
inline constexpr double explorer_step = 1.0 / 120.0;
landscape load_landscape(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    auto read = [&](auto& value) { file.read(reinterpret_cast<char*>(&value), sizeof(value)); };
    char magic[8]{};
    file.read(magic, 8);
    if (std::memcmp(magic, "LWLAND1\0", 8))
        throw std::runtime_error("Invalid landscape header");
    landscape land;
    read(land.side);
    read(land.origin);
    read(land.spacing);
    if (!file || land.side < 2 || land.side > 2049 || !std::isfinite(land.origin) ||
        !std::isfinite(land.spacing) || land.spacing <= 0)
        throw std::runtime_error("Invalid landscape dimensions");
    land.heights.resize(size_t(land.side) * land.side);
    file.read(reinterpret_cast<char*>(land.heights.data()), land.heights.size() * sizeof(float));
    uint32_t count{};
    read(count);
    if (!file || count > 100000)
        throw std::runtime_error("Invalid landscape collision data");
    land.obstacles.resize(count);
    for (auto& box : land.obstacles) {
        read(box.low.x);
        read(box.low.y);
        read(box.low.z);
        read(box.high.x);
        read(box.high.y);
        read(box.high.z);
        if (!std::isfinite(box.low.x) || !std::isfinite(box.low.y) || !std::isfinite(box.low.z) ||
            !std::isfinite(box.high.x) || !std::isfinite(box.high.y) || !std::isfinite(box.high.z) ||
            box.low.x > box.high.x || box.low.y > box.high.y || box.low.z > box.high.z)
            throw std::runtime_error("Invalid landscape collider");
    }
    if (!file ||
        std::any_of(land.heights.begin(), land.heights.end(), [](float h) { return !std::isfinite(h); }))
        throw std::runtime_error("Truncated or invalid landscape");
    return land;
}
float terrain_height(const landscape& terrain, float x, float z) {
    float gx = std::clamp((x - terrain.origin) / terrain.spacing, 0.f, float(terrain.side - 1));
    float gz = std::clamp((z - terrain.origin) / terrain.spacing, 0.f, float(terrain.side - 1));
    unsigned ix = std::min(unsigned(gx), terrain.side - 2), iz = std::min(unsigned(gz), terrain.side - 2);
    float u = gx - ix, v = gz - iz;
    auto h = [&](unsigned dx, unsigned dz) { return terrain.heights[(iz + dz) * terrain.side + ix + dx]; };

    if (u + v <= 1)
        return h(0, 0) + (h(1, 0) - h(0, 0)) * u + (h(0, 1) - h(0, 0)) * v;
    return h(1, 1) + (h(0, 1) - h(1, 1)) * (1 - u) + (h(1, 0) - h(1, 1)) * (1 - v);
}
bool walkable(const landscape& terrain, point p, float radius, float body_height) {
    const float edge = terrain.origin + (terrain.side - 1) * terrain.spacing;
    if (!std::isfinite(p.x) || !std::isfinite(p.z) || p.x < terrain.origin + radius ||
        p.z < terrain.origin + radius || p.x > edge - radius || p.z > edge - radius)
        return false;
    const float ground = terrain_height(terrain, p.x, p.z);
    for (const auto& box : terrain.obstacles) {
        if (box.low.y >= p.y + body_height || box.high.y <= p.y + .025f)
            continue;
        float dx = p.x - std::clamp(p.x, box.low.x, box.high.x);
        float dz = p.z - std::clamp(p.z, box.low.z, box.high.z);
        if (dx * dx + dz * dz < radius * radius)
            return false;
    }
    const float slope_x =
        (terrain_height(terrain, p.x + .2f, p.z) - terrain_height(terrain, p.x - .2f, p.z)) / .4f;
    const float slope_z =
        (terrain_height(terrain, p.x, p.z + .2f) - terrain_height(terrain, p.x, p.z - .2f)) / .4f;
    return p.y > ground + .15f || std::hypot(slope_x, slope_z) < .9f;
}
namespace {
bool overlaps(point p, const box& box) {
    const float dx = p.x - std::clamp(p.x, box.low.x, box.high.x),
                dz = p.z - std::clamp(p.z, box.low.z, box.high.z);
    return dx * dx + dz * dz < .23f * .23f;
}
}
float support_height(const landscape& terrain, point p, float highest) {
    float result = terrain_height(terrain, p.x, p.z);
    for (const auto& box : terrain.obstacles)
        if (box.high.y <= highest && overlaps(p, box))
            result = std::max(result, box.high.y);
    return result;
}
float ceiling_height(const landscape& terrain, point p, float lowest) {
    float result = 10000;
    for (const auto& box : terrain.obstacles)
        if (box.low.y >= lowest && overlaps(p, box))
            result = std::min(result, box.low.y);
    return result;
}
explorer make_explorer(const landscape& terrain, point spawn, float yaw, float pitch) {
    if (terrain.side < 2 || terrain.heights.size() != std::size_t(terrain.side) * terrain.side)
        throw std::invalid_argument("Explorer needs a valid landscape");
    explorer player{.terrain = terrain};
    if (!relocate(player, spawn, yaw, pitch))
        throw std::runtime_error("Explorer spawn is obstructed");
    return player;
}
static void integrate(explorer& player, explorer_input input);
void look(explorer& player, float yaw, float pitch) {
    if (!std::isfinite(yaw) || !std::isfinite(pitch))
        return;
    player.yaw = std::remainder(player.yaw + yaw, 2 * std::numbers::pi_v<float>);
    player.pitch = std::clamp(player.pitch + pitch, -1.4f, 1.35f);
}
bool relocate(explorer& player, point feet, float yaw, float pitch) {
    if (!std::isfinite(feet.x) || !std::isfinite(feet.z))
        return false;
    feet.y = terrain_height(player.terrain, feet.x, feet.z);
    if (!walkable(player.terrain, feet) || !std::isfinite(yaw) || !std::isfinite(pitch))
        return false;
    player.position = {feet.x, terrain_height(player.terrain, feet.x, feet.z), feet.z};
    player.previous_position = player.position;
    player.yaw = yaw;
    player.pitch = std::clamp(pitch, -1.4f, 1.35f);
    player.velocity.y = 0;
    player.grounded = true;
    player.landing = player.landing_velocity = player.landing_impact = 0;
    stop(player);
    return true;
}
void stop(explorer& player) {
    player.velocity.x = player.velocity.z = 0;
    player.pending = 0;
    player.previous_position = player.position;
    player.bob = player.previous_bob = 0;
    player.footsteps = 0;
    player.jump_buffer = 0;
}
void advance(explorer& player, double seconds, explorer_input input) {
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(input.forward) ||
        !std::isfinite(input.right))
        return;
    if (input.jump_pressed)
        player.jump_buffer = .14f;

    player.pending += std::min(seconds, .1);
    while (player.pending + 1e-10 >= explorer_step) {
        integrate(player, input);
        player.pending -= explorer_step;
    }
    player.pending = std::max(0., player.pending);
}
static void integrate(explorer& player, explorer_input input) {
    player.previous_position = player.position;
    player.previous_eye = player.eye_height;
    player.previous_bob = player.bob;
    const float dt = float(explorer_step);
    player.coyote = player.grounded ? .10f : std::max(0.f, player.coyote - dt);
    player.jump_buffer = std::max(0.f, player.jump_buffer - dt);
    if (player.jump_buffer > 0 && player.coyote > 0 && !input.crouching) {
        player.velocity.y = 5.6f;
        player.grounded = false;
        player.coyote = player.jump_buffer = 0;
        player.stride_distance = 0;
    }
    float f = std::clamp(input.forward, -1.f, 1.f), r = std::clamp(input.right, -1.f, 1.f);
    const float length = std::max(1.f, std::hypot(f, r));
    const float speed = input.crouching ? 1.0f : input.running ? 4.2f : 2.1f;
    point wish{(std::sin(player.yaw) * f + std::cos(player.yaw) * r) * speed / length, 0,
               (-std::cos(player.yaw) * f + std::sin(player.yaw) * r) * speed / length};
    float dx = wish.x - player.velocity.x, dz = wish.z - player.velocity.z;
    const float delta = std::hypot(dx, dz), limit = dt * (player.grounded ? (f == 0 && r == 0 ? 30.f : 18.f)
                                                                          : (f == 0 && r == 0 ? 0.f : 4.5f));
    if (delta > 0) {
        player.velocity.x += dx * std::min(1.f, limit / delta);
        player.velocity.z += dz * std::min(1.f, limit / delta);
    }
    for (int axis = 0; axis < 2; ++axis) {
        auto p = player.position;
        if (axis == 0)
            p.x += player.velocity.x * explorer_step;
        else
            p.z += player.velocity.z * explorer_step;
        float support = support_height(player.terrain, p, player.position.y + .22f);
        if (player.grounded && std::abs(support - player.position.y) < .22f)
            p.y = support;
        if (walkable(player.terrain, p, .23f, player.eye_height + .17f)) {
            player.position = p;
        } else if (axis == 0)
            player.velocity.x = 0;
        else
            player.velocity.z = 0;
    }
    const float floor =
        support_height(player.terrain, player.position,
                       (player.grounded ? player.position.y : player.previous_position.y) + .025f);
    if (!player.grounded || player.position.y > floor + .025f) {
        player.grounded = false;
        player.velocity.y -= 16.f * dt;
        const float top =
            ceiling_height(player.terrain, player.position, player.position.y + player.eye_height + .16f);
        player.position.y += player.velocity.y * dt;
        if (player.position.y + player.eye_height + .17f > top && player.velocity.y > 0) {
            player.position.y = top - player.eye_height - .17f;
            player.velocity.y = 0;
        }
        if (player.position.y <= floor) {
            const float impact = std::max(0.f, -player.velocity.y);
            player.position.y = floor;
            player.velocity.y = 0;
            player.grounded = true;
            if (impact > 1.5f) {
                player.landing_velocity = -std::min(.8f, impact * .10f);
                player.landing_impact = impact;
            }
        }
    } else {
        player.position.y = floor;
        player.velocity.y = 0;
    }
    player.landing_velocity += (-150.f * player.landing - 22.f * player.landing_velocity) * dt;
    player.landing += player.landing_velocity * dt;
    player.run_blend += (((input.running || !player.grounded) && sengine::speed(player) > 2.5f ? 1.f : 0.f) -
                         player.run_blend) *
                        (1 - std::exp(-8 * dt));
    const float travelled = std::hypot(player.position.x - player.previous_position.x,
                                       player.position.z - player.previous_position.z);
    const float stride = input.running ? 1.12f : .76f;
    if (player.grounded) {
        player.distance += travelled / stride;
        player.stride_distance += travelled;
    }
    if (player.grounded && player.stride_distance >= stride) {
        player.stride_distance = std::fmod(player.stride_distance, stride);
        ++player.footsteps;
    }
    const float blend = 1 - std::exp(-18 * float(explorer_step));
    player.bob += ((player.grounded ? std::min(1.f, travelled / dt / 2.1f) : 0.f) - player.bob) * blend;
    float desired_eye = input.crouching ? 1.10f : 1.62f;
    if (desired_eye > player.eye_height && !walkable(player.terrain, player.position, .23f, 1.79f))
        desired_eye = 1.10f;
    player.eye_height += (desired_eye - player.eye_height) * (1 - std::exp(-12 * dt));
}
camera_pose camera(const explorer& player, bool motion) {
    const float t = float(player.pending / explorer_step);
    auto lerp = [&](float a, float b) { return a + (b - a) * t; };
    point eye{lerp(player.previous_position.x, player.position.x),
              lerp(player.previous_position.y, player.position.y) +
                  lerp(player.previous_eye, player.eye_height),
              lerp(player.previous_position.z, player.position.z)};

    eye.x += std::sin(player.yaw) * .20f;
    eye.z -= std::cos(player.yaw) * .20f;
    const float phase = player.distance * std::numbers::pi_v<float>;
    const float gait = lerp(player.previous_bob, player.bob);
    if (motion) {
        eye.y += (.018f + .009f * player.run_blend) * gait * std::cos(phase * 2) + player.landing;
        const float sway = .010f * gait * std::sin(phase);
        eye.x += std::cos(player.yaw) * sway;
        eye.z += std::sin(player.yaw) * sway;
    }
    point feet{lerp(player.previous_position.x, player.position.x),
               lerp(player.previous_position.y, player.position.y),
               lerp(player.previous_position.z, player.position.z)};
    return {eye,
            {std::sin(player.yaw) * std::cos(player.pitch), std::sin(player.pitch),
             -std::cos(player.yaw) * std::cos(player.pitch)},
            feet,
            player.yaw,
            phase,
            gait,
            player.landing,
            motion ? 65.f + 3.f * player.run_blend : 65.f,
            player.grounded,
            speed(player)};
}
unsigned take_footsteps(explorer& player) {
    auto result = player.footsteps;
    player.footsteps = 0;
    return result;
}
float take_landing(explorer& player) {
    auto result = player.landing_impact;
    player.landing_impact = 0;
    return result;
}
}
