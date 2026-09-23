#include "sengine/model_components.hpp"
#include "sengine/scene_schema.hpp"

namespace sengine {
namespace {
void validate_model(const model_component& value) {
    if (value.source.uri().empty())
        throw std::invalid_argument("A model needs an asset source");
    for (auto column : value.offset.columns)
        if (!detail::finite(column))
            throw std::invalid_argument("Model offset must be finite");
    if (value.offset[0].w != 0 || value.offset[1].w != 0 || value.offset[2].w != 0 || value.offset[3].w != 1)
        throw std::invalid_argument("Model offset must be affine");
}
void validate_animation(const model_animation& value) {
    if (!std::isfinite(value.time) || !std::isfinite(value.speed) || !std::isfinite(value.transition) ||
        value.time < 0 || value.transition < 0 || unsigned(value.mode) > unsigned(playback_mode::ping_pong))
        throw std::invalid_argument("Invalid model animation state");
}
void validate_part(const model_part& value) {
    if (!value.model || value.index < -1 || (value.index == -1 && value.node.empty()))
        throw std::invalid_argument("A model part needs a model entity and node selector");
    for (auto weight : value.weights)
        if (!std::isfinite(weight))
            throw std::invalid_argument("Model morph weights must be finite");
}
}
void register_model_components(scene_registry& registry) {
    component_schema<model_component> model;
    model.field("source", &model_component::source)
        .field("offset", &model_component::offset, false)
        .field("visible", &model_component::visible, false)
        .validate(validate_model);
    registry.add("render.model", std::move(model));
    component_schema<model_animation> animation;
    animation.field("clip", &model_animation::clip)
        .field("time", &model_animation::time, false)
        .field("speed", &model_animation::speed, false)
        .field("transition", &model_animation::transition, false)
        .enumeration("mode", &model_animation::mode,
                     {{"once", playback_mode::once},
                      {"loop", playback_mode::loop},
                      {"ping_pong", playback_mode::ping_pong}},
                     false)
        .field("paused", &model_animation::paused, false)
        .validate(validate_animation);
    registry.add("render.animation", std::move(animation));
    component_schema<model_part> part;
    part.field("model", &model_part::model)
        .field("node", &model_part::node, false)
        .field("index", &model_part::index, false)
        .field("weights", &model_part::weights, false)
        .field("visible", &model_part::visible, false)
        .field("transform", &model_part::transform, false)
        .validate(validate_part);
    registry.add("render.part", std::move(part));
}
}
