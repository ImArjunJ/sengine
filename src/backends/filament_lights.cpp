#include "backends/filament_scene_state.hpp"
#include "backends/filament_values.hpp"
#include <filament/LightManager.h>
#include <numbers>
namespace sengine {
namespace {
using namespace filament_detail;
void validate(const light_options& options) {
    for (auto vector : {options.color, options.position, options.direction})
        for (float value : {vector.x, vector.y, vector.z})
            if (!std::isfinite(value))
                throw std::invalid_argument("Non-finite light vector");
    if (unsigned(options.kind) > unsigned(light_kind::spot) || !std::isfinite(options.intensity) ||
        options.intensity < 0 || !std::isfinite(options.radius) || options.radius <= 0 ||
        !std::isfinite(options.inner_cone) || !std::isfinite(options.outer_cone) || options.inner_cone < 0 ||
        options.outer_cone <= 0 || options.inner_cone > options.outer_cone ||
        options.outer_cone >= std::numbers::pi_v<float> / 2 || length(options.direction) < .00001f)
        throw std::invalid_argument("Invalid light options");
}
filament::LightManager::Type native(light_kind kind) {
    using type = filament::LightManager::Type;
    switch (kind) {
    case light_kind::directional:
        return type::DIRECTIONAL;
    case light_kind::point:
        return type::POINT;
    case light_kind::spot:
        return type::SPOT;
    }
    throw std::invalid_argument("Invalid light type");
}
}
scene_node add_light(scene& scene, const light_options& options) {
    validate(options);
    auto& state = scene_data(scene);
    const auto node = create_node(scene);
    try {
        const auto status = filament::LightManager::Builder(native(options.kind))
                                .color(filament_detail::native(options.color))
                                .position(filament_detail::native(options.position))
                                .direction(filament_detail::native(options.direction))
                                .intensity(options.intensity)
                                .falloff(options.radius)
                                .spotLightCone(options.inner_cone, options.outer_cone)
                                .castShadows(options.shadows)
                                .build(state.engine, filament_detail::native(node));
        if (status != filament::LightManager::Builder::Success)
            throw std::runtime_error("Cannot create scene light");
        state.target.addEntity(filament_detail::native(node));
    } catch (...) {
        destroy_node(scene, node);
        throw;
    }
    return node;
}
void update_light(scene& scene, scene_node node, const light_options& options) {
    validate(options);
    auto& lights = scene_data(scene).engine.getLightManager();
    const auto instance = lights.getInstance(filament_detail::native(node));
    if (!instance || lights.getType(instance) != native(options.kind))
        throw std::invalid_argument("Light type does not match its node");
    lights.setColor(instance, filament_detail::native(options.color));
    lights.setPosition(instance, filament_detail::native(options.position));
    lights.setDirection(instance, filament_detail::native(options.direction));
    lights.setIntensity(instance, options.intensity);
    lights.setFalloff(instance, options.radius);
    lights.setSpotLightCone(instance, options.inner_cone, options.outer_cone);
    lights.setShadowCaster(instance, options.shadows);
}
}
