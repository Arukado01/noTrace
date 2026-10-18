#pragma once
#include <SFML/System/Vector3.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sf { class Sound; class SoundBuffer; class SoundSource; }

class AudioManager {
public:
  enum class Bus { Effects, Ambience, Music, Count };
  enum class Priority { Background, Foley, Action, Important, Critical };
  using VoiceId = std::uint64_t;
  struct PlaybackSettings {
    Bus bus = Bus::Effects;
    Priority priority = Priority::Action;
    float volume = 100.f;
    float pitch = 1.f;
    float pan = 0.f;
    bool loop = false;
    std::size_t maximumInstances = 4;
  };

private:
  struct Effect {
    std::weak_ptr<sf::SoundSource> sound;
    float volume = 100.f;
    Bus bus = Bus::Effects;
  };
  struct Voice {
    VoiceId id;
    std::shared_ptr<sf::Sound> sound;
    std::string group;
    Bus bus;
    Priority priority;
    bool loop;
  };
  std::vector<Effect> effects_;
  std::vector<Voice> voices_;
  std::array<float, static_cast<std::size_t>(Bus::Count)> busVolumes_{100.f, 100.f, 100.f};
  std::size_t voiceLimit_ = 24;
  VoiceId nextVoice_ = 1;
  void RefreshVolumes();
  void PruneVoices();

public:
  void SetGlobalVolume(float volume);
  float GetGlobalVolume() const;
  void SetEffectsVolume(float volume);
  float GetEffectsVolume() const;
  void RegisterEffect(const std::shared_ptr<sf::Sound> &sound, float volume);
  void SetAmbienceVolume(float volume);
  float GetAmbienceVolume() const;
  void SetMusicVolume(float volume);
  float GetMusicVolume() const;
  void SetBusVolume(Bus bus, float volume);
  float GetBusVolume(Bus bus) const;
  void RegisterSource(const std::shared_ptr<sf::SoundSource> &sound, float volume, Bus bus);
  VoiceId Play(const sf::SoundBuffer &buffer, std::string_view group, const PlaybackSettings &settings);
  bool SetVoiceVolume(VoiceId id, float volume);
  bool IsPlaying(VoiceId id) const;
  void Stop(VoiceId id);
  void StopGroup(std::string_view group);
  void StopAll();
  void SetVoiceLimit(std::size_t limit);
  std::size_t GetVoiceLimit() const { return voiceLimit_; }
  std::size_t ActiveVoices(std::string_view group = {}) const;

  void SetListenerPosition(sf::Vector3f position);
  sf::Vector3f GetListenerPosition() const;

  void SetListenerDirection(sf::Vector3f direction);
  sf::Vector3f GetListenerDirection() const;
};
