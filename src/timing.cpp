#include "sengine/timing.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sengine {
fixed_clock::fixed_clock(double step, unsigned max_steps) : step_(step), max_steps_(max_steps) {
    if (!std::isfinite(step) || step <= 0 || !max_steps || !std::isfinite(step * max_steps))
        throw std::invalid_argument("Invalid fixed clock limits");
}
void fixed_clock::speed(double value) {
    if (!std::isfinite(value) || value < 0)
        throw std::invalid_argument("Clock speed must be finite and nonnegative");
    speed_ = value;
}
void fixed_clock::reset() noexcept {
    tick_ = 0;
    remainder_ = 0;
}
fixed_frame fixed_clock::advance(double elapsed) {
    if (!std::isfinite(elapsed) || elapsed < 0)
        throw std::invalid_argument("Frame duration must be finite and nonnegative");
    const double total = remainder_ + (paused_ ? 0 : elapsed * speed_);
    if (!std::isfinite(total))
        throw std::overflow_error("Clock duration overflow");
    constexpr double tolerance = 8 * std::numeric_limits<double>::epsilon();
    const double available = std::floor(total / step_ + tolerance);
    const auto steps = static_cast<unsigned>(std::min(available, double(max_steps_)));
    if (tick_ > std::numeric_limits<std::uint64_t>::max() - steps)
        throw std::overflow_error("Clock tick overflow");
    double remainder =
        available <= max_steps_ ? std::max(0., total - steps * step_) : std::fmod(total, step_);
    if (step_ - remainder <= step_ * tolerance)
        remainder = 0;
    const double dropped = std::max(0., total - remainder - steps * step_);
    const fixed_frame frame{tick_, steps, step_, remainder / step_, dropped};
    tick_ += steps;
    remainder_ = remainder;
    return frame;
}
}
