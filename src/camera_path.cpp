#include "sengine/camera_path.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace sengine {
namespace {
void append_corners(std::vector<point>& nodes, box box) {
    for (unsigned i = 0; i < 8; ++i)
        nodes.push_back(
            {i & 1 ? box.high.x : box.low.x, i & 2 ? box.high.y : box.low.y, i & 4 ? box.high.z : box.low.z});
}
float distance(point a, point b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}
point mix(point a, point b, float t) {
    return {std::lerp(a.x, b.x, t), std::lerp(a.y, b.y, t), std::lerp(a.z, b.z, t)};
}
box padded(box box, float amount) {
    return {{box.low.x - amount, box.low.y - amount, box.low.z - amount},
            {box.high.x + amount, box.high.y + amount, box.high.z + amount}};
}
bool clear(point from, point to, std::span<const box> obstacles) {
    float length = distance(from, to);
    if (length < .00001f)
        return true;
    point ray{(to.x - from.x) / length, (to.y - from.y) / length, (to.z - from.z) / length};
    return std::ranges::none_of(
        obstacles, [from, ray, length](box box) { return intersect(from, ray, box, length).has_value(); });
}
}
camera_path plan_path(point from, point to, std::span<const box> obstacles, float clearance) {
    camera_path path;
    path.points = {from};
    path.length = 0;
    std::vector<box> bounds;
    bounds.reserve(obstacles.size());
    for (auto box : obstacles)
        bounds.push_back(padded(box, clearance));
    if (clear(from, to, bounds)) {
        path.points.push_back(to);
        path.length = distance(from, to);
        return path;
    }
    std::vector<point> nodes{from, to};
    const box region = padded({{std::min(from.x, to.x), std::min(from.y, to.y), std::min(from.z, to.z)},
                               {std::max(from.x, to.x), std::max(from.y, to.y), std::max(from.z, to.z)}},
                              .5f);
    append_corners(nodes, {from, to});
    for (auto box : bounds)
        if (box.low.x <= region.high.x && box.high.x >= region.low.x && box.low.y <= region.high.y &&
            box.high.y >= region.low.y && box.low.z <= region.high.z && box.high.z >= region.low.z)
            append_corners(nodes, padded(box, .002f));
    std::vector<float> costs(nodes.size(), std::numeric_limits<float>::infinity());
    std::vector<size_t> previous(nodes.size(), nodes.size());
    std::vector<bool> visited(nodes.size());
    costs[0] = 0;
    for (size_t step = 0; step < nodes.size(); ++step) {
        size_t current = nodes.size();
        for (size_t i = 0; i < nodes.size(); ++i)
            if (!visited[i] && (current == nodes.size() || costs[i] < costs[current]))
                current = i;
        if (current == nodes.size() || !std::isfinite(costs[current]))
            return path;
        if (current == 1)
            break;
        visited[current] = true;
        for (size_t i = 0; i < nodes.size(); ++i) {
            float cost = costs[current] + distance(nodes[current], nodes[i]);
            if (!visited[i] && cost < costs[i] && clear(nodes[current], nodes[i], bounds)) {
                costs[i] = cost;
                previous[i] = current;
            }
        }
    }
    if (!std::isfinite(costs[1]))
        return path;
    std::vector<point> route;
    for (size_t i = 1; i != nodes.size(); i = previous[i])
        route.push_back(nodes[i]);
    std::ranges::reverse(route);
    path.points.clear();
    path.points.push_back(from);
    for (size_t i = 1; i + 1 < route.size(); ++i) {
        auto a = mix(route[i], route[i - 1], .2f), b = mix(route[i], route[i + 1], .2f);
        std::vector<point> bend{a};
        for (int sample = 1; sample <= 12; ++sample) {
            float t = sample / 12.f;
            bend.push_back(mix(mix(a, route[i], t), mix(route[i], b, t), t));
        }
        bool safe = true;
        for (size_t j = 1; j < bend.size(); ++j)
            safe &= clear(bend[j - 1], bend[j], bounds);
        if (safe)
            path.points.insert(path.points.end(), bend.begin(), bend.end());
        else
            path.points.push_back(route[i]);
    }
    path.points.push_back(to);
    for (size_t i = 1; i < path.points.size(); ++i)
        path.length += distance(path.points[i - 1], path.points[i]);
    return path;
}
point sample_path(const camera_path& path, float progress) {
    if (path.points.empty())
        return {};
    float remaining = std::clamp(progress, 0.f, 1.f) * path.length;
    for (size_t i = 1; i < path.points.size(); ++i) {
        float length = distance(path.points[i - 1], path.points[i]);
        if (remaining < length)
            return mix(path.points[i - 1], path.points[i], remaining / length);
        remaining -= length;
    }
    return path.points.back();
}
}
