#include "native_surface.hpp"
#include "sengine/window.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#if defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>
#include <algorithm>
#include <array>
#include <deque>
#include <stdexcept>
#include <utility>
namespace sengine {
#ifdef __APPLE__
void* make_metal_layer(void*);
#endif
namespace {
unsigned users{};
constexpr std::array key_mapping{std::pair{GLFW_KEY_A, key_code::a},
                                 std::pair{GLFW_KEY_B, key_code::b},
                                 std::pair{GLFW_KEY_C, key_code::c},
                                 std::pair{GLFW_KEY_D, key_code::d},
                                 std::pair{GLFW_KEY_E, key_code::e},
                                 std::pair{GLFW_KEY_F, key_code::f},
                                 std::pair{GLFW_KEY_G, key_code::g},
                                 std::pair{GLFW_KEY_H, key_code::h},
                                 std::pair{GLFW_KEY_I, key_code::i},
                                 std::pair{GLFW_KEY_J, key_code::j},
                                 std::pair{GLFW_KEY_K, key_code::k},
                                 std::pair{GLFW_KEY_L, key_code::l},
                                 std::pair{GLFW_KEY_M, key_code::m},
                                 std::pair{GLFW_KEY_N, key_code::n},
                                 std::pair{GLFW_KEY_O, key_code::o},
                                 std::pair{GLFW_KEY_P, key_code::p},
                                 std::pair{GLFW_KEY_Q, key_code::q},
                                 std::pair{GLFW_KEY_R, key_code::r},
                                 std::pair{GLFW_KEY_S, key_code::s},
                                 std::pair{GLFW_KEY_T, key_code::t},
                                 std::pair{GLFW_KEY_U, key_code::u},
                                 std::pair{GLFW_KEY_V, key_code::v},
                                 std::pair{GLFW_KEY_W, key_code::w},
                                 std::pair{GLFW_KEY_X, key_code::x},
                                 std::pair{GLFW_KEY_Y, key_code::y},
                                 std::pair{GLFW_KEY_Z, key_code::z},
                                 std::pair{GLFW_KEY_1, key_code::digit_1},
                                 std::pair{GLFW_KEY_2, key_code::digit_2},
                                 std::pair{GLFW_KEY_3, key_code::digit_3},
                                 std::pair{GLFW_KEY_4, key_code::digit_4},
                                 std::pair{GLFW_KEY_5, key_code::digit_5},
                                 std::pair{GLFW_KEY_6, key_code::digit_6},
                                 std::pair{GLFW_KEY_7, key_code::digit_7},
                                 std::pair{GLFW_KEY_8, key_code::digit_8},
                                 std::pair{GLFW_KEY_9, key_code::digit_9},
                                 std::pair{GLFW_KEY_0, key_code::digit_0},
                                 std::pair{GLFW_KEY_ENTER, key_code::enter},
                                 std::pair{GLFW_KEY_ESCAPE, key_code::escape},
                                 std::pair{GLFW_KEY_BACKSPACE, key_code::backspace},
                                 std::pair{GLFW_KEY_TAB, key_code::tab},
                                 std::pair{GLFW_KEY_SPACE, key_code::space},
                                 std::pair{GLFW_KEY_HOME, key_code::home},
                                 std::pair{GLFW_KEY_END, key_code::end},
                                 std::pair{GLFW_KEY_LEFT, key_code::left},
                                 std::pair{GLFW_KEY_RIGHT, key_code::right},
                                 std::pair{GLFW_KEY_UP, key_code::up},
                                 std::pair{GLFW_KEY_DOWN, key_code::down},
                                 std::pair{GLFW_KEY_DELETE, key_code::delete_key},
                                 std::pair{GLFW_KEY_PAGE_UP, key_code::page_up},
                                 std::pair{GLFW_KEY_PAGE_DOWN, key_code::page_down},
                                 std::pair{GLFW_KEY_LEFT_SHIFT, key_code::left_shift},
                                 std::pair{GLFW_KEY_RIGHT_SHIFT, key_code::right_shift},
                                 std::pair{GLFW_KEY_LEFT_CONTROL, key_code::left_control},
                                 std::pair{GLFW_KEY_RIGHT_CONTROL, key_code::right_control},
                                 std::pair{GLFW_KEY_LEFT_ALT, key_code::left_alt},
                                 std::pair{GLFW_KEY_RIGHT_ALT, key_code::right_alt},
                                 std::pair{GLFW_KEY_LEFT_SUPER, key_code::left_super},
                                 std::pair{GLFW_KEY_RIGHT_SUPER, key_code::right_super},
                                 std::pair{GLFW_KEY_F1, key_code::f1},
                                 std::pair{GLFW_KEY_F2, key_code::f2},
                                 std::pair{GLFW_KEY_F3, key_code::f3},
                                 std::pair{GLFW_KEY_F4, key_code::f4},
                                 std::pair{GLFW_KEY_F5, key_code::f5},
                                 std::pair{GLFW_KEY_F6, key_code::f6},
                                 std::pair{GLFW_KEY_F7, key_code::f7},
                                 std::pair{GLFW_KEY_F8, key_code::f8},
                                 std::pair{GLFW_KEY_F9, key_code::f9},
                                 std::pair{GLFW_KEY_F10, key_code::f10},
                                 std::pair{GLFW_KEY_F11, key_code::f11},
                                 std::pair{GLFW_KEY_F12, key_code::f12}};
key_code translate(int code) {
    for (auto [native, key] : key_mapping)
        if (native == code)
            return key;
    return key_code::unknown;
}
unsigned buttons(GLFWwindow* w) {
    return (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_LEFT) ? left_button : 0) |
           (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_MIDDLE) ? middle_button : 0) |
           (glfwGetMouseButton(w, GLFW_MOUSE_BUTTON_RIGHT) ? right_button : 0);
}
std::string utf8(unsigned code) {
    std::string result;
    if (code < 128)
        result += char(code);
    else {
        int n = code < 2048 ? 2 : code < 65536 ? 3 : 4;
        result += char((n == 2 ? 0xc0 : n == 3 ? 0xe0 : 0xf0) | (code >> (6 * (n - 1))));
        for (int i = 1; i < n; ++i)
            result += char(0x80 | ((code >> (6 * (n - i - 1))) & 63));
    }
    return result;
}
}
struct window::impl {
    GLFWwindow* handle{};
    graphics_api api{};
    bool initialized{}, typing{}, relative{}, fullscreen{};
    int x{}, y{}, width{}, height{};
    double last_x{}, last_y{};
    std::deque<input_event> events;
    std::array<GLFWcursor*, 3> cursors{};
    void* layer{};

