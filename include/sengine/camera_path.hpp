#pragma once
#include "geometry.hpp"
#include <span>
#include <vector>

namespace sengine {
struct camera_path {
    std::vector<point> points;
    float length{};
};
camera_path plan_path(point from, point to, std::span<const box> obstacles, float clearance);
point sample_path(const camera_path& path, float progress);
}
