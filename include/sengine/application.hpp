#pragma once
#include "runtime.hpp"
#include "window.hpp"
#include <chrono>

namespace sengine {
struct application_options {
    std::string title{"sengine"};
    int width{1280}, height{720};
    window_options window{};
    runtime_options runtime{};
    double minimum_refresh{30}, maximum_refresh{144}, idle_delay{.025};
    bool background_input{};
};
class application {
  public:
    explicit application(application_options = {});
    application(const application&) = delete;
    application& operator=(const application&) = delete;
    window& display() noexcept { return display_; }
    runtime& scenes() noexcept { return runtime_; }
    const window_metrics& viewport() const noexcept { return viewport_; }
    void run();

  private:
    void loop();
    void poll();
    void pace(std::chrono::steady_clock::time_point started);

  private:
    application_options options_;
    window display_;
    window_metrics viewport_;
    bool running_{};
    runtime runtime_;
};
}
