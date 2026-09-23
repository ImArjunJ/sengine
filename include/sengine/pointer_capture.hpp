#pragma once
namespace sengine {
class pointer_capture {
  public:
    void focus(bool focused) {
        if (focused_ != focused)
            retry();
        focused_ = focused;
    }
    void request(bool requested) {
        if (requested_ != requested)
            retry();
        requested_ = requested;
    }
    bool captured() const noexcept { return focused_ && requested_ && !failed_; }
    void fail() noexcept { failed_ = true; }
    void retry() noexcept { failed_ = false; }

  private:
    bool focused_{}, requested_{}, failed_{};
};
}
