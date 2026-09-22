#pragma once
#include <filesystem>
#include <memory>
#include <span>
#include <vector>
namespace sengine {
struct audio_format {
    int rate{48000}, channels{2};
};
struct audio_clip {
    audio_format format;
    std::vector<float> samples;
};
audio_clip load_audio(const std::filesystem::path&);
class audio_stream {
  public:
    explicit audio_stream(bool enabled = true, audio_format format = {});
    ~audio_stream();
    audio_stream(const audio_stream&) = delete;
    audio_stream& operator=(const audio_stream&) = delete;
    explicit operator bool() const;
    int queued_samples() const;
    bool write(std::span<const float>);
    void clear();
    void volume(float);

  private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
