#include "sengine/window.hpp"
#include <chrono>
#include <cstdlib>
#include <thread>
#if defined(__linux__)
#include <unistd.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif
#include <stdexcept>
#include <vector>
namespace sengine {
std::filesystem::path user_data_directory(const std::string& application) {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    if (xdg)
        return std::filesystem::path(xdg) / application;
#ifdef __APPLE__
    if (home)
        return std::filesystem::path(home) / "Library/Application Support" / application;
#elif defined(_WIN32)
    if (const char* local = std::getenv("LOCALAPPDATA"))
        return std::filesystem::path(local) / application;
#else
    if (home)
        return std::filesystem::path(home) / ".local/share" / application;
#endif
    return std::filesystem::current_path() / application;
}
void sleep_for(double seconds) {
    if (seconds > 0)
        std::this_thread::sleep_for(std::chrono::duration<double>(seconds));
}
std::filesystem::path window::executable_directory() {
#if defined(__linux__)
    return std::filesystem::canonical("/proc/self/exe").parent_path();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size))
        throw std::runtime_error("Executable path unavailable");
    return std::filesystem::weakly_canonical(buffer.data()).parent_path();
#elif defined(_WIN32)
    std::vector<wchar_t> buffer(32768);
    auto size = GetModuleFileNameW(nullptr, buffer.data(), buffer.size());
    if (!size || size == buffer.size())
        throw std::runtime_error("Executable path unavailable");
    return std::filesystem::path(buffer.data()).parent_path();
#else
    throw std::runtime_error("Executable path unavailable on this platform");
#endif
}
}
