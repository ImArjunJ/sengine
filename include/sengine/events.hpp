#pragma once
#include <array>
#include <cstdint>
#include <string>

namespace sengine {
enum class key_code : std::uint16_t {
    unknown,
    a,
    b,
    c,
    d,
    e,
    f,
    g,
    h,
    i,
    j,
    k,
    l,
    m,
    n,
    o,
    p,
    q,
    r,
    s,
    t,
    u,
    v,
    w,
    x,
    y,
    z,
    digit_1,
    digit_2,
    digit_3,
    digit_4,
    digit_5,
    digit_6,
    digit_7,
    digit_8,
    digit_9,
    digit_0,
    enter,
    escape,
    backspace,
    tab,
    space,
    home,
    end,
    left,
    right,
    up,
    down,
    delete_key,
    page_up,
    page_down,
    left_shift,
    right_shift,
    left_control,
    right_control,
    left_alt,
    right_alt,
    left_super,
    right_super,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
    count
};
enum class event_type {
    none,
    quit,
    close,
    focus_gained,
    focus_lost,
    key_down,
    key_up,
    text_input,
    mouse_motion,
    mouse_down,
    mouse_up,
    mouse_wheel
};
enum class mouse_button { none, left, middle, right, extra_1, extra_2 };
inline constexpr unsigned left_button = 1, middle_button = 2, right_button = 4;
enum class cursor_shape { arrow, hand, text };
struct key_state {
    std::array<bool, static_cast<unsigned>(key_code::count)> values{};
    bool operator[](key_code code) const { return values.at(static_cast<unsigned>(code)); }
    bool& operator[](key_code code) { return values.at(static_cast<unsigned>(code)); }
    void fill(bool value) { values.fill(value); }
};
struct input_event {
    event_type type{};
    int logical_width{}, logical_height{};
    struct {
        key_code code{};
        bool repeat{};
    } key;
    struct {
        float x{}, y{}, dx{}, dy{};
        unsigned buttons{};
    } motion;
    struct {
        float x{}, y{}, mouse_x{}, mouse_y{};
    } wheel;
    struct {
        mouse_button button{};
    } button;
    std::string text;
};
}
