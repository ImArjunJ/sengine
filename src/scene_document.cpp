#include "sengine/scene_document.hpp"
#include "sengine/assets.hpp"
#include "sengine/scene_schema.hpp"
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <random>
#include <set>

namespace sengine {
namespace {
using json = nlohmann::ordered_json;
class parse_guard {
  public:
    bool operator()(int depth, json::parse_event_t event, json& value) {
        if (depth > 64)
            throw std::invalid_argument("JSON nesting exceeds 64 levels");
        if (event == json::parse_event_t::object_start)
            keys_.emplace_back();
        else if (event == json::parse_event_t::object_end)
            keys_.pop_back();
        else if (event == json::parse_event_t::key && !keys_.back().insert(value.get<std::string>()).second)
            throw std::invalid_argument("Duplicate key: " + value.get<std::string>());
        return true;
    }

  private:
    std::vector<std::set<std::string>> keys_;
};
void object(const json& value, std::initializer_list<std::string_view> keys, const std::string& path) {
    if (!value.is_object())
        throw std::invalid_argument(path + ": expected an object");
    for (const auto& [key, item] : value.items())
        if (std::ranges::find(keys, key) == keys.end())
            throw std::invalid_argument(path + ": unknown key '" + key + "'");
}
unsigned version(const json& value) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > std::numeric_limits<unsigned>::max() ||
        value == 0)
        throw std::invalid_argument("Expected a positive integer version");
    return value.get<unsigned>();
}
scene_value property(const json& value) {
    if (value.is_boolean())
        return value.get<bool>();
    if (value.is_number_unsigned())
        return value.get<std::uint64_t>();
    if (value.is_number_integer())
        return value.get<std::int64_t>();
    if (value.is_number_float()) {
        const auto number = value.get<double>();
        if (!std::isfinite(number))
            throw std::invalid_argument("Expected a finite number");
        return number;
    }
    if (value.is_string())
        return value.get<std::string>();
    if (value.is_array()) {
        if (value.size() > 256)
            throw std::invalid_argument("Numeric vectors contain at most 256 values");
        std::vector<double> result;
        for (const auto& item : value) {
            if (!item.is_number())
                throw std::invalid_argument("Expected a numeric vector");
            result.push_back(item.get<double>());
        }
        return result;
    }
    throw std::invalid_argument("Expected a scalar or numeric vector");
}
mat4 transform(const json& value) {
    mat4 result;
    if (value.is_array()) {
        detail::scene_decode(property(value), result);
    } else {
        object(value, {"position", "rotation", "scale"}, "transform");
        float3 position{}, scale{1};
        quaternion orientation;
        if (value.contains("position"))
            detail::scene_decode(property(value.at("position")), position);
        if (value.contains("scale"))
            detail::scene_decode(property(value.at("scale")), scale);
        if (value.contains("rotation"))
            detail::scene_decode(property(value.at("rotation")), orientation);
        const double magnitude = std::hypot(std::hypot(double(orientation.x), double(orientation.y)),
                                            std::hypot(double(orientation.z), double(orientation.w)));
        if (magnitude == 0)
            throw std::invalid_argument("Rotation quaternion must be nonzero");
        orientation = {float(orientation.x / magnitude), float(orientation.y / magnitude),
                       float(orientation.z / magnitude), float(orientation.w / magnitude)};
        result = translation(position) * rotation(orientation) * scaling(scale);
    }
    return result;
}
std::map<std::string, scene_component, std::less<>> components(const json& values) {
    if (!values.is_object())
        throw std::invalid_argument("Components must be an object");
    std::map<std::string, scene_component, std::less<>> result;
    for (const auto& [name, value] : values.items()) {
        object(value, {"version", "fields"}, name);
        scene_component component{version(value.at("version")), {}};
        const auto& fields = value.at("fields");
        if (!fields.is_object())
            throw std::invalid_argument(name + ": fields must be an object");
        for (const auto& [field, data] : fields.items()) {
            try {
                component.fields.emplace(field, property(data));
            } catch (const std::exception& error) {
                throw std::invalid_argument(name + '.' + field + ": " + error.what());
            }
        }
        result.emplace(name, std::move(component));
    }
    return result;
}
scene_entity read_entity(const json& value) {
    object(value, {"id", "name", "parent", "transform", "components"}, "entity");
    scene_entity result;
    result.id = value.at("id").get<std::string>();
    result.name = value.value("name", result.id);
    result.parent = value.value("parent", std::string{});
    if (value.contains("transform"))
        result.transform = transform(value.at("transform"));
    if (value.contains("components"))
        result.components = components(value.at("components"));
    return result;
}
prefab_instance read_instance(const json& value) {
    object(value, {"id", "parent", "source", "transform", "overrides"}, "instance");
    prefab_instance result;
    result.id = value.at("id").get<std::string>();
    result.parent = value.value("parent", std::string{});
    result.source = asset_reference(value.at("source").get<std::string>());
    if (value.contains("transform"))
        result.transform = transform(value.at("transform"));
    if (value.contains("overrides")) {
        const auto& overrides = value.at("overrides");
        if (!overrides.is_object())
            throw std::invalid_argument("Overrides must be an object");
        for (const auto& [id, data] : overrides.items()) {
            object(data, {"name", "transform", "components", "remove_components"}, "override " + id);
            scene_override change;
            if (data.contains("name"))
                change.name = data.at("name").get<std::string>();
            if (data.contains("transform"))
                change.transform = transform(data.at("transform"));
            if (data.contains("components"))
                change.components = components(data.at("components"));
            if (data.contains("remove_components"))
                change.remove_components = data.at("remove_components").get<std::vector<std::string>>();
            result.overrides.emplace(id, std::move(change));
        }
    }
    return result;
}
struct property_writer {
    template <class value> json operator()(const value& data) const { return data; }
};
json write_components(const std::map<std::string, scene_component, std::less<>>& components) {
    auto result = json::object();
    for (const auto& [name, component] : components) {
        auto fields = json::object();
        for (const auto& [field, value] : component.fields) {
            if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
                throw std::invalid_argument(name + '.' + field + ": non-finite number");
            if (const auto* numbers = std::get_if<std::vector<double>>(&value))
                for (auto number : *numbers)
                    if (!std::isfinite(number))
                        throw std::invalid_argument(name + '.' + field + ": non-finite coordinate");
            fields[field] = std::visit(property_writer{}, value);
        }
        result[name] = {{"version", component.version}, {"fields", std::move(fields)}};
    }
    return result;
}
json write_transform(const mat4& value) {
    auto result = json::array();
    for (auto column : value.columns)
        for (auto number : {column.x, column.y, column.z, column.w}) {
            if (!std::isfinite(number))
                throw std::invalid_argument("Non-finite transform");
            result.push_back(number);
        }
    return result;
}
class temporary_file {
  public:
    explicit temporary_file(const std::filesystem::path& destination) {
        std::random_device random;
        for (unsigned attempt = 0; attempt < 64; ++attempt) {
            directory_ = destination.string() + ".tmp." + std::to_string(random());
            if (std::filesystem::create_directory(directory_))
                return;
        }
        throw std::runtime_error("Cannot reserve temporary scene file");
    }
    ~temporary_file() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }
    temporary_file(const temporary_file&) = delete;
    temporary_file& operator=(const temporary_file&) = delete;
    std::filesystem::path path() const { return directory_ / "scene"; }

  private:
    std::filesystem::path directory_;
};
}
scene_document parse_scene(std::string_view text, std::string_view source) {
    try {
        if (text.size() > 16 * 1024 * 1024)
            throw std::length_error("Scene exceeds 16 MiB");
        parse_guard guard;
        const auto data = json::parse(text, std::ref(guard));
        object(data, {"version", "entities", "instances"}, "scene");
        scene_document result;
        result.version = version(data.at("version"));
        if (result.version != 1)
            throw std::invalid_argument("Unsupported scene version " + std::to_string(result.version));
        if (data.contains("entities")) {
            if (!data.at("entities").is_array())
                throw std::invalid_argument("Entities must be an array");
            for (const auto& value : data.at("entities")) {
                try {
                    result.entities.push_back(read_entity(value));
                } catch (const std::exception& error) {
                    throw std::invalid_argument("entity[" + std::to_string(result.entities.size()) +
                                                "]: " + error.what());
                }
            }
        }
        if (data.contains("instances")) {
            if (!data.at("instances").is_array())
                throw std::invalid_argument("Instances must be an array");
            for (const auto& value : data.at("instances"))
                result.instances.push_back(read_instance(value));
        }
        return result;
    } catch (const std::exception& error) {
        throw std::invalid_argument(std::string(source) + ": " + error.what());
    }
}
scene_document read_scene(const std::filesystem::path& path) {
    return parse_scene(read_text(path), path.string());
}
std::string write_scene(const scene_document& document) {
    json data{{"version", document.version}, {"entities", json::array()}};
    for (const auto& entity : document.entities) {
        json value{{"id", entity.id}, {"name", entity.name}};
        if (!entity.parent.empty())
            value["parent"] = entity.parent;
        if (entity.transform != mat4{})
            value["transform"] = write_transform(entity.transform);
        if (!entity.components.empty())
            value["components"] = write_components(entity.components);
        data["entities"].push_back(std::move(value));
    }
    if (!document.instances.empty())
        data["instances"] = json::array();
    for (const auto& instance : document.instances) {
        json value{{"id", instance.id}, {"source", instance.source.uri()}};
        if (!instance.parent.empty())
            value["parent"] = instance.parent;
        if (instance.transform != mat4{})
            value["transform"] = write_transform(instance.transform);
        if (!instance.overrides.empty()) {
            auto overrides = json::object();
            for (const auto& [id, change] : instance.overrides) {
                auto item = json::object();
                if (change.name)
                    item["name"] = *change.name;
                if (change.transform)
                    item["transform"] = write_transform(*change.transform);
                if (!change.components.empty())
                    item["components"] = write_components(change.components);
                if (!change.remove_components.empty())
                    item["remove_components"] = change.remove_components;
                overrides[id] = std::move(item);
            }
            value["overrides"] = std::move(overrides);
        }
        data["instances"].push_back(std::move(value));
    }
    auto result = data.dump(2) + '\n';
    parse_scene(result);
    return result;
}
void save_scene(const std::filesystem::path& path, const scene_document& document) {
    try {
        const auto text = write_scene(document);
        const temporary_file staging(path);
        std::ofstream output(staging.path(), std::ios::binary);
        output.exceptions(std::ios::failbit | std::ios::badbit);
        output.write(text.data(), std::streamsize(text.size()));
        output.close();
        std::filesystem::rename(staging.path(), path);
    } catch (const std::exception& error) {
        throw std::runtime_error(path.string() + ": " + error.what());
    }
}
}
