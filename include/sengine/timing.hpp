#pragma once
#include <cstdint>

namespace sengine {
struct fixed_frame {
    std::uint64_t first_tick{};
    unsigned steps{};
    double step{}, alpha{}, dropped_time{};
};
class fixed_clock {
  public:
    explicit fixed_clock(double step = 1.0 / 60, unsigned max_steps = 8);
    fixed_frame advance(double elapsed);
    void reset() noexcept;
    void speed(double value);
    void pause(bool value) noexcept { paused_ = value; }
    bool paused() const noexcept { return paused_; }
    std::uint64_t tick() const noexcept { return tick_; }
    double step() const noexcept { return step_; }

  private:
    double step_, remainder_{}, speed_{1};
    std::uint64_t tick_{};
    unsigned max_steps_;
    bool paused_{};
};
}
