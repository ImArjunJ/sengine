#include "backends/filament_scene_state.hpp"
#include "backends/filament_values.hpp"
#include <gltfio/Animator.h>
namespace sengine {
using namespace filament_detail;
namespace {
filament::gltfio::Animator* animator(scene& s, scene_asset a) {
    return scene_data(s).assets.at(a.value).asset->getInstance()->getAnimator();
}
}
std::vector<std::string> animation_names(scene& s, scene_asset a) {
    auto* anim = animator(s, a);
    std::vector<std::string> result;
    for (std::size_t i = 0; i < anim->getAnimationCount(); ++i)
        result.emplace_back(anim->getAnimationName(i));
    return result;
}
float animation_duration(scene& s, scene_asset a, std::size_t i) {
    return animator(s, a)->getAnimationDuration(i);
}
void animate(scene& s, scene_asset a, std::size_t i, float time) {
    animator(s, a)->applyAnimation(i, time);
}
void blend_animation(scene& s, scene_asset a, std::size_t i, float time, float alpha) {
    animator(s, a)->applyCrossFade(i, time, alpha);
}
void update_bones(scene& s, scene_asset a) {
    animator(s, a)->updateBoneMatrices();
}
}
