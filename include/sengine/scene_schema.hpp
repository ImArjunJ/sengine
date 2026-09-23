#pragma once
#include "assets.hpp"
#include "scene_document.hpp"
#include "world.hpp"
#include <concepts>
#include <initializer_list>
#include <limits>
#include <set>
#include <type_traits>

namespace sengine {
namespace detail {
struct scene_read_context {
    const std::map<std::string, entity, std::less<>>& entities;
    const asset_store& assets;

  public:
    entity reference(const std::string&) const;
    asset_reference asset(const std::string&) const;
};
struct scene_write_context {
    const std::map<entity, std::string>& names;

  public:
    std::string reference(entity) const;
};
double scene_number(const scene_value&);
const std::string& scene_string(const scene_value&);
std::vector<double> scene_vector(const scene_value&, std::size_t);
scene_value scene_encode(float2);
scene_value scene_encode(float3);
scene_value scene_encode(float4);
scene_value scene_encode(quaternion);
scene_value scene_encode(const mat4&);
void scene_decode(const scene_value&, float2&);
void scene_decode(const scene_value&, float3&);
void scene_decode(const scene_value&, float4&);
void scene_decode(const scene_value&, quaternion&);
void scene_decode(const scene_value&, mat4&);
template <std::integral value> scene_value scene_encode(value data) {
    if constexpr (std::same_as<value, bool>)
        return data;
    else if constexpr (std::is_signed_v<value>)
        return std::int64_t(data);
    else
        return std::uint64_t(data);
}
template <std::floating_point value> scene_value scene_encode(value data) {
    if (!std::isfinite(data))
        throw std::invalid_argument("Expected a finite number");
    return double(data);
}
inline scene_value scene_encode(const std::string& data) {
    return data;
}
inline scene_value scene_encode(const asset_reference& data) {
    return data.uri();
}
template <std::integral value> void scene_decode(const scene_value& data, value& result) {
    if constexpr (std::same_as<value, bool>) {
        const auto* boolean = std::get_if<bool>(&data);
        if (!boolean)
            throw std::invalid_argument("Expected a boolean");
        result = *boolean;
    } else if (const auto* number = std::get_if<std::int64_t>(&data)) {
        if (!std::in_range<value>(*number))
            throw std::out_of_range("Integer is out of range");
        result = value(*number);
    } else if (const auto* number = std::get_if<std::uint64_t>(&data)) {
        if (!std::in_range<value>(*number))
            throw std::out_of_range("Integer is out of range");
        result = value(*number);
    } else {
        throw std::invalid_argument("Expected an integer");
    }
}
template <std::floating_point value> void scene_decode(const scene_value& data, value& result) {
    const double number = scene_number(data);
    if (number < -std::numeric_limits<value>::max() || number > std::numeric_limits<value>::max())
        throw std::out_of_range("Number is out of range");
    result = value(number);
}
inline void scene_decode(const scene_value& data, std::string& result) {
    result = scene_string(data);
}
template <std::floating_point value> scene_value scene_encode(const std::vector<value>& data) {
    if (data.size() > 256)
        throw std::invalid_argument("Numeric vectors contain at most 256 values");
    std::vector<double> result;
    result.reserve(data.size());
    for (value item : data)
        result.push_back(std::get<double>(scene_encode(item)));
    return result;
}
template <std::floating_point value> void scene_decode(const scene_value& data, std::vector<value>& result) {
    const auto* numbers = std::get_if<std::vector<double>>(&data);
    if (!numbers || numbers->size() > 256)
        throw std::invalid_argument("Expected a numeric vector of at most 256 values");
    result.clear();
    result.reserve(numbers->size());
    for (double number : *numbers) {
        value item;
        scene_decode(scene_value(number), item);
        result.push_back(item);
    }
}
std::string scene_reference(const std::string& prefix, const std::string& reference);
class scene_codec {
  public:
    virtual ~scene_codec() = default;
    virtual void decode(world&, entity, scene_component, const scene_read_context&) const = 0;
    virtual std::optional<scene_component> encode(const world&, entity, const scene_write_context&) const = 0;
    virtual void remap(scene_component&, const std::string& prefix) const = 0;
};
template <class component> class scene_field {
  public:
    virtual ~scene_field() = default;
    virtual void decode(component&, const scene_value&, const scene_read_context&) const = 0;
    virtual scene_value encode(const component&, const scene_write_context&) const = 0;
    virtual void remap(scene_value&, const std::string&) const = 0;
};
template <class component, class value> class scene_member final : public scene_field<component> {
  public:
    explicit scene_member(value component::* member) : member_(member) {}
    void decode(component& target, const scene_value& data,
                const scene_read_context& context) const override {
        if constexpr (std::same_as<value, entity>)
            target.*member_ = context.reference(scene_string(data));
        else if constexpr (std::same_as<value, asset_reference>)
            target.*member_ = context.asset(scene_string(data));
        else
            scene_decode(data, target.*member_);
    }
    scene_value encode(const component& target, const scene_write_context& context) const override {
        if constexpr (std::same_as<value, entity>)
            return context.reference(target.*member_);
        else
            return scene_encode(target.*member_);
    }
    void remap(scene_value& data, const std::string& prefix) const override {
        if constexpr (std::same_as<value, entity>)
            data = scene_reference(prefix, scene_string(data));
    }

  private:
    value component::* member_;
};
template <class component, class value> class scene_enum final : public scene_field<component> {
  public:
    scene_enum(value component::* member, std::initializer_list<std::pair<std::string, value>> names)
        : member_(member) {
        std::set<value> values;
        for (const auto& [name, item] : names)
            if (name.empty() || !names_.emplace(name, item).second || !values.insert(item).second)
                throw std::invalid_argument("Invalid or duplicate enum name");
        if (names_.empty())
            throw std::invalid_argument("An enum needs named values");
    }
    void decode(component& target, const scene_value& data, const scene_read_context&) const override {
        const auto& name = scene_string(data);
        const auto found = names_.find(name);
        if (found == names_.end())
            throw std::invalid_argument("Unknown enum value: " + name);
        target.*member_ = found->second;
    }
    scene_value encode(const component& target, const scene_write_context&) const override {
        for (const auto& [name, item] : names_)
            if (target.*member_ == item)
                return name;
        throw std::invalid_argument("Enum value has no registered name");
    }
    void remap(scene_value&, const std::string&) const override {}

  private:
    value component::* member_;
    std::map<std::string, value, std::less<>> names_;
};
}
template <class component> class component_schema final : public detail::scene_codec {
  public:
    explicit component_schema(unsigned version = 1) : version_(version) {
        if (!version)
            throw std::invalid_argument("Component versions start at one");
    }
    template <class value>
    component_schema& field(std::string name, value component::* member, bool required = true) {
        if (name.empty() || !member || fields_.contains(name))
            throw std::invalid_argument("Invalid or duplicate component field: " + name);
        fields_.emplace(
            std::move(name),
            field_record{std::make_unique<detail::scene_member<component, value>>(member), required});
        return *this;
    }
    component_schema& validate(void (*check)(const component&)) {
        check_ = check;
        return *this;
    }
    template <class value>
        requires std::is_enum_v<value>
    component_schema& enumeration(std::string name, value component::* member,
                                  std::initializer_list<std::pair<std::string, value>> values,
                                  bool required = true) {
        if (name.empty() || !member || fields_.contains(name))
            throw std::invalid_argument("Invalid or duplicate component field: " + name);
        fields_.emplace(
            std::move(name),
            field_record{std::make_unique<detail::scene_enum<component, value>>(member, values), required});
        return *this;
    }
    component_schema& migrate(unsigned from_version, void (*convert)(scene_properties&)) {
        if (!from_version || from_version >= version_ || !convert || migrations_.contains(from_version))
            throw std::invalid_argument("Invalid component migration");
        migrations_.emplace(from_version, convert);
        return *this;
    }

  private:
    scene_component upgrade(scene_component data) const {
        while (data.version < version_) {
            const auto migration = migrations_.find(data.version);
            if (migration == migrations_.end())
                throw std::invalid_argument("No migration from component version " +
                                            std::to_string(data.version));
            migration->second(data.fields);
            ++data.version;
        }
        if (data.version != version_)
            throw std::invalid_argument("Unsupported component version " + std::to_string(data.version));
        return data;
    }
    void decode(world& target, entity id, scene_component data,
                const detail::scene_read_context& context) const override {
        data = upgrade(std::move(data));
        component result{};
        for (const auto& [name, value] : data.fields)
            if (!fields_.contains(name))
                throw std::invalid_argument("Unknown field: " + name);
        for (const auto& [name, field] : fields_) {
            const auto found = data.fields.find(name);
            if (found == data.fields.end()) {
                if (field.required)
                    throw std::invalid_argument("Missing field: " + name);
                continue;
            }
            try {
                field.codec->decode(result, found->second, context);
            } catch (const std::exception& error) {
                throw std::invalid_argument(name + ": " + error.what());
            }
        }
        if (check_)
            check_(result);
        target.emplace<component>(id, std::move(result));
    }
    std::optional<scene_component> encode(const world& source, entity id,
                                          const detail::scene_write_context& context) const override {
        const auto* value = source.get<component>(id);
        if (!value)
            return {};
        if (check_)
            check_(*value);
        scene_component result{version_, {}};
        for (const auto& [name, field] : fields_)
            result.fields.emplace(name, field.codec->encode(*value, context));
        return result;
    }
    void remap(scene_component& data, const std::string& prefix) const override {
        data = upgrade(std::move(data));
        for (auto& [name, value] : data.fields) {
            const auto found = fields_.find(name);
            if (found == fields_.end())
                throw std::invalid_argument("Unknown field: " + name);
            found->second.codec->remap(value, prefix);
        }
    }

  private:
    struct field_record {
        std::unique_ptr<detail::scene_field<component>> codec;
        bool required;
    };
    unsigned version_;
    std::map<std::string, field_record, std::less<>> fields_;
    std::map<unsigned, void (*)(scene_properties&)> migrations_;
    void (*check_)(const component&){};
};
class scene_registry {
  public:
    template <class component> void add(std::string name, component_schema<component> schema) {
        if (name.empty() || codecs_.contains(name) || types_.contains(typeid(component)))
            throw std::invalid_argument("Empty or duplicate component registration: " + name);
        auto codec = std::make_unique<component_schema<component>>(std::move(schema));
        const auto inserted = types_.insert(typeid(component));
        try {
            codecs_.emplace(std::move(name), std::move(codec));
        } catch (...) {
            types_.erase(inserted.first);
            throw;
        }
    }

  private:
    friend class scene_loader;
    const detail::scene_codec& require(const std::string&) const;

  private:
    std::map<std::string, std::unique_ptr<detail::scene_codec>, std::less<>> codecs_;
    std::set<std::type_index> types_;
};
}
