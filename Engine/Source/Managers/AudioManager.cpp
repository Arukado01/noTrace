#include "Managers/AudioManager.h"
#include <SFML/Audio/Listener.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <algorithm>
#include <cmath>

namespace {
  float ValidVolume(float volume) { return std::isfinite(volume) ? std::clamp(volume, 0.f, 100.f) : 100.f; }
}

void AudioManager::SetGlobalVolume(float volume) {
  sf::Listener::setGlobalVolume(ValidVolume(volume));
}

float AudioManager::GetGlobalVolume() const {
  return sf::Listener::getGlobalVolume();
}

void AudioManager::RefreshVolumes() {
  std::erase_if(effects_, [this](const Effect &effect) {
    if (const auto sound = effect.sound.lock()) {
      sound->setVolume(effect.volume * GetBusVolume(effect.bus) / 100.f);
      return false;
    }
    return true;
  });
}

void AudioManager::SetBusVolume(Bus bus, float volume) {
  const auto index = static_cast<std::size_t>(bus);
  if (index >= busVolumes_.size()) return;
  busVolumes_[index] = ValidVolume(volume);
  RefreshVolumes();
}

float AudioManager::GetBusVolume(Bus bus) const {
  const auto index = static_cast<std::size_t>(bus);
  return index < busVolumes_.size() ? busVolumes_[index] : 0.f;
}

void AudioManager::SetEffectsVolume(float volume) { SetBusVolume(Bus::Effects, volume); }
float AudioManager::GetEffectsVolume() const { return GetBusVolume(Bus::Effects); }
void AudioManager::SetAmbienceVolume(float volume) { SetBusVolume(Bus::Ambience, volume); }
float AudioManager::GetAmbienceVolume() const { return GetBusVolume(Bus::Ambience); }
void AudioManager::SetMusicVolume(float volume) { SetBusVolume(Bus::Music, volume); }
float AudioManager::GetMusicVolume() const { return GetBusVolume(Bus::Music); }

void AudioManager::RegisterEffect(const std::shared_ptr<sf::Sound> &sound, float volume) {
  RegisterSource(sound, volume, Bus::Effects);
}

void AudioManager::RegisterSource(const std::shared_ptr<sf::SoundSource> &sound, float volume, Bus bus) {
  if (!sound || static_cast<std::size_t>(bus) >= busVolumes_.size()) return;
  std::erase_if(effects_, [](const Effect &effect) { return effect.sound.expired(); });
  const float base = ValidVolume(volume);
  const auto existing = std::find_if(effects_.begin(), effects_.end(), [&sound](const Effect &effect) { return effect.sound.lock() == sound; });
  if (existing != effects_.end()) { existing->volume = base; existing->bus = bus; }
  else effects_.push_back({sound, base, bus});
  sound->setVolume(base * GetBusVolume(bus) / 100.f);
}

void AudioManager::PruneVoices() {
  std::erase_if(voices_, [](const Voice &voice) { return voice.sound->getStatus() == sf::SoundSource::Status::Stopped; });
  std::erase_if(effects_, [](const Effect &effect) { return effect.sound.expired(); });
}

AudioManager::VoiceId AudioManager::Play(const sf::SoundBuffer &buffer, std::string_view group, const PlaybackSettings &settings) {
  if (buffer.getSampleCount() == 0 || settings.maximumInstances == 0 || static_cast<std::size_t>(settings.bus) >= busVolumes_.size()) return 0;
  PruneVoices();
  if (settings.loop) {
    const auto existing = std::ranges::find_if(voices_, [&](const Voice &voice) { return voice.loop && voice.group == group && voice.bus == settings.bus; });
    if (existing != voices_.end()) { SetVoiceVolume(existing->id, settings.volume); return existing->id; }
  }
  const auto copies = std::ranges::count_if(voices_, [&](const Voice &voice) { return voice.group == group && voice.bus == settings.bus; });
  if (static_cast<std::size_t>(copies) >= settings.maximumInstances) return 0;
  if (voices_.size() >= voiceLimit_) {
    const auto weakest = std::ranges::min_element(voices_, [](const Voice &first, const Voice &second) { return first.priority < second.priority; });
    if (weakest == voices_.end() || weakest->priority > settings.priority) return 0;
    weakest->sound->stop(); voices_.erase(weakest);
  }
  auto sound = std::make_shared<sf::Sound>(buffer);
  sound->setLooping(settings.loop);
  sound->setPitch(std::isfinite(settings.pitch) ? std::clamp(settings.pitch, 0.5f, 2.f) : 1.f);
  const float pan = std::isfinite(settings.pan) ? std::clamp(settings.pan, -1.f, 1.f) : 0.f;
  sound->setSpatializationEnabled(false); sound->setPan(pan);
  RegisterSource(sound, settings.volume, settings.bus);
  const auto id = nextVoice_++;
  voices_.push_back({id, sound, std::string(group), settings.bus, settings.priority, settings.loop});
  sound->play();
  return id;
}

bool AudioManager::SetVoiceVolume(VoiceId id, float volume) {
  const auto voice = std::ranges::find(voices_, id, &Voice::id);
  if (voice == voices_.end()) return false;
  RegisterSource(voice->sound, volume, voice->bus);
  return true;
}

bool AudioManager::IsPlaying(VoiceId id) const {
  return std::ranges::any_of(voices_, [id](const Voice &voice) { return voice.id == id && voice.sound->getStatus() != sf::SoundSource::Status::Stopped; });
}

void AudioManager::Stop(VoiceId id) {
  for (auto &voice : voices_) if (voice.id == id) voice.sound->stop();
  PruneVoices();
}

void AudioManager::StopGroup(std::string_view group) {
  for (auto &voice : voices_) if (voice.group == group) voice.sound->stop();
  PruneVoices();
}

void AudioManager::StopAll() {
  for (auto &voice : voices_) voice.sound->stop();
  PruneVoices();
}

void AudioManager::SetVoiceLimit(std::size_t limit) {
  voiceLimit_ = std::clamp<std::size_t>(limit, 1, 128);
  PruneVoices();
  while (voices_.size() > voiceLimit_) {
    const auto weakest = std::ranges::min_element(voices_, [](const Voice &first, const Voice &second) { return first.priority < second.priority; });
    weakest->sound->stop(); voices_.erase(weakest);
  }
}

std::size_t AudioManager::ActiveVoices(std::string_view group) const {
  return static_cast<std::size_t>(std::ranges::count_if(voices_, [group](const Voice &voice) { return (group.empty() || voice.group == group) && voice.sound->getStatus() != sf::SoundSource::Status::Stopped; }));
}

void AudioManager::SetListenerPosition(sf::Vector3f position) {
  sf::Listener::setPosition(position);
}

sf::Vector3f AudioManager::GetListenerPosition() const {
  return sf::Listener::getPosition();
}

void AudioManager::SetListenerDirection(sf::Vector3f direction) {
  sf::Listener::setDirection(direction);
}

sf::Vector3f AudioManager::GetListenerDirection() const {
  return sf::Listener::getDirection();
}
