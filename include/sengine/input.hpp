#pragma once
#include "events.hpp"
#include <map>
#include <span>
#include <string>
#include <vector>

namespace sengine {
enum class input_source { key, mouse_button, pointer_x, pointer_y, wheel_x, wheel_y };
struct input_binding {
    input_source source{input_source::key};
    key_code key{};
    mouse_button button{};
    float scale{1};
};
struct action_state {
    float value{};
    bool held{}, pressed{}, released{};
};
struct action_options {
    float dead_zone{}, press_threshold{.5f};
    bool clamp{true};
};
class input_map {
  public:
    void define(std::string action, std::vector<input_binding>, action_options = {});
    void erase(const std::string& action);
    void begin_frame();
    void process(const input_event&);
    void release_all();
    action_state action(const std::string&) const;
    std::array<float, 2> axis_pair(const std::string& horizontal, const std::string& vertical) const;

  private:
    struct mapping {
        std::vector<input_binding> bindings;
        action_options options;
        action_state state;
    };
    float read(const input_binding&) const;
    void update();

  private:
    std::map<std::string, mapping, std::less<>> mappings_;
    key_state keys_;
    std::array<bool, 6> buttons_{};
    float pointer_x_{}, pointer_y_{}, wheel_x_{}, wheel_y_{};
};

struct recorded_input {
    std::uint64_t tick{};
    input_event event;
};
class input_recording {
  public:
    void append(std::uint64_t tick, input_event);
    std::span<const recorded_input> events() const { return events_; }
    std::span<const recorded_input> at(std::uint64_t tick) const;
    void clear() { events_.clear(); }

  private:
    std::vector<recorded_input> events_;
};
}
