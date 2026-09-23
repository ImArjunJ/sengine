#include "sengine/animation.hpp"
namespace sengine {
playback::playback(double duration, playback_mode mode) : duration_(duration), mode_(mode) {
    if (!std::isfinite(duration) || duration <= 0 || unsigned(mode) > unsigned(playback_mode::ping_pong))
        throw std::invalid_argument("Invalid animation duration or playback mode");
}
void playback::speed(double rate) {
    if (!std::isfinite(rate))
        throw std::invalid_argument("Invalid animation speed");
    rate_ = rate;
}
void playback::seek(double seconds) {
    if (!std::isfinite(seconds))
        throw std::invalid_argument("Invalid animation position");
    if (mode_ == playback_mode::once) {
        position_ = std::clamp(seconds, 0., duration_);
        return;
    }
    const double period = mode_ == playback_mode::loop ? duration_ : duration_ * 2;
    if (!std::isfinite(period))
        throw std::overflow_error("Animation period overflow");
    position_ = std::fmod(seconds, period);
    if (position_ < 0)
        position_ += period;
}
void playback::advance(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::invalid_argument("Animation delta must be finite and nonnegative");
    if (!paused_)
        seek(position_ + seconds * rate_);
}
bool playback::finished() const noexcept {
    return mode_ == playback_mode::once && (rate_ < 0 ? position_ <= 0 : position_ >= duration_);
}
double playback::time() const noexcept {
    return mode_ == playback_mode::ping_pong && position_ > duration_ ? duration_ * 2 - position_ : position_;
}
transform_pose blend(const transform_pose& a, const transform_pose& b, float weight) {
    if (!std::isfinite(weight))
        throw std::invalid_argument("Invalid pose blend weight");
    weight = std::clamp(weight, 0.f, 1.f);
    return {detail::interpolate(a.position, b.position, weight), slerp(a.orientation, b.orientation, weight),
            detail::interpolate(a.scale, b.scale, weight)};
}
transform_pose transform_track::sample(double seconds, const transform_pose& rest) const {
    return {position.sample(seconds).value_or(rest.position),
            orientation.sample(seconds).value_or(rest.orientation),
            scale.sample(seconds).value_or(rest.scale)};
}
}
