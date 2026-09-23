#pragma once
#include "sengine/animation.hpp"
#include "sengine/model.hpp"
#include <nlohmann/json.hpp>

namespace sengine::gltf_detail {
struct node_pose {
    mat4 transform;
    transform_pose channels;
    std::vector<float> weights;
};
struct node_data {
    std::string name;
    std::optional<std::size_t> parent;
    transform_pose rest;
    node_pose pose;
    std::vector<std::string> morphs;
};
struct node_track {
    std::size_t node{};
    transform_track transform;
    std::vector<animation_curve<float>> weights;
};
struct clip_data {
    model_clip info;
    std::vector<node_track> tracks;
};
class gltf_source {
  public:
    explicit gltf_source(const std::filesystem::path&);
    void sample(std::size_t clip, double time, std::vector<node_pose>& result) const;
    std::vector<node_pose> rest() const;

  public:
    std::vector<std::uint8_t> bytes;
    std::vector<node_data> nodes;
    std::vector<clip_data> clips;

  private:
    std::vector<float> accessor(std::size_t index, unsigned width) const;
    void read_nodes();
    void read_animations();
    void pack();

  private:
    nlohmann::json document_;
    std::vector<std::vector<std::uint8_t>> buffers_;
    std::vector<std::uint8_t> binary_;
};
std::vector<std::uint8_t> read_file(const std::filesystem::path&);
std::filesystem::path resource_path(const std::filesystem::path& root, std::string_view uri);
void validate_transform(const mat4&);
transform_pose decompose(const mat4&);
}
