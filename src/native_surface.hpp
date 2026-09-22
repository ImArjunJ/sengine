#pragma once
#include "sengine/window.hpp"
namespace sengine {
struct native_surface {
    static void* handle(window&);
    static void* shared_context(window&);
    static void release_context(window&);
};
}
