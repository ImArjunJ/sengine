#include "gltf_source.hpp"
#include <bit>
#include <charconv>
#include <fstream>
#include <map>
#include <set>

namespace sengine::gltf_detail {
namespace {
using json = nlohmann::json;
constexpr std::size_t maximum_bytes = 512 * 1024 * 1024;
std::uint32_t word(std::span<const std::uint8_t> data, std::size_t offset, unsigned count = 4) {
    if (offset > data.size() || count > data.size() - offset)
        throw std::invalid_argument("Truncated glTF buffer");
    std::uint32_t result{};
    for (unsigned i = 0; i < count; ++i)
        result |= std::uint32_t(data[offset + i]) << (i * 8);
    return result;
}
void append(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.push_back(std::uint8_t(value >> (i * 8)));
}
std::vector<std::uint8_t> base64(std::string_view text) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<std::uint8_t> result;
    unsigned bits{}, count{};
    unsigned padding{};
    if (text.size() % 4)
        throw std::invalid_argument("Invalid glTF base64 length");
    for (char c : text) {
        if (c == '=') {
            ++padding;
            continue;
        }
        const auto digit = alphabet.find(c);
        if (padding || digit == std::string_view::npos)
            throw std::invalid_argument("Invalid glTF base64 buffer");
        bits = (bits << 6) | unsigned(digit);
        count += 6;
        if (count >= 8) {
            count -= 8;
            result.push_back(std::uint8_t(bits >> count));
        }
    }
    if (padding > 2 || padding != (3 - result.size() % 3) % 3 || (bits & ((1u << count) - 1)))
        throw std::invalid_argument("Invalid glTF base64 padding");
    return result;
}
std::vector<float> numbers(const json& source, std::size_t count) {
    if (!source.is_array() || source.size() != count)
        throw std::invalid_argument("Invalid glTF vector size");
    auto result = source.get<std::vector<float>>();
    for (float value : result)
        if (!std::isfinite(value))
            throw std::invalid_argument("Nonfinite glTF value");
    return result;
}
float3 vector3(const json& source) {
    const auto data = numbers(source, 3);
    return {data[0], data[1], data[2]};
}
quaternion rotation4(const json& source) {
    const auto data = numbers(source, 4);
    quaternion value{data[0], data[1], data[2], data[3]};
    if (!detail::valid_key(value))
        throw std::invalid_argument("Invalid glTF rotation");
    return value;
}
interpolation interpolation_of(const std::string& name) {
    if (name == "LINEAR")
        return interpolation::linear;
    if (name == "STEP")
        return interpolation::step;
    if (name == "CUBICSPLINE")
        return interpolation::cubic;
    throw std::invalid_argument("Unknown glTF interpolation");
}
template <class value> value unpack(std::span<const float> data);
template <> float unpack(std::span<const float> data) {
    return data[0];
}
template <> float3 unpack(std::span<const float> data) {
    return {data[0], data[1], data[2]};
}
template <> quaternion unpack(std::span<const float> data) {
    return {data[0], data[1], data[2], data[3]};
}
template <class value>
animation_curve<value> curve(const std::vector<float>& times, const std::vector<float>& values,
                             unsigned width, interpolation mode, unsigned stride = 0, unsigned offset = 0) {
    const unsigned parts = mode == interpolation::cubic ? 3 : 1;
    if (!stride)
        stride = width;
    if (times.empty() || values.size() != times.size() * stride * parts)
        throw std::invalid_argument("Invalid glTF animation sample count");
    std::vector<keyframe<value>> keys;
    for (std::size_t i = 0; i < times.size(); ++i) {
        const auto start = i * stride * parts + offset;
        keyframe<value> key{.time = times[i], .mode = mode};
        key.value = unpack<value>(std::span(values).subspan(start + (parts == 3 ? stride : 0), width));
        if (parts == 3) {
            key.incoming = unpack<value>(std::span(values).subspan(start, width));
            key.outgoing = unpack<value>(std::span(values).subspan(start + 2 * stride, width));
        }
        keys.push_back(key);
    }
    return animation_curve<value>(std::move(keys));
}
}
std::vector<std::uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream || stream.tellg() < 0 || std::uint64_t(stream.tellg()) > maximum_bytes)
        throw std::runtime_error("Cannot read model resource: " + path.string());
    std::vector<std::uint8_t> result(std::size_t(stream.tellg()));
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(result.data()), std::streamsize(result.size())))
        throw std::runtime_error("Truncated model resource: " + path.string());
    return result;
}
std::filesystem::path resource_path(const std::filesystem::path& root, std::string_view uri) {
    std::string decoded;
    for (std::size_t i = 0; i < uri.size(); ++i) {
        if (uri[i] != '%') {
            decoded += uri[i];
            continue;
        }
        unsigned value{};
        if (i + 2 >= uri.size())
            throw std::invalid_argument("Invalid resource URI");
        const auto result = std::from_chars(uri.data() + i + 1, uri.data() + i + 3, value, 16);
        if (result.ec != std::errc{} || result.ptr != uri.data() + i + 3 || value == 0)
            throw std::invalid_argument("Invalid resource URI");
        decoded += char(value);
        i += 2;
    }
    if (decoded.find(':') != std::string::npos || decoded.find('\0') != std::string::npos)
        throw std::invalid_argument("Model resources must use local URIs");
    return root / decoded;
}
void validate_transform(const mat4& value) {
    for (auto column : value.columns)
        if (!detail::finite(column))
            throw std::invalid_argument("Model transforms must be finite");
    if (value[0].w != 0 || value[1].w != 0 || value[2].w != 0 || value[3].w != 1)
        throw std::invalid_argument("Model transforms must be affine");
}
transform_pose decompose(const mat4& value) {
    validate_transform(value);
    float3 scale{length(value[0].xyz()), length(value[1].xyz()), length(value[2].xyz())};
    if (scale.x < 1e-8f || scale.y < 1e-8f || scale.z < 1e-8f)
        return {value[3].xyz(), {}, scale};
    if (dot(cross(value[0].xyz(), value[1].xyz()), value[2].xyz()) < 0)
        scale.x = -scale.x;
    mat4 orientation{{float4{value[0].xyz() / scale.x, 0}, float4{value[1].xyz() / scale.y, 0},
                      float4{value[2].xyz() / scale.z, 0}, float4{0, 0, 0, 1}}};
    return {value[3].xyz(), rotation_of(orientation), scale};
}
gltf_source::gltf_source(const std::filesystem::path& path) {
    const auto input = read_file(path);
    if (input.size() >= 4 && word(input, 0) == 0x46546c67) {
        if (word(input, 4) != 2 || word(input, 8) != input.size())
            throw std::invalid_argument("Invalid GLB header");
        for (std::size_t offset = 12; offset < input.size();) {
            const auto size = word(input, offset), type = word(input, offset + 4);
            offset += 8;
            if (size > input.size() - offset || size % 4)
                throw std::invalid_argument("Invalid GLB chunk");
            if (type == 0x4e4f534a) {
                if (!document_.is_null())
                    throw std::invalid_argument("Duplicate GLB JSON chunk");
                document_ = json::parse(input.begin() + offset, input.begin() + offset + size);
            } else if (type == 0x004e4942) {
                if (!binary_.empty())
                    throw std::invalid_argument("Duplicate GLB binary chunk");
                binary_.assign(input.begin() + offset, input.begin() + offset + size);
            }
            offset += size;
        }
    } else
        document_ = json::parse(input);
    if (document_.at("asset").at("version") != "2.0")
        throw std::invalid_argument("Models require glTF 2.0");
    std::size_t total{};
    for (const auto& buffer : document_.value("buffers", json::array())) {
        auto uri = buffer.value("uri", std::string{});
        std::vector<std::uint8_t> data;
        if (uri.empty()) {
            if (!buffers_.empty())
                throw std::invalid_argument("Only the first buffer can use a GLB chunk");
            data = binary_;
        } else if (uri.starts_with("data:")) {
            const auto comma = uri.find(',');
            if (comma == std::string::npos || !std::string_view(uri).substr(0, comma).ends_with(";base64"))
                throw std::invalid_argument("Unsupported glTF data URI");
            data = base64(std::string_view(uri).substr(comma + 1));
        } else
            data = read_file(resource_path(path.parent_path(), uri));
        const auto length = buffer.at("byteLength").get<std::size_t>();
        if (data.size() < length || length > maximum_bytes - total)
            throw std::invalid_argument("Invalid glTF buffer length");
        data.resize(length);
        total += length;
        buffers_.push_back(std::move(data));
    }
    read_nodes();
    read_animations();
    pack();
    document_ = {};
    buffers_.clear();
    binary_.clear();
}
std::vector<float> gltf_source::accessor(std::size_t index, unsigned width) const {
    const auto& source = document_.at("accessors").at(index);
    const auto type = source.at("type").get<std::string>();
    const unsigned actual = type == "SCALAR" ? 1 : type == "VEC3" ? 3 : type == "VEC4" ? 4 : 0;
    const auto count = source.at("count").get<std::size_t>();
    if (source.at("componentType") != 5126 || actual != width || count > maximum_bytes / (width * 4))
        throw std::invalid_argument("Invalid glTF animation accessor");
    std::vector<float> result(count * width);
    if (source.contains("bufferView")) {
        const auto& view = document_.at("bufferViews").at(source.at("bufferView").get<std::size_t>());
        if (view.value("extensions", json::object()).contains("EXT_meshopt_compression"))
            throw std::invalid_argument("Compressed animation accessors are not supported");
        const auto& buffer = buffers_.at(view.at("buffer").get<std::size_t>());
        const auto offset = view.value("byteOffset", std::size_t{}),
                   size = view.at("byteLength").get<std::size_t>();
        const auto start = source.value("byteOffset", std::size_t{}),
                   stride = view.value("byteStride", std::size_t(width * 4));
        if (offset > buffer.size() || size > buffer.size() - offset || start > size || stride < width * 4 ||
            (count && (size - start < width * 4 || count - 1 > (size - start - width * 4) / stride)))
            throw std::invalid_argument("Animation accessor exceeds its buffer view");
        for (std::size_t i = 0; i < count; ++i)
            for (unsigned c = 0; c < width; ++c)
                result[i * width + c] =
                    std::bit_cast<float>(word(buffer, offset + start + i * stride + c * 4));
    }
    if (source.contains("sparse")) {
        const auto& sparse = source.at("sparse");
        const auto size = sparse.at("count").get<std::size_t>();
        const auto &indices = sparse.at("indices"), &values = sparse.at("values");
        const auto& iv = document_.at("bufferViews").at(indices.at("bufferView").get<std::size_t>());
        const auto& vv = document_.at("bufferViews").at(values.at("bufferView").get<std::size_t>());
        const auto component = indices.at("componentType").get<unsigned>();
        const unsigned bytes = component == 5121 ? 1 : component == 5123 ? 2 : component == 5125 ? 4 : 0;
        const auto io = indices.value("byteOffset", std::size_t{}),
                   vo = values.value("byteOffset", std::size_t{});
        const auto il = iv.at("byteLength").get<std::size_t>(), vl = vv.at("byteLength").get<std::size_t>();
        if (!bytes || size > count || io > il || vo > vl || size > (il - io) / bytes ||
            size > (vl - vo) / (width * 4))
            throw std::invalid_argument("Invalid sparse animation accessor");
        const auto &ib = buffers_.at(iv.at("buffer").get<std::size_t>()),
                   &vb = buffers_.at(vv.at("buffer").get<std::size_t>());
        const auto ibase = iv.value("byteOffset", std::size_t{}),
                   vbase = vv.value("byteOffset", std::size_t{});
        if (ibase > ib.size() || il > ib.size() - ibase || vbase > vb.size() || vl > vb.size() - vbase)
            throw std::invalid_argument("Sparse accessor exceeds its buffer");
        std::size_t previous{};
        for (std::size_t i = 0; i < size; ++i) {
            const auto target = word(ib, ibase + io + i * bytes, bytes);
            if (target >= count || (i && target <= previous))
                throw std::invalid_argument("Invalid sparse indices");
            previous = target;
            for (unsigned c = 0; c < width; ++c)
                result[target * width + c] = std::bit_cast<float>(word(vb, vbase + vo + (i * width + c) * 4));
        }
    }
    for (float value : result)
        if (!std::isfinite(value))
            throw std::invalid_argument("Nonfinite animation sample");
    return result;
}
void gltf_source::read_nodes() {
    auto& definitions = document_["nodes"];
    if (definitions.is_null())
        definitions = json::array();
    if (!definitions.is_array() || definitions.size() > 100000)
        throw std::invalid_argument("Invalid model node count");
    nodes.resize(definitions.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        auto& source = definitions[i];
        auto& target = nodes[i];
        target.name = source.value("name", std::string{});
        if (source.contains("translation"))
            target.rest.position = vector3(source.at("translation"));
        if (source.contains("scale"))
            target.rest.scale = vector3(source.at("scale"));
        if (source.contains("rotation"))
            target.rest.orientation = rotation4(source.at("rotation"));
        target.pose.channels = target.rest;
        target.pose.transform = target.rest.matrix();
        if (source.contains("matrix")) {
            const auto matrix = numbers(source.at("matrix"), 16);
            for (unsigned column = 0; column < 4; ++column)
                target.pose.transform[column] = {matrix[column * 4], matrix[column * 4 + 1],
                                                 matrix[column * 4 + 2], matrix[column * 4 + 3]};
            validate_transform(target.pose.transform);
            target.pose.channels = decompose(target.pose.transform);
        }
        if (source.contains("mesh")) {
            const auto& mesh = document_.at("meshes").at(source.at("mesh").get<std::size_t>());
            const auto& primitives = mesh.at("primitives");
            const auto count = primitives.empty() ? 0 : primitives[0].value("targets", json::array()).size();
            if (count > 256)
                throw std::invalid_argument("Too many morph targets");
            for (const auto& primitive : primitives)
                if (primitive.value("targets", json::array()).size() != count)
                    throw std::invalid_argument("Inconsistent morph target counts");
            target.pose.weights.resize(count);
            if (mesh.contains("weights"))
                target.pose.weights = numbers(mesh.at("weights"), count);
            if (source.contains("weights"))
                target.pose.weights = numbers(source.at("weights"), count);
            target.morphs.resize(count);
            if (mesh.contains("extras") && mesh.at("extras").is_object() &&
                mesh.at("extras").contains("targetNames")) {
                const auto names = mesh.at("extras").at("targetNames").get<std::vector<std::string>>();
                if (names.size() != count)
                    throw std::invalid_argument("Invalid morph target names");
                target.morphs = names;
            }
        }
        for (auto child : source.value("children", std::vector<std::size_t>{})) {
            if (child >= nodes.size() || child == i || nodes[child].parent)
                throw std::invalid_argument("Invalid glTF hierarchy");
            nodes[child].parent = i;
        }
        if (!source.contains("extras") || !source["extras"].is_object())
            source["extras"] = json::object();
        source["extras"]["sengine_node"] = i;
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        std::size_t depth{};
        for (auto parent = nodes[i].parent; parent; parent = nodes[*parent].parent)
            if (++depth > 256)
                throw std::invalid_argument("Model hierarchy is cyclic or too deep");
    }
}
void gltf_source::read_animations() {
    for (const auto& animation : document_.value("animations", json::array())) {
        clip_data clip;
        clip.info.name = animation.value("name", std::string{});
        std::map<std::size_t, node_track> tracks;
        std::set<std::pair<std::size_t, std::string>> paths;
        for (const auto& channel : animation.at("channels")) {
            const auto& target = channel.at("target");
            if (!target.contains("node"))
                continue;
            const auto index = target.at("node").get<std::size_t>();
            const auto path = target.at("path").get<std::string>();
            if (index >= nodes.size() || document_.at("nodes")[index].contains("matrix") ||
                !paths.emplace(index, path).second)
                throw std::invalid_argument("Invalid animation target");
            const auto& sampler = animation.at("samplers").at(channel.at("sampler").get<std::size_t>());
            const auto times = accessor(sampler.at("input").get<std::size_t>(), 1);
            const auto output = sampler.at("output").get<std::size_t>();
            const auto mode = interpolation_of(sampler.value("interpolation", std::string("LINEAR")));
            auto& track = tracks[index];
            track.node = index;
            if (path == "translation")
                track.transform.position = curve<float3>(times, accessor(output, 3), 3, mode);
            else if (path == "rotation")
                track.transform.orientation = curve<quaternion>(times, accessor(output, 4), 4, mode);
            else if (path == "scale")
                track.transform.scale = curve<float3>(times, accessor(output, 3), 3, mode);
            else if (path == "weights") {
                const auto values = accessor(output, 1);
                const unsigned count = unsigned(nodes[index].pose.weights.size());
                if (!count)
                    throw std::invalid_argument("Animation targets a node without morphs");
                for (unsigned i = 0; i < count; ++i)
                    track.weights.push_back(curve<float>(times, values, 1, mode, count, i));
            } else
                throw std::invalid_argument("Unsupported animation channel");
            clip.info.duration = std::max(clip.info.duration, double(times.back()));
        }
        for (auto& [index, track] : tracks)
            clip.tracks.push_back(std::move(track));
        clips.push_back(std::move(clip));
    }
}
void gltf_source::pack() {
    auto text = document_.dump();
    while (text.size() % 4)
        text += ' ';
    const auto size = 12 + 8 + text.size() + (binary_.empty() ? 0 : 8 + binary_.size());
    if (size > maximum_bytes)
        throw std::invalid_argument("Model exceeds size limit");
    append(bytes, 0x46546c67);
    append(bytes, 2);
    append(bytes, std::uint32_t(size));
    append(bytes, std::uint32_t(text.size()));
    append(bytes, 0x4e4f534a);
    bytes.insert(bytes.end(), text.begin(), text.end());
    if (!binary_.empty()) {
        append(bytes, std::uint32_t(binary_.size()));
        append(bytes, 0x004e4942);
        bytes.insert(bytes.end(), binary_.begin(), binary_.end());
    }
}
std::vector<node_pose> gltf_source::rest() const {
    std::vector<node_pose> result;
    result.reserve(nodes.size());
    for (const auto& node : nodes)
        result.push_back(node.pose);
    return result;
}
void gltf_source::sample(std::size_t index, double time, std::vector<node_pose>& result) const {
    if (!std::isfinite(time) || time < 0)
        throw std::invalid_argument("Invalid model animation time");
    const auto& clip = clips.at(index);
    result.resize(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i)
        result[i] = nodes[i].pose;
    for (const auto& track : clip.tracks) {
        result[track.node].channels = track.transform.sample(time, nodes[track.node].rest);
        result[track.node].transform = result[track.node].channels.matrix();
        for (std::size_t i = 0; i < track.weights.size(); ++i)
            result[track.node].weights[i] = *track.weights[i].sample(time);
    }
}
}
