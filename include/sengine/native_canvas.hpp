#pragma once
#include "drawing.hpp"
#include "native_hud.hpp"
#include "window.hpp"
#include <memory>

namespace sengine {
struct canvas_state;
class native_canvas {
  public:
    class binding {
      public:
        ~binding();
        binding(const binding&) = delete;
        binding& operator=(const binding&) = delete;

      private:
        explicit binding(canvas_state&);
        friend class native_canvas;

      private:
        canvas_state* previous_;
    };

    native_canvas(window&, native_hud&);
    ~native_canvas();
    native_canvas(const native_canvas&) = delete;
    native_canvas& operator=(const native_canvas&) = delete;
    [[nodiscard]] binding activate() const;
    void begin_events();
    void event(const input_event&);
    void begin_frame(int width, int height, float dpi, float delta, hud_input, bool focused);
    int width() const;
    int height() const;
    hud_input pointer() const;
    bool key_pressed(key_code) const;
    bool key_down(key_code) const;
    bool consume_key_press(key_code);

  private:
    std::unique_ptr<canvas_state> state_;
};
}
