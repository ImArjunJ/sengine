#pragma once
#include "math.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>
namespace sengine {
class renderer;
struct scene_node {
    std::uint32_t value{};

  public:
    explicit operator bool() const { return value != 0; }
};
struct scene_asset {
    std::size_t value{};
};
struct scene_instance {
    std::size_t value{};
};
struct material_id {
    std::size_t value{};
};
struct mesh_id {
    std::size_t value{};
};
struct scene_vertex {
    float3 position, normal, tangent;
    float2 uv;
    float4 color{1};
};
struct morph_target {
    std::vector<float3> positions, normals;
};
struct mesh_data {
    std::vector<scene_vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<morph_target> morphs;
    bounds volume;
    bool reverse_bitangent{};
};
enum class light_kind { directional, point, spot };
struct light_options {
    light_kind kind{light_kind::point};
    float3 color{1}, position{}, direction{0, -1, 0};
    float intensity{1000}, radius{10}, inner_cone{.3f}, outer_cone{.6f};
    bool shadows{true};
};
struct sun_options {
    float3 color{1};
    float intensity{10000};
    float3 direction{0, -1, 0};
    float angular_radius{.27f};
    unsigned shadow_size{2048}, cascades{3};
    std::array<float, 3> splits{.08f, .30f, .5f};
    float shadow_far{90}, shadow_hint{80};
    bool stable{true};
};
struct environment_options {
    float3 horizon{.65f, .72f, .80f}, zenith{.20f, .32f, .48f}, ground{.16f, .15f, .12f};
    float upper_curve{.45f}, lower_curve{.35f}, intensity{9000};
};
class scene {
  public:
    explicit scene(renderer&);
    ~scene();
    scene(const scene&) = delete;
    scene& operator=(const scene&) = delete;
    struct impl;

  private:
    std::unique_ptr<impl> impl_;
    friend impl& scene_data(scene&);
};
scene_node create_node(scene&, scene_node parent = {});
void set_parent(scene&, scene_node child, scene_node parent, bool preserve_world = true);
void destroy_node(scene&, scene_node);
scene_node add_light(scene&, const light_options&);
void update_light(scene&, scene_node, const light_options&);
scene_asset load_scene(scene&, const std::filesystem::path&, bool visible = true);
std::span<const scene_node> nodes(scene&, scene_asset);
std::string node_name(scene&, scene_asset, scene_node);
scene_node root(scene&, scene_asset);
scene_instance instantiate(scene&, scene_asset);
scene_node root(scene&, scene_instance);
void visible(scene&, scene_instance, bool);
void visible(scene&, scene_node, bool);
void visible(scene&, std::span<const scene_node>, bool);
std::vector<scene_node> renderable_descendants(scene&, scene_node);
bool renderable(scene&, scene_node);
mat4 local_transform(scene&, scene_node);
mat4 world_transform(scene&, scene_node);
scene_node parent(scene&, scene_node);
void set_transform(scene&, scene_node, const mat4&);
void begin_transforms(scene&);
void end_transforms(scene&);
bounds node_bounds(scene&, scene_node);
void cast_shadows(scene&, scene_node, bool);
void layers(scene&, scene_node, std::uint8_t);
material_id material_at(scene&, scene_node, unsigned slot = 0);
material_id load_material(scene&, const std::filesystem::path&);
material_id duplicate_material(scene&, material_id, const std::string& name = {});
void set_material(scene&, scene_node, material_id, unsigned slot = 0);
void set_parameter(scene&, material_id, const std::string&, float);
void set_color(scene&, material_id, const std::string&, float4, bool srgb = true);
mesh_id upload_mesh(scene&, const mesh_data&);
scene_node create_mesh(scene&, mesh_id, material_id, bool shadows = true);
void morph_weights(scene&, scene_node, std::span<const float>);
std::vector<std::string> animation_names(scene&, scene_asset);
float animation_duration(scene&, scene_asset, std::size_t);
void animate(scene&, scene_asset, std::size_t, float time);
void blend_animation(scene&, scene_asset, std::size_t previous, float time, float alpha);
void update_bones(scene&, scene_asset);
scene_node add_sun(scene&, const sun_options&);
void shadow_resolution(scene&, scene_node, unsigned);
void set_environment(scene&, const environment_options&);
}
