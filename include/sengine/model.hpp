#pragma once
#include "scene.hpp"
#include <optional>
#include <string_view>

namespace sengine {
struct model_node {
    std::uint64_t owner{};
    std::uint32_t index{};

  public:
    explicit operator bool() const { return owner != 0; }
    auto operator<=>(const model_node&) const = default;
};
struct model_node_info {
    model_node id;
    std::string name;
    std::optional<model_node> parent;
    std::vector<std::string> morphs;
};
struct model_clip {
    std::string name;
    double duration{};
};
class model_instance;
class model {
  public:
    model(scene&, const std::filesystem::path&);
    std::span<const model_clip> clips() const;
    std::size_t instance_count() const;
    std::size_t capacity() const;

  private:
    struct impl;
    std::shared_ptr<impl> impl_;
    friend class model_instance;
};
class model_instance {
  public:
    explicit model_instance(const model&);
    ~model_instance();
    model_instance(const model_instance&) = delete;
    model_instance& operator=(const model_instance&) = delete;
    std::span<const model_node_info> nodes() const;
    std::span<const model_clip> clips() const;
    model_node node(std::size_t index) const;
    model_node find(std::string_view name) const;
    std::optional<model_node> parent(model_node) const;
    std::size_t clip(std::string_view name) const;
    void transform(const mat4&);
    void visible(bool);
    void show(model_node, bool);
    mat4 local_transform(model_node) const;
    mat4 world_transform(model_node) const;
    void local_transform(model_node, const mat4&);
    void world_transform(model_node, const mat4&);
    std::span<const float> morph_weights(model_node) const;
    void morph_weights(model_node, std::span<const float>);
    void reset_pose();
    void transition(double seconds);
    void animate(std::size_t clip, double time, double elapsed = 0);
    void synchronize();

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
