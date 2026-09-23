#pragma once
#include <array>
#include <cmath>
#include <cstddef>
namespace sengine {
struct float2 {
    float x{}, y{};

  public:
    bool operator==(const float2&) const = default;
};
struct float3 {
    float x{}, y{}, z{};

  public:
    constexpr float3() = default;
    constexpr explicit float3(float value) : x(value), y(value), z(value) {}
    constexpr float3(float x, float y, float z) : x(x), y(y), z(z) {}
    bool operator==(const float3&) const = default;
};
struct float4 {
    float x{}, y{}, z{}, w{};

  public:
    constexpr float4() = default;
    constexpr explicit float4(float value) : x(value), y(value), z(value), w(value) {}
    constexpr float4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
    constexpr float4(float3 value, float w) : x(value.x), y(value.y), z(value.z), w(w) {}
    constexpr float3 xyz() const { return {x, y, z}; }
    bool operator==(const float4&) const = default;
};
constexpr float3 operator+(float3 a, float3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
constexpr float3 operator-(float3 a, float3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
constexpr float3 operator-(float3 a) {
    return {-a.x, -a.y, -a.z};
}
constexpr float3 operator*(float3 a, float3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}
constexpr float3 operator*(float3 a, float b) {
    return {a.x * b, a.y * b, a.z * b};
}
constexpr float3 operator/(float3 a, float b) {
    return {a.x / b, a.y / b, a.z / b};
}
constexpr float4 operator+(float4 a, float4 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}
constexpr float4 operator*(float4 a, float b) {
    return {a.x * b, a.y * b, a.z * b, a.w * b};
}
constexpr float dot(float3 a, float3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
constexpr float3 cross(float3 a, float3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(float3 a) {
    return std::sqrt(dot(a, a));
}
inline float3 normalize(float3 a) {
    return a / length(a);
}
struct quaternion {
    float x{}, y{}, z{}, w{1};

  public:
    bool operator==(const quaternion&) const = default;
};
struct mat4 {
    std::array<float4, 4> columns{{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}}};

  public:
    float4& operator[](std::size_t i) { return columns[i]; }
    const float4& operator[](std::size_t i) const { return columns[i]; }
    bool operator==(const mat4&) const = default;
};
float4 operator*(const mat4&, float4);
mat4 operator*(const mat4&, const mat4&);
mat4 translation(float3);
mat4 scaling(float3);
mat4 rotation(float angle, float3 axis);
mat4 rotation(quaternion);
mat4 inverse(const mat4&);
quaternion rotation_of(const mat4&);
quaternion slerp(quaternion, quaternion, float);
struct bounds {
    float3 center, half_extent;
};
}
