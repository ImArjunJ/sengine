#pragma once
#include "sengine/native_hud.hpp"
#include <algorithm>
#include <array>

namespace sengine {
inline hud_vertex solid_vertex(std::array<float, 2> position, ink color) {
    return {position[0], position[1], 0, 8.f / 1024, 8.f / 2048, color.r, color.g, color.b, color.a};
}
class hud_clipper {
  public:
    hud_clipper(std::vector<hud_vertex>& vertices, std::array<float, 4> clip, unsigned width, unsigned height,
                float scale)
        : vertices_(vertices), clip_(clip), width_(width), height_(height), scale_(scale) {}
    void emit(hud_vertex a, hud_vertex b, hud_vertex c) {
        if (inside(a) && inside(b) && inside(c)) {
            append(a);
            append(b);
            append(c);
            return;
        }
        if (std::max({a.x, b.x, c.x}) < clip_[0] || std::min({a.x, b.x, c.x}) > clip_[2] ||
            std::max({a.y, b.y, c.y}) < clip_[1] || std::min({a.y, b.y, c.y}) > clip_[3])
            return;
        std::array<hud_vertex, 12> polygon{};
        polygon[0] = a;
        polygon[1] = b;
        polygon[2] = c;
        std::size_t count = 3;
        for (unsigned edge = 0; edge < 4 && count; ++edge)
            count = clip_edge(polygon, count, edge);
        for (std::size_t i = 1; i + 1 < count; ++i) {
            append(polygon[0]);
            append(polygon[i]);
            append(polygon[i + 1]);
        }
    }

  private:
    void append(hud_vertex vertex) {
        vertex.x = 2 * vertex.x * scale_ / width_ - 1;
        vertex.y = 1 - 2 * vertex.y * scale_ / height_;
        vertices_.push_back(vertex);
    }
    bool inside(hud_vertex vertex) const {
        return vertex.x >= clip_[0] && vertex.y >= clip_[1] && vertex.x <= clip_[2] && vertex.y <= clip_[3];
    }
    static float coordinate(hud_vertex vertex, unsigned axis) { return axis == 0 ? vertex.x : vertex.y; }
    bool inside_edge(hud_vertex vertex, unsigned edge) const {
        return edge < 2 ? coordinate(vertex, edge % 2) >= clip_[edge]
                        : coordinate(vertex, edge % 2) <= clip_[edge];
    }
    static hud_vertex interpolate(hud_vertex a, hud_vertex b, float t) {
        return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, 0,
                a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t, a.r + (b.r - a.r) * t,
                a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    }
    std::size_t clip_edge(std::array<hud_vertex, 12>& polygon, std::size_t count, unsigned edge) const {
        std::array<hud_vertex, 12> next{};
        std::size_t next_count = 0;
        const auto axis = edge % 2;
        auto previous = polygon[count - 1];
        bool before = inside_edge(previous, edge);
        for (std::size_t i = 0; i < count; ++i) {
            const auto current = polygon[i];
            const bool after = inside_edge(current, edge);
            if (before != after)
                next[next_count++] =
                    interpolate(previous, current,
                                (clip_[edge] - coordinate(previous, axis)) /
                                    (coordinate(current, axis) - coordinate(previous, axis)));
            if (after)
                next[next_count++] = current;
            previous = current;
            before = after;
        }
        polygon = next;
        return next_count;
    }

  private:
    std::vector<hud_vertex>& vertices_;
    std::array<float, 4> clip_;
    unsigned width_, height_;
    float scale_;
};
}
