#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <array>
#include <cstdint>
#include <optional>

struct EngineContext;

namespace NoTrace {
  class AudioPlayback {
    EngineContext &context_;
    std::array<std::size_t, static_cast<std::size_t>(SoundKind::Count)> lastVariants_;
    std::array<std::uint64_t, 4> ambience_{};
  public:
    explicit AudioPlayback(EngineContext &context);
    ~AudioPlayback();
    AudioPlayback(const AudioPlayback &) = delete;
    AudioPlayback &operator=(const AudioPlayback &) = delete;
    bool Play(SoundKind kind, float volume = 65.f, float pan = 0.f, float pitch = 1.f);
    bool PlayEvent(const SoundEvent &event, const WorldState &world, const Agent &listener, std::optional<sf::Vector2f> cameraRight = std::nullopt);
    void UpdateAmbience(const WorldState &world, PlayerId listener);
    void StopAmbience();
  };
}
