#include "sengine/math.hpp"
#include <algorithm>
#include <stdexcept>
namespace sengine {
float4 operator*(const mat4& m, float4 v) {
    return ((m[0] * v.x + m[1] * v.y) + m[2] * v.z) + m[3] * v.w;
}
mat4 operator*(const mat4& a, const mat4& b) {
    return {{a * b[0], a * b[1], a * b[2], a * b[3]}};
}
mat4 translation(float3 p) {
    mat4 m;
    m[3] = {p, 1};
    return m;
}
mat4 scaling(float3 s) {
    mat4 m;
    m[0].x = s.x;
    m[1].y = s.y;
    m[2].z = s.z;
    return m;
}
mat4 rotation(float angle, float3 axis) {
    const auto a = normalize(axis);
    const float s = std::sin(angle * .5f), c = std::cos(angle * .5f);
    return rotation({a.x * s, a.y * s, a.z * s, c});
}
mat4 rotation(quaternion q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    return {{float4{1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w), 0},
             float4{2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w), 0},
             float4{2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y), 0},
             float4{0, 0, 0, 1}}};
}
mat4 inverse(const mat4& m) {
    float values[4][8]{};
    for (unsigned c = 0; c < 4; ++c) {
        const auto v = m[c];
        const std::array col{v.x, v.y, v.z, v.w};
        for (unsigned r = 0; r < 4; ++r)
            values[r][c] = col[r];
        values[c][c + 4] = 1;
    }
    for (unsigned c = 0; c < 4; ++c) {
        unsigned pivot = c;
        for (unsigned r = c + 1; r < 4; ++r)
            if (std::abs(values[r][c]) > std::abs(values[pivot][c]))
                pivot = r;
        if (values[pivot][c] == 0)
            throw std::invalid_argument("Singular transform");
        for (unsigned j = 0; j < 8; ++j)
            std::swap(values[c][j], values[pivot][j]);
        const float scale = values[c][c];
        for (float& value : values[c])
            value /= scale;
        for (unsigned r = 0; r < 4; ++r)
            if (r != c) {
                const float factor = values[r][c];
                for (unsigned j = 0; j < 8; ++j)
                    values[r][j] -= factor * values[c][j];
            }
    }
    mat4 result;
    for (unsigned c = 0; c < 4; ++c)
        result[c] = {values[0][c + 4], values[1][c + 4], values[2][c + 4], values[3][c + 4]};
    return result;
}
quaternion rotation_of(const mat4& m) {
    const float trace = m[0].x + m[1].y + m[2].z;
    if (trace > 0) {
        const float s = std::sqrt(trace + 1) * 2;
        return {(m[1].z - m[2].y) / s, (m[2].x - m[0].z) / s, (m[0].y - m[1].x) / s, s * .25f};
    }
    if (m[0].x > m[1].y && m[0].x > m[2].z) {
        const float s = std::sqrt(1 + m[0].x - m[1].y - m[2].z) * 2;
        return {s * .25f, (m[1].x + m[0].y) / s, (m[2].x + m[0].z) / s, (m[1].z - m[2].y) / s};
    }
    if (m[1].y > m[2].z) {
        const float s = std::sqrt(1 + m[1].y - m[0].x - m[2].z) * 2;
        return {(m[1].x + m[0].y) / s, s * .25f, (m[2].y + m[1].z) / s, (m[2].x - m[0].z) / s};
    }
    const float s = std::sqrt(1 + m[2].z - m[0].x - m[1].y) * 2;
    return {(m[2].x + m[0].z) / s, (m[2].y + m[1].z) / s, s * .25f, (m[0].y - m[1].x) / s};
}
quaternion slerp(quaternion a, quaternion b, float t) {
    float cosine = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (cosine < 0) {
        b = {-b.x, -b.y, -b.z, -b.w};
        cosine = -cosine;
    }
    float left = 1 - t, right = t;
    if (cosine < .9995f) {
        const float angle = std::acos(std::clamp(cosine, -1.f, 1.f));
        const float sine = std::sin(angle);
        left = std::sin((1 - t) * angle) / sine;
        right = std::sin(t * angle) / sine;
    }
    quaternion q{a.x * left + b.x * right, a.y * left + b.y * right, a.z * left + b.z * right,
                 a.w * left + b.w * right};
    const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
}
}
