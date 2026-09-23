#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace sengine {
template <class signature> class unique_function;
template <class result, class... args> class unique_function<result(args...)> {
  private:
    struct operations {
        result (*invoke)(void*, args&&...);
        void (*destroy)(void*) noexcept;
        void (*move)(void*, void*) noexcept;
    };
    template <class function>
    static constexpr bool inline_storage =
        sizeof(function) <= 48 && alignof(function) <= alignof(std::max_align_t) &&
        std::is_nothrow_move_constructible_v<function>;

  private:
    template <class function> static function& target(void* storage) noexcept {
        if constexpr (inline_storage<function>)
            return *std::launder(static_cast<function*>(storage));
        else
            return **std::launder(static_cast<function**>(storage));
    }
    template <class function> static result invoke_target(void* storage, args&&... values) {
        if constexpr (std::is_void_v<result>)
            std::invoke(target<function>(storage), std::forward<args>(values)...);
        else
            return std::invoke(target<function>(storage), std::forward<args>(values)...);
    }
    template <class function> static void destroy_target(void* storage) noexcept {
        if constexpr (inline_storage<function>)
            std::destroy_at(&target<function>(storage));
        else
            delete &target<function>(storage);
    }
    template <class function> static void move_target(void* from, void* to) noexcept {
        if constexpr (inline_storage<function>) {
            std::construct_at(static_cast<function*>(to), std::move(target<function>(from)));
            std::destroy_at(&target<function>(from));
        } else {
            std::construct_at(static_cast<function**>(to), &target<function>(from));
        }
    }
    template <class function> static const operations& table() {
        static const operations value{invoke_target<function>, destroy_target<function>,
                                      move_target<function>};
        return value;
    }
    void reset() noexcept {
        if (operations_)
            operations_->destroy(storage_);
        operations_ = nullptr;
    }

  public:
    unique_function() noexcept = default;
    unique_function(std::nullptr_t) noexcept {}
    template <class function>
        requires(!std::is_same_v<std::remove_cvref_t<function>, unique_function> &&
                 std::is_invocable_r_v<result, std::decay_t<function>&, args...>)
    unique_function(function&& callable) {
        using stored = std::decay_t<function>;
        if constexpr (std::is_pointer_v<stored> || std::is_member_pointer_v<stored>)
            if (!callable)
                return;
        if constexpr (inline_storage<stored>)
            std::construct_at(reinterpret_cast<stored*>(storage_), std::forward<function>(callable));
        else
            std::construct_at(reinterpret_cast<stored**>(storage_),
                              new stored(std::forward<function>(callable)));
        operations_ = &table<stored>();
    }
    unique_function(unique_function&& other) noexcept
        : operations_(std::exchange(other.operations_, nullptr)) {
        if (operations_)
            operations_->move(other.storage_, storage_);
    }
    unique_function& operator=(unique_function&& other) noexcept {
        if (this != &other) {
            reset();
            operations_ = std::exchange(other.operations_, nullptr);
            if (operations_)
                operations_->move(other.storage_, storage_);
        }
        return *this;
    }
    unique_function(const unique_function&) = delete;
    unique_function& operator=(const unique_function&) = delete;
    ~unique_function() { reset(); }
    explicit operator bool() const noexcept { return operations_ != nullptr; }
    result operator()(args... values) {
        if (!operations_)
            throw std::bad_function_call();
        return operations_->invoke(storage_, std::forward<args>(values)...);
    }

  private:
    alignas(std::max_align_t) std::byte storage_[48];
    const operations* operations_{};
};
}
