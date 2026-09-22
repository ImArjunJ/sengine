#include "sengine/native_canvas.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace {
using namespace sengine::drawing;
using sengine::native_canvas;
native_canvas* canvas{};
native_canvas& context() {
    if (!canvas)
        throw std::logic_error("UI canvas is not bound");
    return *canvas;
}
point2 position(point2 p) {
    auto t = context().transform;
    return {(p.x - t.target.x) * t.zoom + t.offset.x, (p.y - t.target.y) * t.zoom + t.offset.y};
}
sengine::ink to_ink(color c) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
}
void triangle(point2 a, point2 b, point2 c, color ca, color cb, color cc) {
    a = position(a);
    b = position(b);
    c = position(c);
    context().hud->triangle({a.x, a.y}, {b.x, b.y}, {c.x, c.y}, to_ink(ca), to_ink(cb), to_ink(cc));
}
void ellipse(point2 c, float rx, float ry, color color, bool outline) {
    constexpr float tau = 2 * std::numbers::pi_v<float>;
    int segments = std::clamp(int(std::max(rx, ry) * context().transform.zoom * .8f), 20, 96);
    for (int i = 0; i < segments; ++i) {
        float a = tau * i / segments, b = tau * (i + 1) / segments;
        point2 p{c.x + std::cos(a) * rx, c.y + std::sin(a) * ry},
            q{c.x + std::cos(b) * rx, c.y + std::sin(b) * ry};
        if (outline)
            draw_line_ex(p, q, 1, color);
        else
            triangle(c, p, q, color, color, color);
    }
}
}
namespace sengine {
native_canvas::native_canvas() {
    canvas = this;
}
native_canvas::~native_canvas() {
    if (canvas == this)
        canvas = nullptr;
}
void native_canvas::begin_events() {
    pressed.fill(false);
    text.clear();
}
void native_canvas::event(const sengine::input_event& e) {
    if (e.type == sengine::event_type::mouse_motion || e.type == sengine::event_type::mouse_wheel) {
        int logical_width = e.logical_width, logical_height = e.logical_height;
        float x = e.type == sengine::event_type::mouse_motion ? e.motion.x : e.wheel.mouse_x;
        float y = e.type == sengine::event_type::mouse_motion ? e.motion.y : e.wheel.mouse_y;
        input.x = x * width / std::max(1, logical_width);
        input.y = y * height / std::max(1, logical_height);
    }
    if (e.type == sengine::event_type::key_down) {
        down[e.key.code] = true;
        if (!e.key.repeat)
            pressed[e.key.code] = true;
    }
    if (e.type == sengine::event_type::key_up)
        down[e.key.code] = false;
    if (e.type == sengine::event_type::focus_lost) {
        down.fill(false);
        pressed.fill(false);
        text.clear();
    }
    if (e.type == sengine::event_type::text_input) {
        const auto* p = reinterpret_cast<const unsigned char*>(e.text.c_str());
        while (*p) {
            unsigned c = *p++;
            int n = 0;
            if (c >= 0xc2 && c <= 0xdf) {
                c &= 31;
                n = 1;
            } else if (c >= 0xe0 && c <= 0xef) {
                c &= 15;
                n = 2;
            } else if (c >= 0xf0) {
                c &= 7;
                n = 3;
            }
            while (n-- && *p)
                c = (c << 6) | (*p++ & 63);
            text.push_back(c);
        }
    }
}
void native_canvas::draw(native_hud& h, int w, int hh, float scale, float dt, hud_input in, bool focused) {
    hud = &h;
    width = w;
    height = hh;
    dpi = scale;
    delta = dt;
    input = focused ? in : hud_input{};
    transform = {};
    transform.zoom = 1;
    canvas = this;
}
}
namespace sengine::drawing {
void begin_transform(transform c) {
    context().transform = c;
}
void end_transform() {
    context().transform = {};
    context().transform.zoom = 1;
}
void begin_scissor_mode(int x, int y, int w, int h) {
    context().hud->clip(x, y, w, h);
}
void end_scissor_mode() {
    context().hud->clear_clip();
}
void draw_rectangle_rec(rect r, color c) {
    auto p = position({r.x, r.y});
    float z = context().transform.zoom;
    context().hud->rectangle(p.x, p.y, r.width * z, r.height * z, to_ink(c));
}
void draw_rectangle_lines_ex(rect r, float t, color c) {
    draw_line_ex({r.x, r.y}, {r.x + r.width, r.y}, t, c);
    draw_line_ex({r.x + r.width, r.y}, {r.x + r.width, r.y + r.height}, t, c);
    draw_line_ex({r.x + r.width, r.y + r.height}, {r.x, r.y + r.height}, t, c);
    draw_line_ex({r.x, r.y + r.height}, {r.x, r.y}, t, c);
}
void draw_rectangle_rounded(rect r, float roundness, int, color c) {
    float rad = std::min(r.width, r.height) * roundness * .5f;
    draw_rectangle_rec({r.x + rad, r.y, r.width - 2 * rad, r.height}, c);
    draw_rectangle_rec({r.x, r.y + rad, rad, r.height - 2 * rad}, c);
    draw_rectangle_rec({r.x + r.width - rad, r.y + rad, rad, r.height - 2 * rad}, c);
    for (int corner = 0; corner < 4; ++corner) {
        point2 centre{r.x + (corner == 0 || corner == 3 ? rad : r.width - rad),
                      r.y + (corner < 2 ? rad : r.height - rad)};
        float start = (corner + 2) * std::numbers::pi_v<float> * .5f;
        for (int j = 0; j < 8; ++j) {
            float a = start + j * std::numbers::pi_v<float> / 16, b = a + std::numbers::pi_v<float> / 16;
            triangle(centre, {centre.x + std::cos(a) * rad, centre.y + std::sin(a) * rad},
                     {centre.x + std::cos(b) * rad, centre.y + std::sin(b) * rad}, c, c, c);
        }
    }
}
void draw_rectangle_gradient_h(int x, int y, int w, int h, color a, color b) {
    point2 p{float(x), float(y)}, q{float(x + w), float(y)}, r{float(x + w), float(y + h)},
        s{float(x), float(y + h)};
    triangle(p, q, r, a, b, b);
    triangle(p, r, s, a, b, a);
}
void draw_rectangle_gradient_v(int x, int y, int w, int h, color a, color b) {
    point2 p{float(x), float(y)}, q{float(x + w), float(y)}, r{float(x + w), float(y + h)},
        s{float(x), float(y + h)};
    triangle(p, q, r, a, a, b);
    triangle(p, r, s, a, b, b);
}
void draw_line_ex(point2 a, point2 b, float t, color c) {
    a = position(a);
    b = position(b);
    context().hud->line(a.x, a.y, b.x, b.y, t * context().transform.zoom, to_ink(c));
}
void draw_triangle(point2 a, point2 b, point2 c, color color) {
    triangle(a, b, c, color, color, color);
}
void draw_circle(int x, int y, float radius, color c) {
    draw_circle({float(x), float(y)}, radius, c);
}
void draw_circle(point2 p, float r, color c) {
    ellipse(p, r, r, c, false);
}
void draw_circle_lines(int x, int y, float r, color c) {
    draw_circle_lines({float(x), float(y)}, r, c);
}
void draw_circle_lines(point2 p, float r, color c) {
    ellipse(p, r, r, c, true);
}
void draw_ellipse(int x, int y, float rx, float ry, color c) {
    ellipse({float(x), float(y)}, rx, ry, c, false);
}
void draw_ellipse_lines(int x, int y, float rx, float ry, color c) {
    ellipse({float(x), float(y)}, rx, ry, c, true);
}
void draw_text_ex(font font, const char* text, point2 p, float size, float, color color) {
    p = position(p);
    context().hud->text(p.x, p.y, text, size * context().transform.zoom, to_ink(color), font.face);
}
point2 measure_text_ex(font font, const char* text, float size, float) {
    return {context().hud->measure(text, size, font.face), size};
}
void draw_paper_rect(rect r) {
    auto p = position({r.x, r.y});
    context().hud->paper_texture(p.x, p.y, r.width * context().transform.zoom,
                                 r.height * context().transform.zoom);
}
color fade(color c, float a) {
    c.a = static_cast<unsigned char>(std::clamp(a, 0.f, 1.f) * c.a);
    return c;
}
color color_lerp(color a, color b, float t) {
    auto mix = [t](int x, int y) {
        return static_cast<unsigned char>(std::clamp(x + (y - x) * t, 0.f, 255.f));
    };
    return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
}
bool check_collision_point_rec(point2 p, rect r) {
    return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height;
}
bool check_collision_point_circle(point2 p, point2 c, float r) {
    return std::hypot(p.x - c.x, p.y - c.y) <= r;
}
int canvas_width() {
    return context().width;
}
int canvas_height() {
    return context().height;
}
point2 display_scale() {
    return {context().dpi, context().dpi};
}
point2 get_mouse_position() {
    return {context().input.x, context().input.y};
}
float get_mouse_wheel_move() {
    return context().input.wheel;
}
float get_frame_time() {
    return context().delta;
}
double get_time() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool is_mouse_button_pressed() {
    return context().input.pressed;
}
bool is_mouse_button_released() {
    return context().input.released;
}
bool is_mouse_button_down() {
    return context().input.down;
}
bool is_key_down(key_code key) {
    return context().down[key];
}
bool is_key_pressed(key_code key) {
    return context().pressed[key];
}
int get_char_pressed() {
    if (context().text.empty())
        return 0;
    auto c = context().text.front();
    context().text.pop_front();
    return int(c);
}
const char* codepoint_to_utf8(int cp, int* size) {
    static thread_local char text[5]{};
    *size = 0;
    if (cp < 128)
        text[(*size)++] = char(cp);
    else {
        int n = cp < 2048 ? 2 : cp < 65536 ? 3 : 4;
        text[0] = char((n == 2 ? 0xc0 : n == 3 ? 0xe0 : 0xf0) | (cp >> (6 * (n - 1))));
        for (int i = 1; i < n; ++i)
            text[i] = char(0x80 | ((cp >> (6 * (n - i - 1))) & 63));
        *size = n;
    }
    text[*size] = 0;
    return text;
}
void set_mouse_cursor(cursor cursor) {
    if (context().display)
        context().display->cursor(cursor);
}

}
