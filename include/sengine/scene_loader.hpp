#pragma once
#include "scene_schema.hpp"

namespace sengine {
class scene_world {
  public:
    world& entities() noexcept { return world_; }
    const world& entities() const noexcept { return world_; }
    entity find(const std::string& id) const;
    void identify(entity, std::string id);
    const std::map<std::string, entity, std::less<>>& names() const noexcept { return names_; }

  private:
    world world_;
    std::map<std::string, entity, std::less<>> names_;
    std::map<entity, std::string> identities_;
};
struct scene_limits {
    std::size_t entities{100000}, prefab_depth{32}, files{1024};
};
class scene_loader {
  public:
    scene_loader(const scene_registry&, const asset_store&, scene_limits = {});
    std::unique_ptr<scene_world> load(const std::string& uri) const;
    std::unique_ptr<scene_world> build(const scene_document&) const;
    scene_document capture(const scene_world&) const;

  private:
    struct expansion;
    void expand(const scene_document&, const std::string& prefix, const std::string& parent,
                expansion&) const;
    void remap(scene_entity&, const std::string& prefix) const;
    std::unique_ptr<scene_world> assemble(std::vector<scene_entity>) const;

  private:
    const scene_registry& registry_;
    const asset_store& assets_;
    scene_limits limits_;
};
}
