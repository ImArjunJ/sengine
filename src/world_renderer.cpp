#include "sengine/world_renderer.hpp"
#include <map>
#include <set>
#include <stdexcept>

namespace sengine {
namespace {
void validate_transform(const mat4& value) {
    for (auto column : value.columns)
        if (!detail::finite(column))
            throw std::invalid_argument("Model transform must be finite");
    if (value[0].w != 0 || value[1].w != 0 || value[2].w != 0 || value[3].w != 1)
        throw std::invalid_argument("Model transform must be affine");
}
class mesh_binding {
  public:
    mesh_binding(scene& target, mesh_id mesh, material_id material, mat4 offset)
        : scene_(target), node_(create_mesh(target, mesh, material)), offset_(offset) {}
    ~mesh_binding() { destroy_node(scene_, node_); }
    mesh_binding(const mesh_binding&) = delete;
    mesh_binding& operator=(const mesh_binding&) = delete;
    void show(bool enabled) { shown_ = enabled; }
    void offset(const mat4& value) { offset_ = value; }
    void synchronize(const mat4& transform) {
        const auto next = transform * offset_;
        if (!initialized_ || next != transform_) {
            set_transform(scene_, node_, next);
            transform_ = next;
        }
        if (!initialized_ || visible_ != shown_) {
            visible(scene_, node_, shown_);
            visible_ = shown_;
        }
        initialized_ = true;
    }

  private:
    scene& scene_;
    scene_node node_;
    mat4 offset_, transform_;
    bool initialized_{}, visible_{}, shown_{true};
};
class model_binding {
  public:
    model_binding(std::shared_ptr<sengine::model> source, std::string uri)
        : source_(std::move(source)), instance(*source_), uri(std::move(uri)) {}
    void prepare(const model_animation* animation) const {
        if (!animation)
            return;
        if (!animation->clip.empty())
            instance.clip(animation->clip);
        if (!std::isfinite(animation->time) || animation->time < 0 || !std::isfinite(animation->speed) ||
            !std::isfinite(animation->transition) || animation->transition < 0 ||
            unsigned(animation->mode) > unsigned(playback_mode::ping_pong))
            throw std::invalid_argument("Invalid model animation state");
    }
    double position(const model_animation& animation, double seconds) const {
        const double duration = instance.clips()[instance.clip(animation.clip)].duration;
        double next = animation.time + (animation.paused ? 0 : seconds * animation.speed);
        if (!std::isfinite(next))
            throw std::invalid_argument("Animation position overflow");
        if (duration == 0)
            return 0;
        if (animation.mode == playback_mode::once)
            return std::clamp(next, 0., duration);
        const double period = duration * (animation.mode == playback_mode::ping_pong ? 2 : 1);
        next = std::fmod(next, period);
        return next < 0 ? next + period : next;
    }
    void sample(model_animation* animation, double seconds) {
        for (auto node : hidden)
            instance.show(node, true);
        hidden.clear();
        if (!animation || animation->clip.empty()) {
            instance.reset_pose();
            active.clear();
            return;
        }
        const auto index = instance.clip(animation->clip);
        const double duration = instance.clips()[index].duration;
        const double next = position(*animation, seconds);
        const double time =
            animation->mode == playback_mode::ping_pong && next > duration ? 2 * duration - next : next;
        if (active != animation->clip) {
            instance.transition(initialized ? animation->transition : 0);
            active = animation->clip;
        }
        instance.animate(index, time, animation->paused ? 0 : seconds);
        animation->time = next;
        initialized = true;
    }

  private:
    std::shared_ptr<sengine::model> source_;

  public:
    model_instance instance;
    std::string uri, active;
    std::vector<model_node> hidden;
    bool initialized{};
};
class transform_scope {
  public:
    explicit transform_scope(scene& target) : scene_(target) { begin_transforms(scene_); }
    ~transform_scope() { end_transforms(scene_); }
    transform_scope(const transform_scope&) = delete;
    transform_scope& operator=(const transform_scope&) = delete;

