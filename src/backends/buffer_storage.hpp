#pragma once
#include <cstddef>
#include <vector>
namespace sengine::filament_detail {
template <class element> void release_vector(void*, std::size_t, void* storage) {
    delete static_cast<std::vector<element>*>(storage);
}
}