  public:
    static impl& state(GLFWwindow* w) { return *static_cast<impl*>(glfwGetWindowUserPointer(w)); }
    void enqueue(input_event event) {
        glfwGetWindowSize(handle, &event.logical_width, &event.logical_height);
        events.push_back(std::move(event));
    }
    static void closed(GLFWwindow* w) {
        glfwSetWindowShouldClose(w, false);
        impl::state(w).enqueue({.type = event_type::close});
    }
    static void focused(GLFWwindow* w, int focus) {
        impl::state(w).enqueue({.type = focus ? event_type::focus_gained : event_type::focus_lost});
    }
    static void key_changed(GLFWwindow* w, int key, int, int action, int) {
        input_event event{.type = action == GLFW_RELEASE ? event_type::key_up : event_type::key_down};
        event.key = {translate(key), action == GLFW_REPEAT};
        impl::state(w).enqueue(std::move(event));
    }
    static void character_entered(GLFWwindow* w, unsigned code) {
        auto& p = impl::state(w);
        if (p.typing) {
            input_event event{.type = event_type::text_input};
            event.text = utf8(code);
            p.enqueue(std::move(event));
        }
    }
    static void pointer_moved(GLFWwindow* w, double x, double y) {
        auto& p = impl::state(w);
        input_event event{.type = event_type::mouse_motion};
        event.motion = {float(x), float(y), float(x - p.last_x), float(y - p.last_y), buttons(w)};
        p.last_x = x;
        p.last_y = y;
        p.enqueue(std::move(event));
    }
    static void button_changed(GLFWwindow* w, int button, int action, int) {
        input_event event{.type = action == GLFW_PRESS ? event_type::mouse_down : event_type::mouse_up};
        event.button.button = button == GLFW_MOUSE_BUTTON_LEFT     ? mouse_button::left
                              : button == GLFW_MOUSE_BUTTON_MIDDLE ? mouse_button::middle
                              : button == GLFW_MOUSE_BUTTON_RIGHT  ? mouse_button::right
                                                                   : mouse_button::none;
        impl::state(w).enqueue(std::move(event));
    }
    static void scrolled(GLFWwindow* w, double x, double y) {
        double mx, my;
        glfwGetCursorPos(w, &mx, &my);
        input_event event{.type = event_type::mouse_wheel};
        event.wheel = {float(x), float(y), float(mx), float(my)};
        impl::state(w).enqueue(std::move(event));
    }
    void bind_window_events() {
        glfwSetWindowCloseCallback(handle, closed);
        glfwSetWindowFocusCallback(handle, focused);
    }
    void bind_keyboard_events() {
        glfwSetKeyCallback(handle, key_changed);
        glfwSetCharCallback(handle, character_entered);
    }
    void bind_pointer_events() {
        glfwSetCursorPosCallback(handle, pointer_moved);
        glfwSetMouseButtonCallback(handle, button_changed);
        glfwSetScrollCallback(handle, scrolled);
    }
    ~impl() {
        for (auto* cursor : cursors)
            if (cursor)
                glfwDestroyCursor(cursor);
        if (handle)
            glfwDestroyWindow(handle);
        if (initialized && --users == 0)
            glfwTerminate();
    }
};
window::window(const std::string& title, int width, int height, window_options options)
    : impl_(std::make_unique<impl>()) {
    auto& p = *impl_;
    if (!users) {
#if defined(__linux__) && defined(GLFW_PLATFORM)
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
        if (!glfwInit())
            throw std::runtime_error("Cannot initialize GLFW");
    }
    ++users;
    p.initialized = true;
    p.api = options.graphics == graphics_api::automatic ? graphics_api::sengine_default_graphics
                                                        : options.graphics;
#ifdef __APPLE__
    if (p.api != graphics_api::metal)
        throw std::runtime_error("This macOS backend requires Metal");
#endif
    glfwDefaultWindowHints();
    if (p.api == graphics_api::opengl) {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    } else
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    p.handle = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!p.handle)
        throw std::runtime_error(error());
    glfwSetWindowUserPointer(p.handle, &p);
    glfwGetCursorPos(p.handle, &p.last_x, &p.last_y);
