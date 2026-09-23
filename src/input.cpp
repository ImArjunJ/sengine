#include "sengine/input.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sengine {
void input_map::define(std::string action, std::vector<input_binding> bindings, action_options options) {
    if (action.empty() || !std::isfinite(options.dead_zone) || options.dead_zone < 0 ||
        !std::isfinite(options.press_threshold) || options.press_threshold < options.dead_zone)
        throw std::invalid_argument("Invalid input action");
    for (const auto& binding : bindings)
        if (!std::isfinite(binding.scale) || unsigned(binding.key) >= unsigned(key_code::count) ||
            unsigned(binding.button) >= buttons_.size() ||
            unsigned(binding.source) > unsigned(input_source::wheel_y))
            throw std::invalid_argument("Invalid input binding");
    mappings_.insert_or_assign(std::move(action), mapping{std::move(bindings), options, {}});
    update();
}
void input_map::erase(const std::string& action) {
    mappings_.erase(action);
}
float input_map::read(const input_binding& binding) const {
    switch (binding.source) {
    case input_source::key:
        return keys_[binding.key] ? binding.scale : 0;
    case input_source::mouse_button:
        return buttons_[unsigned(binding.button)] ? binding.scale : 0;
    case input_source::pointer_x:
        return pointer_x_ * binding.scale;
    case input_source::pointer_y:
        return pointer_y_ * binding.scale;
    case input_source::wheel_x:
        return wheel_x_ * binding.scale;
    case input_source::wheel_y:
        return wheel_y_ * binding.scale;
    }
    return 0;
}
void input_map::update() {
    for (auto& [name, mapping] : mappings_) {
        float value = 0;
        for (const auto& binding : mapping.bindings)
            value += read(binding);
        if (mapping.options.clamp)
            value = std::clamp(value, -1.f, 1.f);
        if (std::abs(value) <= mapping.options.dead_zone)
            value = 0;
        const bool held = std::abs(value) > mapping.options.press_threshold;
        mapping.state.pressed |= held && !mapping.state.held;
        mapping.state.released |= !held && mapping.state.held;
        mapping.state.held = held;
        mapping.state.value = value;
    }
}
void input_map::begin_frame() {
    pointer_x_ = pointer_y_ = wheel_x_ = wheel_y_ = 0;
    update();
    for (auto& [name, mapping] : mappings_) {
        mapping.state.pressed = false;
        mapping.state.released = false;
    }
}
void input_map::release_all() {
    keys_.fill(false);
    buttons_.fill(false);
    pointer_x_ = pointer_y_ = wheel_x_ = wheel_y_ = 0;
    update();
}
void input_map::process(const input_event& event) {
    switch (event.type) {
    case event_type::key_down:
    case event_type::key_up:
        keys_[event.key.code] = event.type == event_type::key_down;
        break;
    case event_type::mouse_down:
    case event_type::mouse_up:
        buttons_.at(unsigned(event.button.button)) = event.type == event_type::mouse_down;
        break;
    case event_type::mouse_motion:
        if (!std::isfinite(event.motion.dx) || !std::isfinite(event.motion.dy))
            throw std::invalid_argument("Non-finite pointer input");
        pointer_x_ += event.motion.dx;
        pointer_y_ += event.motion.dy;
        break;
    case event_type::mouse_wheel:
        if (!std::isfinite(event.wheel.x) || !std::isfinite(event.wheel.y))
            throw std::invalid_argument("Non-finite wheel input");
        wheel_x_ += event.wheel.x;
        wheel_y_ += event.wheel.y;
        break;
    case event_type::focus_lost:
        release_all();
        return;
    default:
        break;
    }
    update();
}
action_state input_map::action(const std::string& name) const {
    const auto found = mappings_.find(name);
    if (found == mappings_.end())
        throw std::out_of_range("Unknown input action: " + name);
    return found->second.state;
}
std::array<float, 2> input_map::axis_pair(const std::string& horizontal, const std::string& vertical) const {
    const float x = action(horizontal).value, y = action(vertical).value;
    const float scale = std::max(1.f, std::hypot(x, y));
    return {x / scale, y / scale};
}
void input_recording::append(std::uint64_t tick, input_event event) {
    if (!events_.empty() && tick < events_.back().tick)
        throw std::invalid_argument("Recorded input ticks must be ordered");
    events_.push_back({tick, std::move(event)});
}
std::span<const recorded_input> input_recording::at(std::uint64_t tick) const {
    const auto range = std::ranges::equal_range(events_, tick, {}, &recorded_input::tick);
    return {range.begin(), range.end()};
}
}
