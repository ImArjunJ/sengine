#pragma once
#include "detail/component_pool.hpp"
#include "function.hpp"
#include "math.hpp"
#include <algorithm>
#include <compare>
#include <deque>
#include <functional>
#include <memory>
#include <ranges>
#include <stdexcept>
#include <string>
#include <tuple>
#include <typeindex>
#include <unordered_map>

namespace sengine {
struct entity {
    std::uint64_t owner{};
    std::uint32_t index{}, generation{};

  public:
    explicit operator bool() const { return owner != 0; }
    auto operator<=>(const entity&) const = default;
};

class world {
  public:
    world();
    ~world() = default;
    world(const world&) = delete;
    world& operator=(const world&) = delete;
    world(world&&) = delete;
    world& operator=(world&&) = delete;
    entity create(std::string name = {}, entity parent = {});
    bool alive(entity) const noexcept;
    bool destroy(entity);
    std::size_t size() const noexcept { return size_; }
    std::vector<entity> entities() const;
    const std::string& name(entity) const;
    void rename(entity, std::string);
    entity parent(entity) const;
    std::span<const entity> children(entity) const;
    void reparent(entity child, entity parent, bool preserve_world = true);
    const mat4& local_transform(entity) const;
    mat4 world_transform(entity) const;
    void set_local_transform(entity, const mat4&);
    void set_world_transform(entity, const mat4&);

    template <class component, class... args> component& emplace(entity id, args&&... values) {
        require_mutable();
        require(id);
        auto& pool = assure_pool<component>();
        if (pool.get(id.index))
            throw std::invalid_argument("Component already exists");
        return pool.emplace(id.index, std::forward<args>(values)...);
    }
    template <class component> component* get(entity id) noexcept {
        auto* pool = find_pool<component>();
        return alive(id) && pool ? pool->get(id.index) : nullptr;
    }
    template <class component> const component* get(entity id) const noexcept {
        auto* pool = find_pool<component>();
        return alive(id) && pool ? pool->get(id.index) : nullptr;
    }
    template <class component> bool remove(entity id) {
        require_mutable();
        auto* pool = find_pool<component>();
        if (!alive(id) || !pool || !pool->get(id.index))
            return false;
        pool->erase(id.index);
        return true;
    }
    template <class... components, class function> void each(function&& visit) {
        static_assert(sizeof...(components) > 0);
        const auto typed_pools = std::tuple{find_pool<components>()...};
        const auto pools = std::apply(
            [](auto*... pool) {
                return std::array<detail::component_pool_base*, sizeof...(components)>{pool...};
            },
            typed_pools);
        if (std::ranges::find(pools, nullptr) != pools.end())
            return;
        auto* smallest =
            *std::ranges::min_element(pools, {}, [](auto* pool) { return pool->entities().size(); });
        iteration_scope scope(iteration_depth_);
        for (auto index : smallest->entities()) {
            const auto id = handle(index);
            auto values =
                std::apply([index](auto*... pool) { return std::tuple{pool->get(index)...}; }, typed_pools);
            std::apply(
                [&visit, id](auto*... value) {
                    if ((value && ...))
                        std::invoke(visit, id, *value...);
                },
                values);
        }
    }
    void defer(unique_function<void(world&)>);
    void flush();

  private:
    struct record {
        std::string name;
        entity parent;
        std::vector<entity> children;
        mat4 local;
        mutable mat4 global;
        mutable std::uint64_t revision{};
        std::uint32_t generation{1};
        bool alive{};
    };
    class iteration_scope {
      public:
        explicit iteration_scope(unsigned& depth) : depth_(depth) { ++depth_; }
        ~iteration_scope() { --depth_; }
        iteration_scope(const iteration_scope&) = delete;
        iteration_scope& operator=(const iteration_scope&) = delete;

      private:
        unsigned& depth_;
    };
    entity handle(std::uint32_t index) const noexcept;
    const record& require(entity) const;
    record& require(entity);
    void require_mutable() const;
    void invalidate_transforms();
    void erase_record(entity);
    template <class component> detail::component_pool<component>* find_pool() noexcept {
        const auto found = pools_.find(typeid(component));
        return found == pools_.end() ? nullptr
                                     : static_cast<detail::component_pool<component>*>(found->second.get());
    }
    template <class component> const detail::component_pool<component>* find_pool() const noexcept {
        const auto found = pools_.find(typeid(component));
        return found == pools_.end()
                   ? nullptr
                   : static_cast<const detail::component_pool<component>*>(found->second.get());
    }
    template <class component> detail::component_pool<component>& assure_pool() {
        if (auto* existing = find_pool<component>())
            return *existing;
        auto pool = std::make_unique<detail::component_pool<component>>();
        auto* result = pool.get();
        pools_.emplace(typeid(component), std::move(pool));
        return *result;
    }

  private:
    const std::uint64_t identity_;
    std::vector<record> records_;
    std::vector<std::uint32_t> available_;
    std::unordered_map<std::type_index, std::unique_ptr<detail::component_pool_base>> pools_;
    std::deque<unique_function<void(world&)>> deferred_;
    std::size_t size_{};
    std::uint64_t revision_{1};
    unsigned iteration_depth_{};
    bool flushing_{};
};
}
