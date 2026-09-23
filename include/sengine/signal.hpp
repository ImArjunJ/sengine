#pragma once
#include "function.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

namespace sengine {
namespace detail {
struct subscription {
    std::atomic<bool> connected{true};

  public:
    virtual ~subscription() = default;
};
}
class connection {
  public:
    connection() = default;
    connection(connection&& other) noexcept : subscription_(std::move(other.subscription_)) {}
    connection& operator=(connection&& other) noexcept {
        if (this != &other) {
            disconnect();
            subscription_ = std::move(other.subscription_);
        }
        return *this;
    }
    connection(const connection&) = delete;
    connection& operator=(const connection&) = delete;
    ~connection() { disconnect(); }
    void disconnect() noexcept {
        if (subscription_)
            subscription_->connected = false;
        subscription_.reset();
    }
    explicit operator bool() const noexcept { return subscription_ && subscription_->connected.load(); }

  private:
    explicit connection(std::shared_ptr<detail::subscription> subscription)
        : subscription_(std::move(subscription)) {}
    template <class> friend class signal;

  private:
    std::shared_ptr<detail::subscription> subscription_;
};

template <class event> class signal {
  public:
    connection subscribe(unique_function<void(const event&)> callback) {
        if (!callback)
            throw std::invalid_argument("Empty signal callback");
        auto slot = std::make_shared<subscription>(std::move(callback));
        std::lock_guard lock(mutex_);
        collect();
        slots_.push_back(slot);
        return connection(std::move(slot));
    }
    void emit(const event& value) {
        std::vector<std::shared_ptr<subscription>> snapshot;
        {
            std::lock_guard lock(mutex_);
            collect();
            snapshot = slots_;
        }
        for (const auto& slot : snapshot)
            if (slot->connected)
                slot->callback(value);
    }
    std::size_t size() {
        std::lock_guard lock(mutex_);
        collect();
        return slots_.size();
    }
    ~signal() {
        for (auto& slot : slots_)
            slot->connected = false;
    }

  private:
    struct subscription final : detail::subscription {
        explicit subscription(unique_function<void(const event&)> callback) : callback(std::move(callback)) {}

      public:
        unique_function<void(const event&)> callback;
    };
    void collect() {
        std::erase_if(slots_, [](const auto& slot) { return !slot->connected; });
    }

  private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<subscription>> slots_;
};
}
