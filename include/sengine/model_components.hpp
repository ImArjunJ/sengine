#pragma once
#include "animation.hpp"
#include "scene_document.hpp"
#include "world.hpp"

namespace sengine {
struct model_component {
    asset_reference source;
    mat4 offset;
    bool visible{true};
};
struct model_animation {
    std::string clip;
    double time{}, speed{1}, transition{.2};
    playback_mode mode{playback_mode::loop};
    bool paused{};
};
struct model_part {
    entity model;
    std::string node;
    std::int64_t index{-1};
    std::vector<float> weights;
    bool visible{true}, transform{};
};
class scene_registry;
void register_model_components(scene_registry&);
}
