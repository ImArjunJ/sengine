#pragma once
#include "model_components.hpp"
#include "physics.hpp"
#include "scene.hpp"

namespace sengine {
namespace detail {
class scene_content;
}
class game_scene;
class model_instance;
struct surface_options {
    float4 color{1};
    float roughness{.6f}, metallic{};
};
class material {
  public:
    material() = default;
    bool alive() const noexcept;
    void color(float4);
    void roughness(float);
    void metallic(float);
    material clone() const;

  private:
    material(const std::shared_ptr<detail::scene_content>&, material_id);
    std::shared_ptr<detail::scene_content> require() const;
    friend class game_scene;
    friend class game_object;

  private:
    std::weak_ptr<detail::scene_content> owner_;
    material_id id_;
};
class geometry {
  public:
    geometry() = default;
    bool alive() const noexcept;

  private:
    geometry(const std::shared_ptr<detail::scene_content>&, mesh_id);
    friend class game_scene;

  private:
    std::weak_ptr<detail::scene_content> owner_;
    mesh_id id_;
};
class game_object {
  public:
    game_object() = default;
    bool alive() const noexcept;
    entity id() const;
    void destroy();
    std::string name() const;
    float3 position() const;
    void position(float3);
    mat4 transform() const;
    void transform(const mat4&);
    void parent(const game_object&, bool preserve_world = true);
    void visible(bool);
    void material(const sengine::material&);
    void material(std::string_view node, const sengine::material&, unsigned slot = 0);
    void play(std::string clip, double transition = .2, playback_mode = playback_mode::loop);
    void pause(bool = true);
    void reload();
    std::string animation() const;
    model_instance& model();
    void light(const light_options&);
    light_options light() const;
    void walk(float2 velocity, bool jump = false);
    void impulse(float3);

    template <class component, class... args> component& add(args&&... values) {
        return entities().emplace<component>(id_, std::forward<args>(values)...);
    }
    template <class component> component& add(component value) {
        return entities().emplace<component>(id_, std::move(value));
    }
    template <class component> component& get() {
        auto* value = entities().get<component>(id_);
        if (!value)
            throw std::out_of_range("Object has no requested component");
        return *value;
    }
    template <class component> const component& get() const {
        const auto* value = entities().get<component>(id_);
        if (!value)
            throw std::out_of_range("Object has no requested component");
        return *value;
    }
    template <class component> bool has() const { return entities().get<component>(id_) != nullptr; }
    template <class component> bool remove() { return entities().remove<component>(id_); }

  private:
    game_object(const std::shared_ptr<detail::scene_content>&, entity);
    std::shared_ptr<detail::scene_content> require() const;
    world& entities() const;
    friend class game_scene;

  private:
    std::weak_ptr<detail::scene_content> owner_;
    entity id_;
};
}
