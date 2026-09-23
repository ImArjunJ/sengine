#include "sengine/geometry.hpp"
#include <algorithm>
#include <cmath>
namespace sengine {
namespace {
bool finite(point value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
}

std::optional<float> intersect(point o, point d, box box, float limit) {
    if (!finite(o) || !finite(d) || !finite(box.low) || !finite(box.high) || !std::isfinite(limit) ||
        limit < 0 || (d.x == 0 && d.y == 0 && d.z == 0) || box.low.x > box.high.x || box.low.y > box.high.y ||
        box.low.z > box.high.z)
        return {};
    float near = 0, far = limit;
    for (int i = 0; i < 3; ++i) {
        float origin = i == 0 ? o.x : i == 1 ? o.y : o.z, direction = i == 0 ? d.x : i == 1 ? d.y : d.z;
        float low = i == 0   ? box.low.x
                    : i == 1 ? box.low.y
                             : box.low.z,
              high = i == 0   ? box.high.x
                     : i == 1 ? box.high.y
                              : box.high.z;
        if (std::abs(direction) < 1e-7f) {
            if (origin < low || origin > high)
                return {};
            continue;
        }
        float a = (low - origin) / direction, b = (high - origin) / direction;
        if (a > b)
            std::swap(a, b);
        near = std::max(near, a);
        far = std::min(far, b);
        if (near > far)
            return {};
    }
    return near;
}
}
