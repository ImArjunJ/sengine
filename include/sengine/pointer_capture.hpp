#pragma once
namespace sengine {
struct pointer_capture {
    bool focused{}, requested{}, failed{};
};
inline void focus(pointer_capture& state, bool focused) {
    if (state.focused != focused)
        state.failed = false;
    state.focused = focused;
}
inline void request_capture(pointer_capture& state, bool requested) {
    if (state.requested != requested)
        state.failed = false;
    state.requested = requested;
}
inline bool captured(const pointer_capture& state) {
    return state.focused && state.requested && !state.failed;
}
}
