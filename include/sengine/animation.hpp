#pragma once
#include "math.hpp"
#include <algorithm>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

namespace sengine {
enum class interpolation { step, linear, cubic };
enum class playback_mode { once, loop, ping_pong };
template <class value_type> struct keyframe {
    double time{};
    value_type value{}, incoming{}, outgoing{};
    interpolation mode{interpolation::linear};
};
namespace detail {
inline bool finite(float value) {
    return std::isfinite(value);
}
inline bool finite(float3 value) {
    return finite(value.x) && finite(value.y) && finite(value.z);
}
inline bool finite(float4 value) {
    return finite(value.xyz()) && finite(value.w);
}
inline bool finite(quaternion value) {
    return finite(float4{value.x, value.y, value.z, value.w});
}
template <class value> inline bool valid_key(value sample) {
    return finite(sample);
}
inline bool valid_key(quaternion sample) {
    const double norm = double(sample.x) * sample.x + double(sample.y) * sample.y +
                        double(sample.z) * sample.z + double(sample.w) * sample.w;
    return finite(sample) && std::abs(norm - 1) < .001;
}
inline float interpolate(float a, float b, float t) {
    return std::lerp(a, b, t);
}
inline float3 interpolate(float3 a, float3 b, float t) {
    return a * (1 - t) + b * t;
}
inline float4 interpolate(float4 a, float4 b, float t) {
    return a * (1 - t) + b * t;
}
inline quaternion interpolate(quaternion a, quaternion b, float t) {
    return slerp(a, b, t);
}
template <class value> value hermite(const keyframe<value>& a, const keyframe<value>& b, float t) {
    const float t2 = t * t, t3 = t2 * t, duration = float(b.time - a.time);
    return a.value * (2 * t3 - 3 * t2 + 1) + a.outgoing * ((t3 - 2 * t2 + t) * duration) +
           b.value * (-2 * t3 + 3 * t2) + b.incoming * ((t3 - t2) * duration);
}
inline float4 quaternion_vector(quaternion value) {
    return {value.x, value.y, value.z, value.w};
}
inline quaternion hermite(const keyframe<quaternion>& a, const keyframe<quaternion>& b, float t) {
    const keyframe<float4> first{a.time, quaternion_vector(a.value), quaternion_vector(a.incoming),
                                 quaternion_vector(a.outgoing)};
    const keyframe<float4> last{b.time, quaternion_vector(b.value), quaternion_vector(b.incoming),
                                quaternion_vector(b.outgoing)};
    const auto q = hermite(first, last, t);
    const float norm = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (norm <= 0 || !std::isfinite(norm))
        throw std::domain_error("Animation produced an invalid quaternion");
    return {q.x / norm, q.y / norm, q.z / norm, q.w / norm};
}
}
template <class value> class animation_curve {
  public:
    animation_curve() = default;
    explicit animation_curve(std::vector<keyframe<value>> keys) : keys_(std::move(keys)) {
        double previous = -1;
        for (const auto& key : keys_) {
            if (!std::isfinite(key.time) || key.time < 0 || key.time <= previous)
                throw std::invalid_argument("Animation keys must have strictly increasing finite times");
            if (!detail::valid_key(key.value) || !detail::finite(key.incoming) ||
                !detail::finite(key.outgoing) || unsigned(key.mode) > unsigned(interpolation::cubic))
                throw std::invalid_argument("Invalid animation key value or interpolation");
            if (previous >= 0 && key.time - previous > std::numeric_limits<float>::max())
                throw std::invalid_argument("Animation key interval exceeds interpolation range");
            previous = key.time;
        }
    }
    std::span<const keyframe<value>> keys() const { return keys_; }
    std::optional<value> sample(double seconds) const {
        if (!std::isfinite(seconds))
            throw std::invalid_argument("Invalid animation time");
        if (keys_.empty())
            return std::nullopt;
        const auto after = std::ranges::upper_bound(keys_, seconds, {}, &keyframe<value>::time);
        if (after == keys_.begin())
            return after->value;
        if (after == keys_.end())
            return keys_.back().value;
        const auto& before = *(after - 1);
        const float fraction = float((seconds - before.time) / (after->time - before.time));
        switch (before.mode) {
        case interpolation::step:
            return before.value;
        case interpolation::linear:
            return detail::interpolate(before.value, after->value, fraction);
        case interpolation::cubic:
            return detail::hermite(before, *after, fraction);
        }
        throw std::invalid_argument("Invalid animation interpolation");
    }

  private:
    std::vector<keyframe<value>> keys_;
};
class playback {
  public:
    explicit playback(double duration, playback_mode mode = playback_mode::loop);
    void advance(double seconds);
    void seek(double seconds);
    void speed(double rate);
    void pause(bool value) noexcept { paused_ = value; }
    bool paused() const noexcept { return paused_; }
    bool finished() const noexcept;
    double time() const noexcept;
    double duration() const noexcept { return duration_; }

  private:
    double duration_, position_{}, rate_{1};
    playback_mode mode_;
    bool paused_{};
};
struct transform_pose {
    float3 position{};
    quaternion orientation{};
    float3 scale{1};

  public:
    mat4 matrix() const { return translation(position) * rotation(orientation) * scaling(scale); }
};
transform_pose blend(const transform_pose&, const transform_pose&, float weight);
struct transform_track {
    animation_curve<float3> position;
    animation_curve<quaternion> orientation;
    animation_curve<float3> scale;

  public:
    transform_pose sample(double seconds, const transform_pose& rest = {}) const;
};
}
