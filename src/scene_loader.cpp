#include "sengine/scene_loader.hpp"

namespace sengine {
namespace {
void validate_id(const std::string& id) {
    if (id.empty() || id.size() > 4096 || id.front() == '/' || id.back() == '/')
        throw std::invalid_argument("Invalid entity ID: " + id);
    bool separator = false;
    for (unsigned char c : id) {
        if (c == '/' && separator)
            throw std::invalid_argument("Empty entity ID segment: " + id);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-' || c == '/'))
            throw std::invalid_argument("Invalid entity ID: " + id);
        separator = c == '/';
    }
}
void apply_override(scene_entity& target, const scene_override& change) {
    if (change.name)
        target.name = *change.name;
    if (change.transform)
        target.transform = *change.transform;
    for (const auto& name : change.remove_components) {
        if (change.components.contains(name))
            throw std::invalid_argument("Cannot remove and override the same component: " + name);
        if (!target.components.erase(name))
            throw std::invalid_argument("Cannot remove missing component: " + name);
    }
    for (const auto& [name, patch] : change.components) {
        const auto found = target.components.find(name);
        if (found == target.components.end()) {
            target.components.emplace(name, patch);
        } else {
            if (patch.version != found->second.version)
                throw std::invalid_argument("Override component version differs: " + name);
            for (const auto& [field, value] : patch.fields)
                found->second.fields.insert_or_assign(field, value);
        }
    }
}
}
entity scene_world::find(const std::string& id) const {
    const auto found = names_.find(id);
    return found != names_.end() && world_.alive(found->second) ? found->second : entity{};
}
void scene_world::identify(entity id, std::string name) {
    validate_id(name);
    if (!world_.alive(id))
        throw std::invalid_argument("Cannot identify a stale or foreign entity");
    if (const auto existing = identities_.find(id); existing != identities_.end())
        throw std::invalid_argument("Entity already has ID: " + existing->second);
    if (find(name))
        throw std::invalid_argument("Duplicate entity ID: " + name);
    const auto previous = names_.find(name);
    const auto stale = previous == names_.end() ? entity{} : previous->second;
    identities_.emplace(id, name);
    try {
        names_.insert_or_assign(std::move(name), id);
    } catch (...) {
        identities_.erase(id);
        throw;
    }
    identities_.erase(stale);
}
struct scene_loader::expansion {
    std::vector<scene_entity> entities;
    std::map<std::string, std::size_t, std::less<>> positions;
    std::map<std::filesystem::path, scene_document> files;
    std::vector<std::filesystem::path> stack;
    std::size_t depth{};

