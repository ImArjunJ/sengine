#include "sengine/scene_schema.hpp"

namespace sengine {
const detail::scene_codec& scene_registry::require(const std::string& name) const {
    const auto found = codecs_.find(name);
    if (found == codecs_.end())
        throw std::invalid_argument("Unregistered component: " + name);
    return *found->second;
}
namespace detail {
entity scene_read_context::reference(const std::string& name) const {
    if (name.empty())
        return {};
    const auto found = entities.find(name);
    if (found == entities.end())
        throw std::invalid_argument("Unknown entity reference: " + name);
    return found->second;
}
asset_reference scene_read_context::asset(const std::string& uri) const {
    if (uri.empty())
        return {};
    if (!std::filesystem::is_regular_file(assets.resolve(uri)))
        throw std::invalid_argument("Missing asset: " + uri);
    return asset_reference(uri);
}
std::string scene_write_context::reference(entity id) const {
    if (!id)
        return {};
    const auto found = names.find(id);
    if (found == names.end())
        throw std::invalid_argument("Entity reference has no persistent ID");
    return found->second;
}
double scene_number(const scene_value& value) {
    double result;
    if (const auto* number = std::get_if<double>(&value))
        result = *number;
    else if (const auto* number = std::get_if<std::int64_t>(&value))
        result = double(*number);
    else if (const auto* number = std::get_if<std::uint64_t>(&value))
        result = double(*number);
    else
        throw std::invalid_argument("Expected a number");
    if (!std::isfinite(result))
        throw std::invalid_argument("Expected a finite number");
    return result;
}
const std::string& scene_string(const scene_value& value) {
    const auto* text = std::get_if<std::string>(&value);
    if (!text)
        throw std::invalid_argument("Expected a string");
    return *text;
}
std::vector<double> scene_vector(const scene_value& value, std::size_t size) {
    const auto* values = std::get_if<std::vector<double>>(&value);
    if (!values || values->size() != size)
        throw std::invalid_argument("Expected " + std::to_string(size) + " numbers");
    for (double number : *values)
        if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max())
            throw std::out_of_range("Vector coordinate is out of range");
    return *values;
}
scene_value scene_encode(float2 value) {
    return std::vector<double>{value.x, value.y};
}
scene_value scene_encode(float3 value) {
    return std::vector<double>{value.x, value.y, value.z};
}
scene_value scene_encode(float4 value) {
    return std::vector<double>{value.x, value.y, value.z, value.w};
}
scene_value scene_encode(quaternion value) {
    return std::vector<double>{value.x, value.y, value.z, value.w};
}
scene_value scene_encode(const mat4& value) {
    std::vector<double> result;
    for (auto column : value.columns)
        result.insert(result.end(), {column.x, column.y, column.z, column.w});
    return result;
}
void scene_decode(const scene_value& value, float2& result) {
    const auto values = scene_vector(value, 2);
    result = {float(values[0]), float(values[1])};
}
void scene_decode(const scene_value& value, float3& result) {
    const auto values = scene_vector(value, 3);
    result = {float(values[0]), float(values[1]), float(values[2])};
}
void scene_decode(const scene_value& value, float4& result) {
    const auto values = scene_vector(value, 4);
    result = {float(values[0]), float(values[1]), float(values[2]), float(values[3])};
}
void scene_decode(const scene_value& value, quaternion& result) {
    const auto values = scene_vector(value, 4);
    result = {float(values[0]), float(values[1]), float(values[2]), float(values[3])};
}
void scene_decode(const scene_value& value, mat4& result) {
    const auto values = scene_vector(value, 16);
    for (std::size_t i = 0; i < 4; ++i)
        result[i] = {float(values[i * 4]), float(values[i * 4 + 1]), float(values[i * 4 + 2]),
                     float(values[i * 4 + 3])};
}
std::string scene_reference(const std::string& prefix, const std::string& reference) {
    if (reference.empty())
        return {};
    if (reference == "/")
        throw std::invalid_argument("Empty absolute entity reference");
    if (reference.front() == '/')
        return reference.substr(1);
    return prefix.empty() ? reference : prefix + '/' + reference;
}
}
}
