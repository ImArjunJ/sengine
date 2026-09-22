#pragma once
#include "events.hpp"
#include <filesystem>
#include <memory>
#include <string>

namespace sengine {
enum class graphics_api { automatic, opengl, vulkan, metal };
struct window_options {
    graphics_api graphics{graphics_api::automatic};
};
struct window_metrics {
    int width{}, height{}, logical_width{}, logical_height{};
    float scale{1};
    bool focused{}, minimized{};
    double refresh_rate{60};
};
struct pointer_state {
    float x{}, y{};
    unsigned buttons{};
};
struct native_surface;
class window {
  public:
    window(const std::string& title, int width, int height, window_options options = {});
    ~window();
    window(const window&) = delete;
    window& operator=(const window&) = delete;
    bool poll(input_event&);
    window_metrics metrics() const;
    key_state keys() const;
    pointer_state pointer() const;
    bool captured() const;
    bool capture(bool);
    bool fullscreen(bool);
    void text_input(bool);
    void cursor(cursor_shape);
    std::string error() const;
    static std::filesystem::path executable_directory();
    graphics_api graphics() const;

  private:
    friend struct native_surface;
    struct impl;
    std::unique_ptr<impl> impl_;
};
std::filesystem::path user_data_directory(const std::string& application);
void sleep_for(double seconds);
}
