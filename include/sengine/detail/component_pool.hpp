#pragma once
#include <cstdint>
#include <limits>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace sengine::detail {
class component_pool_base {
  public:
    virtual ~component_pool_base() = default;
    virtual void erase(std::uint32_t index) noexcept = 0;
    virtual std::span<const std::uint32_t> entities() const noexcept = 0;
};

template <class component> class component_pool final : public component_pool_base {
    static_assert(std::is_nothrow_move_constructible_v<component> &&
                  std::is_nothrow_move_assignable_v<component>);

  public:
    template <class... args> component& emplace(std::uint32_t index, args&&... values) {
        if (index >= sparse_.size())
            sparse_.resize(std::size_t(index) + 1, missing);
        values_.emplace_back(std::forward<args>(values)...);
        try {
            entities_.push_back(index);
        } catch (...) {
            values_.pop_back();
            throw;
        }
        sparse_[index] = values_.size() - 1;
        return values_.back();
    }
    component* get(std::uint32_t index) noexcept {
        return index < sparse_.size() && sparse_[index] != missing ? &values_[sparse_[index]] : nullptr;
    }
    const component* get(std::uint32_t index) const noexcept {
        return index < sparse_.size() && sparse_[index] != missing ? &values_[sparse_[index]] : nullptr;
    }
    void erase(std::uint32_t index) noexcept override {
        if (!get(index))
            return;
        const auto slot = sparse_[index];
        if (slot + 1 != values_.size()) {
            values_[slot] = std::move(values_.back());
            entities_[slot] = entities_.back();
            sparse_[entities_[slot]] = slot;
        }
        values_.pop_back();
        entities_.pop_back();
        sparse_[index] = missing;
    }
    std::span<const std::uint32_t> entities() const noexcept override { return entities_; }

  private:
    static constexpr auto missing = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> sparse_;
    std::vector<std::uint32_t> entities_;
    std::vector<component> values_;
};
}
