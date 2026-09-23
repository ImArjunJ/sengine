#include "sengine/runtime.hpp"
#include <stdexcept>

namespace sengine {
namespace {
class callback_scope {
  public:
    explicit callback_scope(bool& active) : active_(active) {
        if (active_)
            throw std::logic_error("Cannot reenter the runtime from a scene callback");
        active_ = true;
    }
    ~callback_scope() { active_ = false; }
    callback_scope(const callback_scope&) = delete;
    callback_scope& operator=(const callback_scope&) = delete;

  private:
    bool& active_;
};
}
void runtime_scene::bind(std::string action, std::vector<input_binding> bindings, action_options options) {
    auto frame = input_;
    auto fixed = fixed_input_;
    frame.define(action, bindings, options);
    fixed.define(std::move(action), std::move(bindings), options);
    input_ = std::move(frame);
    fixed_input_ = std::move(fixed);
}
void runtime_scene::unbind(const std::string& action) {
    input_.erase(action);
    fixed_input_.erase(action);
}
void runtime_scene::release_input() {
    input_.release_all();
    input_.begin_frame();
    fixed_input_.release_all();
    fixed_input_.begin_frame();
}
runtime::runtime(runtime_options options) : clock_(options.fixed_step, options.max_steps) {}
runtime::~runtime() {
    while (!scenes_.empty()) {
        scenes_.back()->leave();
        scenes_.pop_back();
    }
}
void runtime::push(std::unique_ptr<runtime_scene> next) {
    if (!next)
        throw std::invalid_argument("Cannot push an empty scene");
    pending_.push_back({transition_kind::push, std::move(next)});
}
void runtime::replace(std::unique_ptr<runtime_scene> next) {
    if (!next)
        throw std::invalid_argument("Cannot replace with an empty scene");
    pending_.push_back({transition_kind::replace, std::move(next)});
}
void runtime::pop() {
    pending_.push_back({transition_kind::pop, {}});
}
void runtime::apply(transition change) {
    if (change.kind == transition_kind::pop) {
        if (scenes_.empty())
            return;
        scenes_.back()->leave();
        scenes_.pop_back();
        if (!scenes_.empty()) {
            scenes_.back()->release_input();
            scenes_.back()->resume();
        }
        return;
    }
    scenes_.reserve(scenes_.size() + 1);
    const auto pending_before = pending_.size();
    try {
        change.next->enter();
    } catch (...) {
        pending_.resize(pending_before);
        throw;
    }
    if (!scenes_.empty()) {
        if (change.kind == transition_kind::replace) {
            scenes_.back()->leave();
            scenes_.pop_back();
        } else {
            scenes_.back()->release_input();
            scenes_.back()->suspend();
        }
    }
    scenes_.push_back(std::move(change.next));
}
void runtime::begin_frame() {
    const callback_scope scope(dispatching_);
    auto changes = std::exchange(pending_, {});
    for (auto& change : changes)
        apply(std::move(change));
    if (!scenes_.empty()) {
        scenes_.back()->input_.begin_frame();
        scenes_.back()->begin_frame();
    }
}
void runtime::close() {
    const callback_scope scope(closing_);
    if (scenes_.empty() || scenes_.back()->close_requested())
        quit();
}
void runtime::request_quit() {
    if (dispatching_) {
        close();
        return;
    }
    const callback_scope scope(dispatching_);
    close();
}
bool runtime::dispatch(const input_event& event) {
    const callback_scope scope(dispatching_);
    if (scenes_.empty())
        return false;
    auto& scene = *scenes_.back();
    scene.input_.process(event);
    scene.fixed_input_.process(event);
    const bool consumed = scene.event(event);
    if (!consumed && (event.type == event_type::quit || event.type == event_type::close))
        close();
    return consumed;
}
void runtime::advance(double seconds) {
    const callback_scope scope(dispatching_);
    frame_ = {seconds, clock_.advance(seconds)};
    if (scenes_.empty())
        return;
    auto& scene = *scenes_.back();
    for (unsigned i = 0; i < frame_.fixed.steps; ++i) {
        scene.fixed_update({frame_.fixed.first_tick + i, frame_.fixed.step});
        scene.fixed_input_.begin_frame();
    }
    scene.update(frame_);
}
void runtime::render() {
    const callback_scope scope(dispatching_);
    if (!scenes_.empty())
        scenes_.back()->render(frame_);
}
}
