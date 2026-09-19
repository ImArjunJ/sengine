#pragma once
#include <optional>
namespace sengine {
struct point {
    float x{}, y{}, z{};
};
struct box {
    point low, high;
};
std::optional<float> intersect(point origin, point direction, box bounds, float limit);
}
