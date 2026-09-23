#include "sengine/world.hpp"
#include <atomic>
#include <limits>

namespace sengine {
namespace {
std::atomic<std::uint64_t> next_world{1};
std::uint64_t world_identity() {
    auto identity = next_world.load();
    do {
        if (identity == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("World identity exhausted");
    } while (!next_world.compare_exchange_weak(identity, identity + 1));
    return identity;
}
void validate(const mat4& transform) {
    for (const auto& column : transform.columns)
        for (float value : {column.x, column.y, column.z, column.w})
            if (!std::isfinite(value))
                throw std::invalid_argument("Non-finite transform");
    if (transform[0].w != 0 || transform[1].w != 0 || transform[2].w != 0 || transform[3].w != 1)
        throw std::invalid_argument("World transforms must be affine");
}
}
world::world() : identity_(world_identity()) {}
bool world::alive(entity id) const noexcept {
    return id.owner == identity_ && id.index < records_.size() && records_[id.index].alive &&
           records_[id.index].generation == id.generation;
}
entity world::handle(std::uint32_t index) const noexcept {
    return {identity_, index, records_[index].generation};
}
const world::record& world::require(entity id) const {
    if (!alive(id))
        throw std::invalid_argument("Stale or foreign entity");
    return records_[id.index];
}
world::record& world::require(entity id) {
    return const_cast<record&>(std::as_const(*this).require(id));
}
void world::require_mutable() const {
    if (iteration_depth_)
        throw std::logic_error("Defer structural changes until after iteration");
}
entity world::create(std::string name, entity parent) {
    require_mutable();
    if (parent) {
        auto& children = require(parent).children;
        if (children.size() == children.capacity())
            children.reserve(children.size() + std::max(std::size_t{1}, children.size() / 2));
    }
    if (records_.size() >= std::numeric_limits<std::uint32_t>::max() && available_.empty())
        throw std::overflow_error("Entity capacity exhausted");
    const auto index = available_.empty() ? std::uint32_t(records_.size()) : available_.back();
    if (available_.empty())
        records_.emplace_back();
    else
        available_.pop_back();
    auto& entry = records_[index];
    entry.name = std::move(name);
    entry.parent = parent;
    entry.alive = true;
    const auto id = handle(index);
    if (parent)
        records_[parent.index].children.push_back(id);
    ++size_;
    invalidate_transforms();
    return id;
}
void world::erase_record(entity id) {
    auto& entry = records_[id.index];
    for (auto& [type, pool] : pools_)
        pool->erase(id.index);
    const auto generation = entry.generation;
    entry = {};
    entry.generation = generation;
    if (generation != std::numeric_limits<std::uint32_t>::max()) {
        ++entry.generation;
        available_.push_back(id.index);
    }
    --size_;
}
bool world::destroy(entity id) {
    require_mutable();
    if (!alive(id))
        return false;
    std::vector<entity> subtree{id};
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        const auto& children = records_[subtree[i].index].children;
        subtree.insert(subtree.end(), children.begin(), children.end());
    }
    available_.reserve(available_.size() + subtree.size());
    if (auto parent = records_[id.index].parent)
        std::erase(records_[parent.index].children, id);
    for (auto entry : std::views::reverse(subtree))
        erase_record(entry);
    invalidate_transforms();
    return true;
}
std::vector<entity> world::entities() const {
    std::vector<entity> result;
    result.reserve(size_);
    for (std::uint32_t index = 0; index < records_.size(); ++index)
        if (records_[index].alive)
            result.push_back(handle(index));
    return result;
}
const std::string& world::name(entity id) const {
    return require(id).name;
}
std::vector<std::type_index> world::component_types(entity id) const {
    require(id);
    std::vector<std::type_index> result;
    for (const auto& [type, pool] : pools_)
        if (pool->contains(id.index))
            result.push_back(type);
    return result;
}
void world::rename(entity id, std::string name) {
    require(id).name = std::move(name);
}
entity world::parent(entity id) const {
    return require(id).parent;
}
std::span<const entity> world::children(entity id) const {
    return require(id).children;
}
const mat4& world::local_transform(entity id) const {
    return require(id).local;
}
void world::invalidate_transforms() {
    if (++revision_ == 0) {
        revision_ = 1;
        for (auto& entry : records_)
            entry.revision = 0;
    }
}
mat4 world::world_transform(entity id) const {
    require(id);
    std::vector<std::uint32_t> chain;
    for (auto current = id; current && records_[current.index].revision != revision_;
         current = records_[current.index].parent)
        chain.push_back(current.index);
    for (auto index : std::views::reverse(chain)) {
        const auto& entry = records_[index];
        entry.global = entry.parent ? records_[entry.parent.index].global * entry.local : entry.local;
        entry.revision = revision_;
    }
    return records_[id.index].global;
}
void world::set_local_transform(entity id, const mat4& transform) {
    validate(transform);
    require(id).local = transform;
    invalidate_transforms();
}
void world::set_world_transform(entity id, const mat4& transform) {
    const auto parent = require(id).parent;
    set_local_transform(id, parent ? inverse(world_transform(parent)) * transform : transform);
}
void world::reparent(entity child, entity parent, bool preserve_world) {
    require_mutable();
    auto& entry = require(child);
    if (parent)
        require(parent);
    for (auto current = parent; current; current = records_[current.index].parent)
        if (current == child)
            throw std::invalid_argument("Transform hierarchy cycle");
    if (entry.parent == parent)
        return;
    auto local = entry.local;
    if (preserve_world) {
        const auto global = world_transform(child);
        local = parent ? inverse(world_transform(parent)) * global : global;
    }
    validate(local);
    if (parent)
        records_[parent.index].children.push_back(child);
    if (entry.parent)
        std::erase(records_[entry.parent.index].children, child);
    entry.parent = parent;
    entry.local = local;
    invalidate_transforms();
}
void world::defer(unique_function<void(world&)> command) {
    if (!command)
        throw std::invalid_argument("Empty world command");
    deferred_.push_back(std::move(command));
}
void world::flush() {
    require_mutable();
    if (flushing_)
        throw std::logic_error("Cannot reenter world command queue");
    flushing_ = true;
    try {
        const auto count = deferred_.size();
        for (std::size_t i = 0; i < count; ++i) {
            auto command = std::move(deferred_.front());
            deferred_.pop_front();
            command(*this);
        }
    } catch (...) {
        flushing_ = false;
        throw;
    }
    flushing_ = false;
}
}
