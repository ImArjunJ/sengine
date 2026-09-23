#include "sengine/audio.hpp"
#include <SDL3/SDL.h>
namespace sengine {
struct audio_stream::impl {
    SDL_AudioStream* stream{};
    bool initialized{};

  public:
    ~impl() {
        if (stream)
            SDL_DestroyAudioStream(stream);
        if (initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};
audio_stream::audio_stream(bool enabled, audio_format format) : impl_(std::make_unique<impl>()) {
    if (!enabled || !SDL_InitSubSystem(SDL_INIT_AUDIO))
        return;
    impl_->initialized = true;
    SDL_AudioSpec spec{SDL_AUDIO_F32, format.channels, format.rate};
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (impl_->stream)
        SDL_ResumeAudioStreamDevice(impl_->stream);
}
audio_stream::~audio_stream() = default;
audio_stream::operator bool() const {
    return impl_->stream;
}
int audio_stream::queued_samples() const {
    const int bytes = impl_->stream ? SDL_GetAudioStreamQueued(impl_->stream) : -1;
    return bytes < 0 ? -1 : bytes / int(sizeof(float));
}
bool audio_stream::write(std::span<const float> data) {
    return impl_->stream && SDL_PutAudioStreamData(impl_->stream, data.data(), int(data.size_bytes()));
}
void audio_stream::clear() {
    if (impl_->stream)
        SDL_ClearAudioStream(impl_->stream);
}
void audio_stream::volume(float gain) {
    if (impl_->stream)
        SDL_SetAudioStreamGain(impl_->stream, gain);
}
audio_clip load_audio(const std::filesystem::path& path) {
    SDL_AudioSpec spec{};
    Uint8* bytes{};
    Uint32 length{};
    if (!SDL_LoadWAV(path.c_str(), &spec, &bytes, &length))
        return {};
    audio_clip result{{spec.freq, spec.channels}, {}};
    if (spec.format == SDL_AUDIO_F32)
        result.samples.assign(reinterpret_cast<float*>(bytes), reinterpret_cast<float*>(bytes + length));
    SDL_free(bytes);
    return result;
}
}
