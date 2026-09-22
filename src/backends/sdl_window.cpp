#include "native_surface.hpp"
#include "sengine/window.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
#ifdef __APPLE__
#include <SDL3/SDL_metal.h>
#endif

namespace sengine {
struct window::impl {
    SDL_Window* window{};
    void* native{};
    SDL_GLContext context{};
    bool initialized{};
    graphics_api api{};
    std::array<SDL_Cursor*, 3> cursors{};
#ifdef __APPLE__
    SDL_MetalView metal{};
#endif
    ~impl() {
        for (auto* cursor : cursors)
            if (cursor)
                SDL_DestroyCursor(cursor);
#ifdef __APPLE__
        if (metal)
            SDL_Metal_DestroyView(metal);
#endif
        if (context)
            SDL_GL_DestroyContext(context);
        if (window)
            SDL_DestroyWindow(window);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};
window::window(const std::string& title, int width, int height, window_options options)
    : impl_(std::make_unique<impl>()) {
#if defined(__linux__)
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE);
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    auto& p = *impl_;
    p.api = options.graphics;
    if (p.api == graphics_api::automatic)
        p.api = graphics_api::sengine_default_graphics;

    p.initialized = true;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __APPLE__
    if (p.api != graphics_api::metal)
        throw std::runtime_error("This macOS backend requires Metal");
    flags |= SDL_WINDOW_METAL;
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    if (p.api == graphics_api::opengl)
        flags |= SDL_WINDOW_OPENGL;
    else if (p.api == graphics_api::vulkan)
        flags |= SDL_WINDOW_VULKAN;
    else
        throw std::runtime_error("Metal requires macOS");
#endif
    p.window = SDL_CreateWindow(title.c_str(), width, height, flags);
    if (!p.window)
        throw std::runtime_error(SDL_GetError());
#ifdef __APPLE__
    p.metal = SDL_Metal_CreateView(p.window);
    if (!p.metal)
        throw std::runtime_error(SDL_GetError());
    p.native = SDL_Metal_GetLayer(p.metal);
#else
    if (p.api == graphics_api::opengl) {
        p.context = SDL_GL_CreateContext(p.window);
        if (!p.context)
            throw std::runtime_error(SDL_GetError());
        if (!SDL_GL_MakeCurrent(p.window, nullptr))
            throw std::runtime_error(SDL_GetError());
    }
    p.native = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
        SDL_GetNumberProperty(SDL_GetWindowProperties(p.window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
#endif
    if (!p.native)
        throw std::runtime_error("Native graphics window unavailable");
}
window::~window() = default;
void* native_surface::handle(window& w) {
    return w.impl_->native;
}
void* native_surface::shared_context(window& w) {
    return w.impl_->context;
}
void native_surface::release_context(window& w) {
    if (w.impl_->context) {
        SDL_GL_DestroyContext(w.impl_->context);
        w.impl_->context = nullptr;
    }
}
graphics_api window::graphics() const {
    return impl_->api;
}
namespace {
constexpr std::array key_mapping{std::pair{SDL_SCANCODE_A, key_code::a},
                                 std::pair{SDL_SCANCODE_B, key_code::b},
                                 std::pair{SDL_SCANCODE_C, key_code::c},
                                 std::pair{SDL_SCANCODE_D, key_code::d},
                                 std::pair{SDL_SCANCODE_E, key_code::e},
                                 std::pair{SDL_SCANCODE_F, key_code::f},
                                 std::pair{SDL_SCANCODE_G, key_code::g},
                                 std::pair{SDL_SCANCODE_H, key_code::h},
                                 std::pair{SDL_SCANCODE_I, key_code::i},
                                 std::pair{SDL_SCANCODE_J, key_code::j},
                                 std::pair{SDL_SCANCODE_K, key_code::k},
                                 std::pair{SDL_SCANCODE_L, key_code::l},
                                 std::pair{SDL_SCANCODE_M, key_code::m},
                                 std::pair{SDL_SCANCODE_N, key_code::n},
                                 std::pair{SDL_SCANCODE_O, key_code::o},
                                 std::pair{SDL_SCANCODE_P, key_code::p},
                                 std::pair{SDL_SCANCODE_Q, key_code::q},
                                 std::pair{SDL_SCANCODE_R, key_code::r},
                                 std::pair{SDL_SCANCODE_S, key_code::s},
                                 std::pair{SDL_SCANCODE_T, key_code::t},
                                 std::pair{SDL_SCANCODE_U, key_code::u},
                                 std::pair{SDL_SCANCODE_V, key_code::v},
                                 std::pair{SDL_SCANCODE_W, key_code::w},
                                 std::pair{SDL_SCANCODE_X, key_code::x},
                                 std::pair{SDL_SCANCODE_Y, key_code::y},
                                 std::pair{SDL_SCANCODE_Z, key_code::z},
                                 std::pair{SDL_SCANCODE_1, key_code::digit_1},
                                 std::pair{SDL_SCANCODE_2, key_code::digit_2},
                                 std::pair{SDL_SCANCODE_3, key_code::digit_3},
                                 std::pair{SDL_SCANCODE_4, key_code::digit_4},
                                 std::pair{SDL_SCANCODE_5, key_code::digit_5},
                                 std::pair{SDL_SCANCODE_6, key_code::digit_6},
                                 std::pair{SDL_SCANCODE_7, key_code::digit_7},
                                 std::pair{SDL_SCANCODE_8, key_code::digit_8},
                                 std::pair{SDL_SCANCODE_9, key_code::digit_9},
                                 std::pair{SDL_SCANCODE_0, key_code::digit_0},
                                 std::pair{SDL_SCANCODE_RETURN, key_code::enter},
                                 std::pair{SDL_SCANCODE_ESCAPE, key_code::escape},
                                 std::pair{SDL_SCANCODE_BACKSPACE, key_code::backspace},
                                 std::pair{SDL_SCANCODE_TAB, key_code::tab},
                                 std::pair{SDL_SCANCODE_SPACE, key_code::space},
                                 std::pair{SDL_SCANCODE_HOME, key_code::home},
                                 std::pair{SDL_SCANCODE_END, key_code::end},
                                 std::pair{SDL_SCANCODE_LEFT, key_code::left},
                                 std::pair{SDL_SCANCODE_RIGHT, key_code::right},
                                 std::pair{SDL_SCANCODE_UP, key_code::up},
                                 std::pair{SDL_SCANCODE_DOWN, key_code::down},
                                 std::pair{SDL_SCANCODE_DELETE, key_code::delete_key},
                                 std::pair{SDL_SCANCODE_PAGEUP, key_code::page_up},
                                 std::pair{SDL_SCANCODE_PAGEDOWN, key_code::page_down},
                                 std::pair{SDL_SCANCODE_LSHIFT, key_code::left_shift},
                                 std::pair{SDL_SCANCODE_RSHIFT, key_code::right_shift},
                                 std::pair{SDL_SCANCODE_LCTRL, key_code::left_control},
                                 std::pair{SDL_SCANCODE_RCTRL, key_code::right_control},
                                 std::pair{SDL_SCANCODE_LALT, key_code::left_alt},
                                 std::pair{SDL_SCANCODE_RALT, key_code::right_alt},
                                 std::pair{SDL_SCANCODE_LGUI, key_code::left_super},
                                 std::pair{SDL_SCANCODE_RGUI, key_code::right_super},
                                 std::pair{SDL_SCANCODE_F1, key_code::f1},
                                 std::pair{SDL_SCANCODE_F2, key_code::f2},
                                 std::pair{SDL_SCANCODE_F3, key_code::f3},
                                 std::pair{SDL_SCANCODE_F4, key_code::f4},
                                 std::pair{SDL_SCANCODE_F5, key_code::f5},
                                 std::pair{SDL_SCANCODE_F6, key_code::f6},
                                 std::pair{SDL_SCANCODE_F7, key_code::f7},
                                 std::pair{SDL_SCANCODE_F8, key_code::f8},
                                 std::pair{SDL_SCANCODE_F9, key_code::f9},
                                 std::pair{SDL_SCANCODE_F10, key_code::f10},
                                 std::pair{SDL_SCANCODE_F11, key_code::f11},
                                 std::pair{SDL_SCANCODE_F12, key_code::f12}};
key_code translate(SDL_Scancode code) {
    for (auto [native, key] : key_mapping)
        if (native == code)
            return key;
    return key_code::unknown;
}
}
bool window::poll(input_event& result) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        result = {};
        SDL_GetWindowSize(impl_->window, &result.logical_width, &result.logical_height);
        switch (e.type) {
        case SDL_EVENT_QUIT:
            result.type = event_type::quit;
            break;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            if (e.window.windowID != SDL_GetWindowID(impl_->window))
                continue;
            result.type = event_type::close;
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            result.type = event_type::focus_gained;
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            result.type = event_type::focus_lost;
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            result.type = e.type == SDL_EVENT_KEY_DOWN ? event_type::key_down : event_type::key_up;
            result.key = {translate(e.key.scancode), e.key.repeat};
            break;
        case SDL_EVENT_TEXT_INPUT:
            result.type = event_type::text_input;
            result.text = e.text.text;
            break;
        case SDL_EVENT_MOUSE_MOTION:
            result.type = event_type::mouse_motion;
            result.motion = {e.motion.x, e.motion.y, e.motion.xrel, e.motion.yrel,
                             unsigned((e.motion.state & SDL_BUTTON_LMASK ? left_button : 0) |
                                      (e.motion.state & SDL_BUTTON_MMASK ? middle_button : 0) |
                                      (e.motion.state & SDL_BUTTON_RMASK ? right_button : 0))};
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            result.type =
                e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? event_type::mouse_down : event_type::mouse_up;
            result.button.button = e.button.button == SDL_BUTTON_LEFT     ? mouse_button::left
                                   : e.button.button == SDL_BUTTON_MIDDLE ? mouse_button::middle
                                   : e.button.button == SDL_BUTTON_RIGHT  ? mouse_button::right
                                                                          : mouse_button::none;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            result.type = event_type::mouse_wheel;
            result.wheel = {e.wheel.x, e.wheel.y, e.wheel.mouse_x, e.wheel.mouse_y};
            break;
        default:
            continue;
        }
        return true;
    }
    return false;
}
window_metrics window::metrics() const {
    window_metrics result;
    auto* w = impl_->window;
    SDL_GetWindowSizeInPixels(w, &result.width, &result.height);
    SDL_GetWindowSize(w, &result.logical_width, &result.logical_height);
    result.scale =
        std::max(SDL_GetWindowDisplayScale(w), float(result.width) / std::max(1, result.logical_width));
    auto flags = SDL_GetWindowFlags(w);
    result.focused = flags & SDL_WINDOW_INPUT_FOCUS;
    result.minimized = flags & SDL_WINDOW_MINIMIZED;
    if (const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(w)))
        result.refresh_rate = mode->refresh_rate;
    return result;
}
key_state window::keys() const {
    key_state result;
    const auto* keys = SDL_GetKeyboardState(nullptr);
    for (auto [native, key] : key_mapping)
        result[key] = keys[native];
    return result;
}
pointer_state window::pointer() const {
    pointer_state result;
    auto buttons = SDL_GetMouseState(&result.x, &result.y);
    result.buttons = (buttons & SDL_BUTTON_LMASK ? left_button : 0) |
                     (buttons & SDL_BUTTON_MMASK ? middle_button : 0) |
                     (buttons & SDL_BUTTON_RMASK ? right_button : 0);
    return result;
}
bool window::captured() const {
    return SDL_GetWindowRelativeMouseMode(impl_->window);
}
bool window::capture(bool enabled) {
    return SDL_SetWindowRelativeMouseMode(impl_->window, enabled);
}
bool window::fullscreen(bool enabled) {
    return SDL_SetWindowFullscreen(impl_->window, enabled);
}
void window::text_input(bool enabled) {
    if (enabled)
        SDL_StartTextInput(impl_->window);
    else
        SDL_StopTextInput(impl_->window);
}
void window::cursor(cursor_shape shape) {
    auto index = static_cast<unsigned>(shape);
    auto*& cursor = impl_->cursors.at(index);
    if (!cursor)
        cursor = SDL_CreateSystemCursor(index == 1   ? SDL_SYSTEM_CURSOR_POINTER
                                        : index == 2 ? SDL_SYSTEM_CURSOR_TEXT
                                                     : SDL_SYSTEM_CURSOR_DEFAULT);
    if (cursor)
        SDL_SetCursor(cursor);
}
std::string window::error() const {
    return SDL_GetError();
}
}
