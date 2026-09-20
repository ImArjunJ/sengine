#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <string>

namespace sengine {
class window {
  public:
    window(const std::string& title, int width, int height);
    ~window();
    window(const window&) = delete;
    window& operator=(const window&) = delete;
    SDL_Window* handle() const;
    void* native() const;
    void* shared_context() const;
    void release_context();
    static std::filesystem::path executable_directory();

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
