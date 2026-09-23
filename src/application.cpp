#include "sengine/application.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sengine {
namespace {
application_options validate(application_options options) {
    if (options.width <= 0 || options.height <= 0 || !std::isfinite(options.minimum_refresh) ||
        !std::isfinite(options.maximum_refresh) || options.minimum_refresh <= 0 ||
        options.maximum_refresh < options.minimum_refresh || !std::isfinite(options.idle_delay) ||
        options.idle_delay < 0)
        throw std::invalid_argument("Invalid application dimensions or pacing limits");
    return options;
}
bool is_input(event_type type) {
    switch (type) {
    case event_type::key_down:
    case event_type::key_up:
    case event_type::text_input:
    case event_type::mouse_motion:
    case event_type::mouse_down:
    case event_type::mouse_up:
    case event_type::mouse_wheel:
        return true;
    default:
        return false;
    }
}
}
application::application(application_options options)
    : options_(validate(std::move(options))),
      display_(options_.title, options_.width, options_.height, options_.window),
      viewport_(display_.metrics()), runtime_(options_.runtime) {}
void application::poll() {
    input_event event;
    while (display_.poll(event)) {
        if (!options_.background_input && is_input(event.type) && !display_.metrics().focused)
            continue;
        runtime_.dispatch(event);
    }
}
void application::pace(std::chrono::steady_clock::time_point started) {
    const double budget =
        1.0 / std::clamp(viewport_.refresh_rate, options_.minimum_refresh, options_.maximum_refresh);
    const double spent = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    sleep_for(budget - spent);
}
void application::loop() {
    auto previous = std::chrono::steady_clock::now();
    while (!runtime_.finished()) {
        runtime_.begin_frame();
        if (runtime_.finished())
            break;
        poll();
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        viewport_ = display_.metrics();
        runtime_.advance(elapsed);
        viewport_ = display_.metrics();
        if (viewport_.minimized || !viewport_.width || !viewport_.height) {
            sleep_for(options_.idle_delay);
            continue;
        }
        runtime_.render();
        pace(now);
    }
}
void application::run() {
    if (running_)
        throw std::logic_error("Application is already running");
    running_ = true;
    try {
        loop();
    } catch (...) {
        running_ = false;
        throw;
    }
    running_ = false;
}
}
