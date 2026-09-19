#pragma once
#include "geometry.hpp"
#include <cmath>
#include <filesystem>
#include <vector>

namespace sengine {
struct landscape {
    unsigned side{};
    float origin{}, spacing{1};
    std::vector<float> heights;
    std::vector<box> obstacles;
};
landscape load_landscape(const std::filesystem::path& path);
float terrain_height(const landscape&, float x, float z);
bool walkable(const landscape&, point feet, float radius = .23f, float body_height = 1.8f);
float support_height(const landscape&, point feet, float highest);
float ceiling_height(const landscape&, point feet, float lowest);

struct explorer_input {
    float forward{}, right{};
    bool running{}, crouching{}, jump_pressed{};
};
struct camera_pose {
    point eye{}, direction{0, 0, -1}, feet{};
    float yaw{}, stride{}, gait{}, landing{}, fov{65};
    bool grounded{true};
    float speed{};
};
struct explorer {
    const landscape& terrain;
    point position{}, previous_position{}, velocity{};
    float yaw{}, pitch{}, eye_height{1.62f}, previous_eye{1.62f};
    float distance{}, bob{}, previous_bob{}, stride_distance{};
    double pending{};
    unsigned footsteps{};
    bool grounded{true};
    float jump_buffer{}, coyote{}, landing{}, landing_velocity{}, landing_impact{}, run_blend{};
};
explorer make_explorer(const landscape&, point spawn = {}, float yaw = 0, float pitch = 0);
void look(explorer&, float yaw, float pitch);
void advance(explorer&, double seconds, explorer_input input);
void stop(explorer&);
bool relocate(explorer&, point feet, float yaw, float pitch);
camera_pose camera(const explorer&, bool motion = true);
unsigned take_footsteps(explorer&);
float take_landing(explorer&);
inline float speed(const explorer& player) {
    return std::hypot(player.velocity.x, player.velocity.z);
}
}