  private:
    scene& scene_;
};
}
struct world_renderer::impl {
  public:
    impl(world& entities, scene& resources, const asset_store* assets = nullptr)
        : entities(entities), resources(resources), assets(assets) {}
    mesh_binding& require(entity id) {
        if (!entities.alive(id))
            throw std::invalid_argument("Cannot render a stale or foreign entity");
        const auto found = bindings.find(id);
        if (found == bindings.end())
            throw std::out_of_range("Entity has no mesh binding");
        return found->second;
    }
    model_binding& require_model(entity id) {
        if (!entities.alive(id))
            throw std::invalid_argument("Cannot render a stale or foreign entity");
        const auto found = models.find(id);
        if (found == models.end())
            throw std::out_of_range("Entity has no model binding");
        return *found->second;
    }
    std::shared_ptr<sengine::model> load(const std::string& uri) {
        if (!assets)
            throw std::logic_error("Model components need an asset store");
        if (const auto found = cache.find(uri); found != cache.end())
            return found->second;
        auto result = std::make_shared<sengine::model>(resources, assets->resolve(uri));
        cache.emplace(uri, result);
        return result;
    }
    void collect() {
        for (auto it = cache.begin(); it != cache.end();)
            if (it->second.use_count() == 1)
                it = cache.erase(it);
            else
                ++it;
    }
    void apply_part(model_binding& binding, model_node node, entity id) {
        const auto& part = *entities.get<model_part>(id);
        if (part.transform)
            binding.instance.world_transform(node, entities.world_transform(id));
        if (!part.weights.empty())
            binding.instance.morph_weights(node, part.weights);
        binding.instance.show(node, part.visible);
        if (!part.visible)
            binding.hidden.push_back(node);
    }
    void synchronize(double seconds, const std::string* reloaded = nullptr) {
        if (!std::isfinite(seconds) || seconds < 0)
            throw std::invalid_argument("Invalid renderer animation delta");
        std::map<entity, std::unique_ptr<model_binding>> replacements;
        std::shared_ptr<sengine::model> fresh;
        std::map<entity, model_animation> playback;
        if (reloaded) {
            if (!assets)
                throw std::logic_error("Model reload needs an asset store");
            fresh = std::make_shared<sengine::model>(resources, assets->resolve(*reloaded));
        }
        const auto living = entities.entities();
        for (auto id : living) {
            const auto* settings = entities.get<model_component>(id);
            if (!settings)
                continue;
            if (bindings.contains(id))
                throw std::invalid_argument("An entity cannot have both a mesh and model binding");
            const auto found = models.find(id);
            const auto& uri = settings->source.uri();
            const auto transform = entities.world_transform(id) * settings->offset;
            validate_transform(transform);
            const bool replace =
                found == models.end() || found->second->uri != uri || (reloaded && *reloaded == uri);
            if (replace)
                replacements.emplace(id, std::make_unique<model_binding>(
                                             reloaded && *reloaded == uri ? fresh : load(uri), uri));
            auto& binding = replace ? *replacements.at(id) : *found->second;
            binding.prepare(entities.get<model_animation>(id));
            if (const auto* animation = entities.get<model_animation>(id);
                animation && !animation->clip.empty())
                binding.position(*animation, seconds);
            if (replace) {
                auto* animation = entities.get<model_animation>(id);
                binding.sample(animation ? &playback.emplace(id, *animation).first->second : nullptr,
                               seconds);
                binding.instance.transform(transform);
            }
        }
        struct part_binding {
            entity id, model;
            model_node node;
            std::size_t depth;
        };
        std::vector<part_binding> parts;
        std::set<std::pair<entity, std::uint32_t>> selected;
        for (auto id : living) {
            const auto* part = entities.get<model_part>(id);
            if (!part)
                continue;
            if (!entities.alive(part->model) || !entities.get<model_component>(part->model))
                continue;
            if (part->index < -1)
                throw std::invalid_argument("Invalid model part node");
            auto& binding = replacements.contains(part->model) ? *replacements.at(part->model)
                                                               : require_model(part->model);
            const auto node = part->index < 0 ? binding.instance.find(part->node)
                                              : binding.instance.node(std::size_t(part->index));
            if (!selected.emplace(part->model, node.index).second)
                throw std::invalid_argument("Multiple entities control the same model node");
            if (!part->weights.empty() && part->weights.size() != binding.instance.morph_weights(node).size())
                throw std::invalid_argument("Wrong number of model part morph weights");
            for (float weight : part->weights)
                if (!std::isfinite(weight))
                    throw std::invalid_argument("Model part morph weights must be finite");
            std::size_t depth{};
            for (auto parent = binding.instance.parent(node); parent;
                 parent = binding.instance.parent(*parent))
                ++depth;
            if (part->transform)
                validate_transform(entities.world_transform(id));
            parts.push_back({id, part->model, node, depth});
        }
        std::ranges::sort(parts, {}, &part_binding::depth);
        for (const auto& part : parts)
            if (const auto found = replacements.find(part.model); found != replacements.end())
                apply_part(*found->second, part.node, part.id);
        for (auto& [id, binding] : replacements)
            binding->instance.synchronize();
        for (auto it = models.begin(); it != models.end();)
            if (!entities.get<model_component>(it->first))
                it = models.erase(it);
            else
                ++it;
        for (auto& [id, binding] : replacements)
            models.insert_or_assign(id, std::move(binding));
        for (const auto& [id, animation] : playback)
            entities.get<model_animation>(id)->time = animation.time;
        if (reloaded)
            cache.insert_or_assign(*reloaded, std::move(fresh));
        {
            const transform_scope scope(resources);
            for (auto it = bindings.begin(); it != bindings.end();) {
                if (!entities.alive(it->first))
                    it = bindings.erase(it);
                else {
                    it->second.synchronize(entities.world_transform(it->first));
                    ++it;
                }
            }
        }
        for (auto& [id, binding] : models) {
            const auto& settings = *entities.get<model_component>(id);
            if (!replacements.contains(id))
                binding->sample(entities.get<model_animation>(id), seconds);
            binding->instance.transform(entities.world_transform(id) * settings.offset);
            binding->instance.visible(settings.visible);
        }
        for (const auto& part : parts)
            if (!replacements.contains(part.model))
                apply_part(require_model(part.model), part.node, part.id);
        for (auto& [id, binding] : models)
            binding->instance.synchronize();
        collect();
    }