#ifdef __APPLE__
    if (p.api == graphics_api::metal)
        p.layer = make_metal_layer(glfwGetCocoaWindow(p.handle));
#endif
    p.bind_window_events();
    p.bind_keyboard_events();
    p.bind_pointer_events();
}
window::~window() = default;
bool window::poll(input_event& event) {
    if (impl_->events.empty())
        glfwPollEvents();
    if (impl_->events.empty())
        return false;
    event = std::move(impl_->events.front());
    impl_->events.pop_front();
    return true;
}
window_metrics window::metrics() const {
    window_metrics result;
    auto* w = impl_->handle;
    glfwGetFramebufferSize(w, &result.width, &result.height);
    glfwGetWindowSize(w, &result.logical_width, &result.logical_height);
    float x, y;
    glfwGetWindowContentScale(w, &x, &y);
    result.scale = std::max(x, float(result.width) / std::max(1, result.logical_width));
    result.focused = glfwGetWindowAttrib(w, GLFW_FOCUSED);
    result.minimized = glfwGetWindowAttrib(w, GLFW_ICONIFIED);
    auto* monitor = glfwGetWindowMonitor(w);
    if (!monitor)
        monitor = glfwGetPrimaryMonitor();
    if (monitor)
        if (const auto* mode = glfwGetVideoMode(monitor))
            result.refresh_rate = mode->refreshRate;
    return result;
}
key_state window::keys() const {
    key_state result;
    for (auto [native, key] : key_mapping)
        result[key] = glfwGetKey(impl_->handle, native) != GLFW_RELEASE;
    return result;
}
pointer_state window::pointer() const {
    double x, y;
    glfwGetCursorPos(impl_->handle, &x, &y);
    return {float(x), float(y), buttons(impl_->handle)};
}
bool window::captured() const {
    return impl_->relative;
}
bool window::capture(bool enabled) {
    glfwGetError(nullptr);
    glfwSetInputMode(impl_->handle, GLFW_CURSOR, enabled ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(impl_->handle, GLFW_RAW_MOUSE_MOTION, enabled);
    if (glfwGetError(nullptr) != GLFW_NO_ERROR)
        return false;
    impl_->relative = enabled;
    glfwGetCursorPos(impl_->handle, &impl_->last_x, &impl_->last_y);
    return true;
}
bool window::fullscreen(bool enabled) {
    auto& p = *impl_;
    if (enabled == p.fullscreen)
        return true;
    glfwGetError(nullptr);
    if (enabled) {
        glfwGetWindowPos(p.handle, &p.x, &p.y);
        glfwGetWindowSize(p.handle, &p.width, &p.height);
        auto* monitor = glfwGetPrimaryMonitor();
        const auto* mode = glfwGetVideoMode(monitor);
        if (!mode)
            return false;
        glfwSetWindowMonitor(p.handle, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else
        glfwSetWindowMonitor(p.handle, nullptr, p.x, p.y, p.width, p.height, GLFW_DONT_CARE);
    if (glfwGetError(nullptr) != GLFW_NO_ERROR)
        return false;
    p.fullscreen = enabled;
    return true;
}
void window::text_input(bool enabled) {
    impl_->typing = enabled;
}
void window::cursor(cursor_shape shape) {
    auto index = static_cast<unsigned>(shape);
    auto*& cursor = impl_->cursors.at(index);
    if (!cursor)
        cursor = glfwCreateStandardCursor(index == 1   ? GLFW_HAND_CURSOR
                                          : index == 2 ? GLFW_IBEAM_CURSOR
                                                       : GLFW_ARROW_CURSOR);
    glfwSetCursor(impl_->handle, cursor);
}
std::string window::error() const {
    const char* error{};
    glfwGetError(&error);
    return error ? error : "Window operation failed";
}
graphics_api window::graphics() const {
    return impl_->api;
}
void* native_surface::handle(window& w) {
#if defined(__linux__)
    return reinterpret_cast<void*>(static_cast<std::uintptr_t>(glfwGetX11Window(w.impl_->handle)));
#elif defined(_WIN32)
    return glfwGetWin32Window(w.impl_->handle);
#elif defined(__APPLE__)
    return w.impl_->layer;
#endif
}
void* native_surface::shared_context(window& w) {
    if (w.graphics() != graphics_api::opengl)
        return nullptr;
#if defined(__linux__)
    return glfwGetGLXContext(w.impl_->handle);
#elif defined(_WIN32)
    return glfwGetWGLContext(w.impl_->handle);
#else
    return nullptr;
#endif
}
void native_surface::release_context(window&) {}
}