  public:
    void append(scene_entity value, std::size_t limit) {
        validate_id(value.id);
        if (entities.size() >= limit)
            throw std::length_error("Expanded scene exceeds entity limit");
        if (!positions.emplace(value.id, entities.size()).second)
            throw std::invalid_argument("Duplicate entity ID: " + value.id);
        entities.push_back(std::move(value));
    }
};
scene_loader::scene_loader(const scene_registry& registry, const asset_store& assets, scene_limits limits)
    : registry_(registry), assets_(assets), limits_(limits) {
    if (!limits.entities || !limits.prefab_depth || !limits.files)
        throw std::invalid_argument("Scene limits must be positive");
}
void scene_loader::remap(scene_entity& value, const std::string& prefix) const {
    for (auto& [name, data] : value.components) {
        try {
            registry_.require(name).remap(data, prefix);
        } catch (const std::exception& error) {
            throw std::invalid_argument(value.id + '/' + name + ": " + error.what());
        }
    }
}
void scene_loader::expand(const scene_document& document, const std::string& prefix,
                          const std::string& parent, expansion& result) const {
    if (document.version != 1)
        throw std::invalid_argument("Unsupported scene version " + std::to_string(document.version));
    for (auto value : document.entities) {
        validate_id(value.id);
        value.id = detail::scene_reference(prefix, value.id);
        value.parent = value.parent.empty() ? parent : detail::scene_reference(prefix, value.parent);
        remap(value, prefix);
        result.append(std::move(value), limits_.entities);
    }
    for (const auto& instance : document.instances) {
        validate_id(instance.id);
        const auto id = detail::scene_reference(prefix, instance.id);
        try {
            if (result.depth >= limits_.prefab_depth)
                throw std::length_error("Prefab nesting exceeds depth limit");
            const auto path = assets_.resolve(instance.source.uri());
            if (std::ranges::find(result.stack, path) != result.stack.end())
                throw std::invalid_argument("Prefab dependency cycle: " + path.string());
            if (!result.files.contains(path)) {
                if (result.files.size() >= limits_.files)
                    throw std::length_error("Scene exceeds source file limit");
                result.files.emplace(path, read_scene(path));
            }
            result.append(
                {id,
                 instance.id,
                 instance.parent.empty() ? parent : detail::scene_reference(prefix, instance.parent),
                 instance.transform,
                 {}},
                limits_.entities);
            result.stack.push_back(path);
            ++result.depth;
            const auto first_child = result.entities.size();
            expand(result.files.at(path), id, id, result);
            --result.depth;
            result.stack.pop_back();
            for (const auto& [target, change] : instance.overrides) {
                validate_id(target);
                const auto name = id + '/' + target;
                const auto found = result.positions.find(name);
                if (found == result.positions.end() || found->second < first_child)
                    throw std::invalid_argument("Override targets missing entity: " + name);
                scene_entity patch{name, {}, {}, {}, change.components};
                remap(patch, id);
                auto resolved = change;
                resolved.components = std::move(patch.components);
                apply_override(result.entities[found->second], resolved);
            }
        } catch (const std::exception& error) {
            throw std::invalid_argument("instance " + id + " (" + instance.source.uri() +
                                        "): " + error.what());
        }
    }
}
std::unique_ptr<scene_world> scene_loader::assemble(std::vector<scene_entity> records) const {
    std::map<std::string, std::size_t, std::less<>> indices;
    for (std::size_t i = 0; i < records.size(); ++i)
        indices.emplace(records[i].id, i);
    std::vector<std::vector<std::size_t>> children(records.size());
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto& record = records[i];
        if (record.parent.empty()) {
            order.push_back(i);
        } else {
            const auto parent = indices.find(record.parent);
            if (parent == indices.end())
                throw std::invalid_argument(record.id + ": unknown parent " + record.parent);
            children[parent->second].push_back(i);
        }
    }
    for (std::size_t i = 0; i < order.size(); ++i)
        order.insert(order.end(), children[order[i]].begin(), children[order[i]].end());
    if (order.size() != records.size())
        throw std::invalid_argument("Scene hierarchy contains a cycle");
    auto result = std::make_unique<scene_world>();
    for (auto index : order) {
        const auto& record = records[index];
        try {
            const auto id = result->entities().create(record.name, result->find(record.parent));
            result->entities().set_local_transform(id, record.transform);
            result->identify(id, record.id);
        } catch (const std::exception& error) {
            throw std::invalid_argument(record.id + ": " + error.what());
        }
    }
    const detail::scene_read_context context{result->names(), assets_};
    for (auto& record : records)
        for (auto& [name, data] : record.components) {
            try {
                registry_.require(name).decode(result->entities(), result->find(record.id), std::move(data),
                                               context);
            } catch (const std::exception& error) {
                throw std::invalid_argument(record.id + '/' + name + ": " + error.what());
            }
        }
    return result;
}
std::unique_ptr<scene_world> scene_loader::load(const std::string& uri) const {
    try {
        expansion result;
        const auto path = assets_.resolve(uri);
        result.files.emplace(path, read_scene(path));
        result.stack.push_back(path);
        expand(result.files.at(path), {}, {}, result);
        return assemble(std::move(result.entities));
    } catch (const std::exception& error) {
        throw std::invalid_argument(uri + ": " + error.what());
    }
}
std::unique_ptr<scene_world> scene_loader::build(const scene_document& document) const {
    expansion result;
    expand(document, {}, {}, result);
    return assemble(std::move(result.entities));
}
scene_document scene_loader::capture(const scene_world& source) const {
    std::map<entity, std::string> names;
    for (const auto& [name, id] : source.names())
        if (source.entities().alive(id))
            names.emplace(id, name);
    if (names.size() != source.entities().size())
        throw std::invalid_argument("Every entity needs a persistent ID before capture");
    const detail::scene_write_context context{names};
    scene_document result;
    for (const auto& [name, id] : source.names()) {
        if (!source.entities().alive(id))
            continue;
        for (auto type : source.entities().component_types(id))
            if (!registry_.types_.contains(type))
                throw std::invalid_argument(name + ": cannot capture an unregistered component");
        scene_entity record{name,
                            source.entities().name(id),
                            context.reference(source.entities().parent(id)),
                            source.entities().local_transform(id),
                            {}};
        for (const auto& [type, codec] : registry_.codecs_) {
            try {
                if (auto data = codec->encode(source.entities(), id, context))
                    record.components.emplace(type, std::move(*data));
            } catch (const std::exception& error) {
                throw std::invalid_argument(name + '/' + type + ": " + error.what());
            }
        }
        result.entities.push_back(std::move(record));
    }
    return result;
}
}