  public:
    world& entities;
    scene& resources;
    const asset_store* assets{};
    std::map<entity, mesh_binding> bindings;
    std::map<std::string, std::shared_ptr<sengine::model>, std::less<>> cache;
    std::map<entity, std::unique_ptr<model_binding>> models;
};
world_renderer::world_renderer(world& entities, scene& resources)
    : impl_(std::make_unique<impl>(entities, resources)) {}
world_renderer::world_renderer(world& entities, scene& resources, const asset_store& assets)
    : impl_(std::make_unique<impl>(entities, resources, &assets)) {
    synchronize();
}
world_renderer::~world_renderer() = default;
void world_renderer::mesh(entity id, mesh_id mesh, material_id material, mat4 offset) {
    if (!impl_->entities.alive(id))
        throw std::invalid_argument("Cannot render a stale or foreign entity");
    if (impl_->entities.get<model_component>(id))
        throw std::invalid_argument("Entity already has a model component");
    if (!impl_->bindings.try_emplace(id, impl_->resources, mesh, material, offset).second)
        throw std::invalid_argument("Entity already has a mesh binding");
}
bool world_renderer::remove(entity id) {
    bool removed = impl_->bindings.erase(id) != 0;
    removed = impl_->entities.remove<model_component>(id) || removed;
    removed = impl_->models.erase(id) != 0 || removed;
    impl_->collect();
    return removed;
}
void world_renderer::show(entity id, bool visible) {
    if (auto* component = impl_->entities.get<model_component>(id))
        component->visible = visible;
    else
        impl_->require(id).show(visible);
}
void world_renderer::offset(entity id, const mat4& offset) {
    if (auto* component = impl_->entities.get<model_component>(id))
        component->offset = offset;
    else
        impl_->require(id).offset(offset);
}
void world_renderer::synchronize() {
    advance(0);
}
void world_renderer::advance(double seconds) {
    try {
        impl_->synchronize(seconds);
    } catch (...) {
        impl_->collect();
        throw;
    }
}
void world_renderer::play(entity id, std::string clip, double transition, playback_mode mode) {
    auto& binding = impl_->require_model(id);
    binding.instance.clip(clip);
    if (!std::isfinite(transition) || transition < 0 || unsigned(mode) > unsigned(playback_mode::ping_pong))
        throw std::invalid_argument("Invalid animation playback options");
    auto* animation = impl_->entities.get<model_animation>(id);
    if (!animation)
        animation = &impl_->entities.emplace<model_animation>(id);
    binding.instance.transition(transition);
    binding.active = clip;
    animation->clip = std::move(clip);
    animation->time = 0;
    animation->transition = transition;
    animation->mode = mode;
    animation->paused = false;
}
model_instance& world_renderer::model(entity id) {
    return impl_->require_model(id).instance;
}
void world_renderer::reload(const std::string& uri) {
    try {
        impl_->synchronize(0, &uri);
    } catch (...) {
        impl_->collect();
        throw;
    }
}
std::size_t world_renderer::size() const noexcept {
    return impl_->bindings.size() + impl_->models.size();
}
}
