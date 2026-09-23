#pragma once
#include "jobs.hpp"
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <typeindex>
#include <vector>

namespace sengine {
namespace detail {
struct asset_slot_base {
    virtual ~asset_slot_base() = default;
};
template <class resource> struct asset_slot final : asset_slot_base {
  public:
    std::shared_ptr<const resource> snapshot() const {
        std::lock_guard lock(mutex_);
        return current_;
    }
    std::uint64_t version() const {
        std::lock_guard lock(mutex_);
        return version_;
    }
    void publish(std::shared_ptr<const resource> value) {
        std::shared_ptr<const resource> previous;
        {
            std::lock_guard lock(mutex_);
            if (version_ == std::numeric_limits<std::uint64_t>::max())
                throw std::overflow_error("Asset version overflow");
            previous = std::exchange(current_, std::move(value));
            ++version_;
        }
    }
    void require_ticket() const {
        if (requested == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("Asset update sequence overflow");
    }

  public:
    std::uint64_t requested{};
    bool decoding{};

  private:
    mutable std::mutex mutex_;
    std::shared_ptr<const resource> current_;
    std::uint64_t version_{};
};
class decoding_scope {
  public:
    explicit decoding_scope(bool& active) : active_(active) {
        if (active_)
            throw std::logic_error("Cyclic asset dependency");
        active_ = true;
    }
    ~decoding_scope() { active_ = false; }
    decoding_scope(const decoding_scope&) = delete;
    decoding_scope& operator=(const decoding_scope&) = delete;

  private:
    bool& active_;
};
}

template <class resource> class asset {
  public:
    asset() = default;
    std::shared_ptr<const resource> snapshot() const { return slot_ ? slot_->snapshot() : nullptr; }
    std::uint64_t version() const { return slot_ ? slot_->version() : 0; }
    explicit operator bool() const { return bool(snapshot()); }

  private:
    explicit asset(std::shared_ptr<detail::asset_slot<resource>> slot) : slot_(std::move(slot)) {}
    friend class asset_store;

  private:
    std::shared_ptr<detail::asset_slot<resource>> slot_;
};

template <class resource> class asset_update {
  public:
    bool ready() const { return decoded_.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    asset<resource> commit() {
        if (std::this_thread::get_id() != owner_)
            throw std::logic_error("Assets must be committed on their owning thread");
        auto value = decoded_.get();
        if (ticket_ != slot_->requested)
            throw std::logic_error("Asset update was superseded");
        if (!value)
            throw std::runtime_error("Asset loader returned an empty resource");
        slot_->publish(std::move(value));
        return handle_;
    }

  private:
    asset_update(asset<resource> handle, std::shared_ptr<detail::asset_slot<resource>> slot,
                 std::future<std::shared_ptr<const resource>> decoded, std::uint64_t ticket)
        : handle_(std::move(handle)), slot_(std::move(slot)), decoded_(std::move(decoded)), ticket_(ticket) {}
    friend class asset_store;

  private:
    asset<resource> handle_;
    std::shared_ptr<detail::asset_slot<resource>> slot_;
    mutable std::future<std::shared_ptr<const resource>> decoded_;
    std::uint64_t ticket_;
    std::thread::id owner_{std::this_thread::get_id()};
};

class asset_store {
  public:
    explicit asset_store(std::filesystem::path root = std::filesystem::current_path());
    void mount(std::string name, std::filesystem::path directory);
    std::filesystem::path resolve(const std::string& uri) const;
    template <class resource, class loader> asset<resource> load(const std::string& uri, loader&& decode) {
        require_owner();
        const auto path = resolve(uri);
        auto slot = find_slot<resource>(path);
        if (!slot->snapshot()) {
            slot->require_ticket();
            detail::decoding_scope scope(slot->decoding);
            std::shared_ptr<const resource> value = std::invoke(std::forward<loader>(decode), path);
            if (!value)
                throw std::runtime_error("Asset loader returned an empty resource");
            ++slot->requested;
            slot->publish(std::move(value));
        }
        return asset<resource>(std::move(slot));
    }
    template <class resource, class loader> asset<resource> reload(const std::string& uri, loader&& decode) {
        require_owner();
        auto path = resolve(uri);
        auto slot = find_slot<resource>(path);
        slot->require_ticket();
        detail::decoding_scope scope(slot->decoding);
        std::shared_ptr<const resource> value = std::invoke(std::forward<loader>(decode), path);
        if (!value)
            throw std::runtime_error("Asset loader returned an empty resource");
        ++slot->requested;
        slot->publish(std::move(value));
        return asset<resource>(std::move(slot));
    }
    template <class resource, class loader>
    asset_update<resource> prepare(const std::string& uri, job_system& jobs, loader&& decode) {
        require_owner();
        auto path = resolve(uri);
        auto slot = find_slot<resource>(path);
        slot->require_ticket();
        auto pending = jobs.submit(
            [path, decode = std::forward<loader>(decode)]() mutable -> std::shared_ptr<const resource> {
                return std::invoke(decode, path);
            });
        asset<resource> handle(slot);
        const auto ticket = ++slot->requested;
        return asset_update<resource>(std::move(handle), std::move(slot), std::move(pending), ticket);
    }
    std::size_t collect_unused();

  private:
    void require_owner() const;
    template <class resource>
    std::shared_ptr<detail::asset_slot<resource>> find_slot(const std::filesystem::path& path) {
        const auto key = std::pair{std::type_index(typeid(resource)), path};
        auto found = slots_.find(key);
        if (found != slots_.end())
            return std::static_pointer_cast<detail::asset_slot<resource>>(found->second);
        auto slot = std::make_shared<detail::asset_slot<resource>>();
        slots_.emplace(key, slot);
        return slot;
    }

  private:
    const std::thread::id owner_{std::this_thread::get_id()};
    std::map<std::string, std::filesystem::path, std::less<>> mounts_;
    std::map<std::pair<std::type_index, std::filesystem::path>, std::shared_ptr<detail::asset_slot_base>>
        slots_;
};
std::vector<std::byte> read_binary(const std::filesystem::path&,
                                   std::size_t maximum_bytes = 512 * 1024 * 1024);
std::string read_text(const std::filesystem::path&, std::size_t maximum_bytes = 16 * 1024 * 1024);
}
