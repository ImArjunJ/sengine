#pragma once
#include "native_hud.hpp"
#include "sengine/drawing.hpp"
#include <array>
#include <deque>
namespace sengine {

class native_canvas {
  public:
    native_canvas();
    ~native_canvas();
    void begin_events();
    void event(const SDL_Event&);
    void draw(native_hud&, int width, int height, float dpi, float delta, hud_input, bool focused);
    native_hud* hud{};
    int width{}, height{};
    float dpi{1}, delta{};
    hud_input input{};
    drawing::transform transform{};
    std::array<bool, SDL_SCANCODE_COUNT> pressed{}, down{};
    std::deque<unsigned> text;
    std::array<SDL_Cursor*, 3> cursors{};
};
}
