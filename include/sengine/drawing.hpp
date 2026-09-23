#pragma once
#include "sengine/events.hpp"
#include "sengine/native_hud.hpp"
#include <string>
namespace sengine::drawing {
struct point2 {
    float x{}, y{};
};
struct rect {
    float x{}, y{}, width{}, height{};
};
struct color {
    unsigned char r{}, g{}, b{}, a{255};
};
struct font {
    int face{};
};
struct transform {
    point2 offset, target;
    float zoom{1};
};
inline constexpr color transparent{0, 0, 0, 0};
using cursor = sengine::cursor_shape;
void begin_transform(transform);
void end_transform();
void begin_scissor_mode(int, int, int, int);
void end_scissor_mode();
void draw_rectangle_rec(rect, color);
void draw_rectangle_lines_ex(rect, float, color);
void draw_rectangle_rounded(rect, float, int, color);
void draw_rectangle_gradient_h(int, int, int, int, color, color);
void draw_rectangle_gradient_v(int, int, int, int, color, color);
void draw_line_ex(point2, point2, float, color);
void draw_triangle(point2, point2, point2, color);
void draw_circle(int, int, float, color);
void draw_circle(point2, float, color);
void draw_circle_lines(int, int, float, color);
void draw_circle_lines(point2, float, color);
void draw_ellipse(int, int, float, float, color);
void draw_ellipse_lines(int, int, float, float, color);
void draw_text_ex(font, const char*, point2, float, float, color);
hud_image upload_image(unsigned width, unsigned height, std::span<const std::uint8_t> rgba);
void draw_image_rect(hud_image, rect, color tint = {255, 255, 255, 255});
void draw_tiled_image(hud_image, rect, point2 tile_pixels, color tint = {255, 255, 255, 255});
point2 measure_text_ex(font, const char*, float, float);
color fade(color, float);
color color_lerp(color, color, float);
bool check_collision_point_rec(point2, rect);
bool check_collision_point_circle(point2, point2, float);
int canvas_width();
int canvas_height();
point2 display_scale();
point2 get_mouse_position();
float get_mouse_wheel_move();
float get_frame_time();
double get_time();
bool is_mouse_button_pressed();
bool is_mouse_button_released();
bool is_mouse_button_down();
bool is_key_down(key_code);
bool is_key_pressed(key_code);
int get_char_pressed();
const char* codepoint_to_utf8(int, int*);
void set_mouse_cursor(cursor);
}
namespace sengine {
using drawing::color;
using drawing::font;
using drawing::point2;
using drawing::rect;
}
