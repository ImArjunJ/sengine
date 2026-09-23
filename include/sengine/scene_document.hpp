#pragma once
#include "math.hpp"
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace sengine {
class asset_reference {
  public:
    asset_reference() = default;
    explicit asset_reference(std::string uri) : uri_(std::move(uri)) {}
    const std::string& uri() const noexcept { return uri_; }
    bool operator==(const asset_reference&) const = default;

  private:
    std::string uri_;
};
using scene_value = std::variant<bool, std::int64_t, std::uint64_t, double, std::string, std::vector<double>>;
using scene_properties = std::map<std::string, scene_value, std::less<>>;
struct scene_component {
    unsigned version{1};
    scene_properties fields;
};
struct scene_entity {
    std::string id, name, parent;
    mat4 transform;
    std::map<std::string, scene_component, std::less<>> components;
};
struct scene_override {
    std::optional<std::string> name;
    std::optional<mat4> transform;
    std::map<std::string, scene_component, std::less<>> components;
    std::vector<std::string> remove_components;
};
struct prefab_instance {
    std::string id, parent;
    asset_reference source;
    mat4 transform;
    std::map<std::string, scene_override, std::less<>> overrides;
};
struct scene_document {
    unsigned version{1};
    std::vector<scene_entity> entities;
    std::vector<prefab_instance> instances;
};
scene_document parse_scene(std::string_view text, std::string_view source = "scene");
std::string write_scene(const scene_document&);
scene_document read_scene(const std::filesystem::path&);
void save_scene(const std::filesystem::path&, const scene_document&);
}
