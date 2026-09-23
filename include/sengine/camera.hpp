#pragma once
#include "geometry.hpp"

namespace sengine {
enum class projection_kind { perspective, orthographic };
struct camera_view {
    point eye{}, direction{0, 0, -1};
    float fov{65};
    point up{0, 1, 0};
    projection_kind projection{projection_kind::perspective};
    float vertical_size{10};
};
}
