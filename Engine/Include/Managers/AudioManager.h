#pragma once
#include <SFML/System/Vector3.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace sf { class Music; class Sound; class SoundBuffer; class SoundSource; }
class ResourceManager;

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
  struct MusicSettings {
    std::string file;
    std::string layerFile;
    float volume = 100.f;
    float layerVolume = 100.f;
    float fadeOutSeconds = 1.f;
    float fadeInSeconds = 1.5f;
    float intensitySeconds = 2.5f;
    float layerSyncToleranceSeconds = .08f;
  };
  struct MusicState {
    std::string file;
    std::string requestedFile;
    std::string layerFile;
    bool playing = false;
    bool layerPlaying = false;
    bool transitioning = false;
    bool looping = false;
    float gain = 0.f;
    float layerGain = 0.f;
    float intensity = 0.f;
    float targetIntensity = 0.f;
    float volume = 0.f;
    float layerVolume = 0.f;
    float offsetSeconds = 0.f;
    float layerOffsetSeconds = 0.f;
    float durationSeconds = 0.f;
    std::size_t mainStreamsPlaying = 0;
    std::size_t cachedStreams = 0;
    std::uint64_t starts = 0;
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
  static constexpr std::size_t MaximumMusicStreams = 6;
  std::unordered_map<std::string, std::shared_ptr<sf::Music>> musicStreams_;
  std::shared_ptr<sf::Music> music_;
  std::shared_ptr<sf::Music> musicLayer_;
  MusicSettings musicSettings_;
  MusicSettings requestedMusic_;
  enum class MusicFade { None, Out, In };
  MusicFade musicFade_ = MusicFade::None;
  float musicGain_ = 0.f;
  float musicIntensity_ = 0.f;
  float requestedIntensity_ = 0.f;
  float musicFadeSeconds_ = 0.f;
  std::uint64_t musicStarts_ = 0;
  void RefreshVolumes();
  void PruneVoices();
  void BeginMusic();
  void FinishMusic();
  void RefreshMusicVolumes();

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
  void StopAllVoices();
  void StopAll();
  void SetVoiceLimit(std::size_t limit);
  std::size_t GetVoiceLimit() const { return voiceLimit_; }
  std::size_t ActiveVoices(std::string_view group = {}) const;
  bool PlayMusic(ResourceManager &resources, const MusicSettings &settings);
  void SetMusicIntensity(float intensity);
  void StopMusic(float fadeOutSeconds = 0.f);
  void Update(float deltaTime);
  MusicState GetMusicState() const;

  void SetListenerPosition(sf::Vector3f position);
  sf::Vector3f GetListenerPosition() const;

  void SetListenerDirection(sf::Vector3f direction);
  sf::Vector3f GetListenerDirection() const;
};
