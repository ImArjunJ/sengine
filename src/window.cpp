#include "sengine/window.hpp"
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
#ifdef __APPLE__
    SDL_MetalView metal{};
#endif
    ~impl() {
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
window::window(const std::string& title, int width, int height) : impl_(std::make_unique<impl>()) {
#ifndef __APPLE__
    SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "x11", SDL_HINT_OVERRIDE);
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        throw std::runtime_error(SDL_GetError());
    auto& p = *impl_;
    p.initialized = true;
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __APPLE__
    flags |= SDL_WINDOW_METAL;
#else
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    flags |= SDL_WINDOW_OPENGL;
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
    p.context = SDL_GL_CreateContext(p.window);
    if (!p.context)
        throw std::runtime_error(SDL_GetError());
    if (!SDL_GL_MakeCurrent(p.window, nullptr))
        throw std::runtime_error(SDL_GetError());
    p.native = reinterpret_cast<void*>(static_cast<std::uintptr_t>(
        SDL_GetNumberProperty(SDL_GetWindowProperties(p.window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
#endif
    if (!p.native)
        throw std::runtime_error("Native graphics window unavailable");
}
window::~window() = default;
SDL_Window* window::handle() const {
    return impl_->window;
}
void* window::native() const {
    return impl_->native;
}
void* window::shared_context() const {
    return impl_->context;
}
void window::release_context() {
    if (impl_->context) {
        SDL_GL_DestroyContext(impl_->context);
        impl_->context = nullptr;
    }
}
std::filesystem::path window::executable_directory() {
    const char* base = SDL_GetBasePath();
    if (!base)
        throw std::runtime_error(SDL_GetError());
    return base;
}
}
