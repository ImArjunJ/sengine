#pragma once
#include "input.hpp"
#include "timing.hpp"
#include <concepts>
#include <memory>
#include <utility>
#include <vector>

namespace sengine {
struct runtime_options {
    double fixed_step{1.0 / 60};
    unsigned max_steps{8};
};
struct runtime_step {
    std::uint64_t tick{};
    double seconds{};
};
struct runtime_frame {
    double seconds{};
    fixed_frame fixed;
};
class runtime_scene {
  public:
    virtual ~runtime_scene() = default;
    virtual void enter() {}
    virtual void leave() noexcept {}
    virtual void suspend() noexcept {}
    virtual void resume() noexcept {}
    virtual void begin_frame() {}
    virtual bool event(const input_event&) { return false; }
    virtual bool close_requested() { return true; }
    virtual void fixed_update(runtime_step) {}
    virtual void update(const runtime_frame&) {}
    virtual void render(const runtime_frame&) {}
    void bind(std::string action, std::vector<input_binding>, action_options = {});
    void unbind(const std::string& action);
    const input_map& input() const noexcept { return input_; }
    const input_map& fixed_input() const noexcept { return fixed_input_; }

  private:
    friend class runtime;
    void release_input();

  private:
    input_map input_, fixed_input_;
};
class runtime {
  public:
    explicit runtime(runtime_options = {});
    ~runtime();
    runtime(const runtime&) = delete;
    runtime& operator=(const runtime&) = delete;
    void push(std::unique_ptr<runtime_scene>);
    void replace(std::unique_ptr<runtime_scene>);
    template <std::derived_from<runtime_scene> scene, class... arguments>
    scene& emplace(arguments&&... values) {
        auto next = std::make_unique<scene>(std::forward<arguments>(values)...);
        auto& result = *next;
        push(std::move(next));
        return result;
    }
    void pop();
    void begin_frame();
    bool dispatch(const input_event&);
    void advance(double seconds);
    void render();
    void request_quit();
    void quit() noexcept { quitting_ = true; }
    bool finished() const noexcept { return quitting_ || (scenes_.empty() && pending_.empty()); }
    std::size_t depth() const noexcept { return scenes_.size(); }
    const runtime_frame& frame() const noexcept { return frame_; }
    void pause(bool value) noexcept { clock_.pause(value); }
    void speed(double value) { clock_.speed(value); }

  private:
    enum class transition_kind { push, replace, pop };
    struct transition {
        transition_kind kind;
        std::unique_ptr<runtime_scene> next;
    };
    void apply(transition);
    void close();

  private:
    fixed_clock clock_;
    runtime_frame frame_;
    std::vector<std::unique_ptr<runtime_scene>> scenes_;
    std::vector<transition> pending_;
    bool quitting_{}, dispatching_{}, closing_{};
};
}
