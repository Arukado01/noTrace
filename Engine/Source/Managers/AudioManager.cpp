#include "Managers/AudioManager.h"
#include "Managers/ResourceManager.h"
#include "Utils/Log.h"
#include <SFML/Audio/Listener.hpp>
#include <SFML/Audio/Music.hpp>
#include <SFML/Audio/Sound.hpp>
#include <SFML/Audio/SoundBuffer.hpp>
#include <algorithm>
#include <cmath>

namespace {
  float ValidVolume(float volume) { return std::isfinite(volume) ? std::clamp(volume, 0.f, 100.f) : 100.f; }
  float ValidSeconds(float seconds) { return std::isfinite(seconds) ? std::max(0.f, seconds) : 0.f; }
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

void AudioManager::StopAllVoices() {
  for (auto &voice : voices_) voice.sound->stop();
  PruneVoices();
}

void AudioManager::StopAll() {
  StopAllVoices(); StopMusic();
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

bool AudioManager::PlayMusic(ResourceManager &resources, const MusicSettings &settings) {
  if (settings.file.empty() || settings.file == settings.layerFile) return false;
  const auto load = [&](const std::string &file) -> std::shared_ptr<sf::Music> {
    if (const auto found = musicStreams_.find(file); found != musicStreams_.end()) return found->second;
    if (musicStreams_.size() >= MaximumMusicStreams) {
      const auto unused = std::ranges::find_if(musicStreams_, [&](const auto &entry) {
        return entry.second.use_count() == 1 && entry.first != requestedMusic_.file && entry.first != requestedMusic_.layerFile;
      });
      if (unused == musicStreams_.end()) return {};
      musicStreams_.erase(unused);
    }
    auto stream = resources.FetchMusic(file);
    if (!stream || stream->getDuration() <= sf::Time::Zero) return {};
    auto music = std::make_shared<sf::Music>(std::move(*stream));
    music->setLooping(true); music->setSpatializationEnabled(false); music->setVolume(0.f);
    musicStreams_.emplace(file, music); return music;
  };
  const auto music = load(settings.file);
  if (!music) return false;
  if (!settings.layerFile.empty()) {
    const auto layer = load(settings.layerFile);
    if (!layer) return false;
    if (std::abs((music->getDuration() - layer->getDuration()).asSeconds()) > .001f ||
        music->getSampleRate() != layer->getSampleRate() || music->getChannelCount() != layer->getChannelCount()) {
      LOG_ERROR("La capa musical no comparte duración ni formato con la base: {}", settings.layerFile);
      return false;
    }
  }
  requestedMusic_ = settings;
  requestedMusic_.volume = ValidVolume(settings.volume); requestedMusic_.layerVolume = ValidVolume(settings.layerVolume);
  requestedMusic_.fadeOutSeconds = ValidSeconds(settings.fadeOutSeconds); requestedMusic_.fadeInSeconds = ValidSeconds(settings.fadeInSeconds);
  requestedMusic_.intensitySeconds = ValidSeconds(settings.intensitySeconds);
  requestedMusic_.layerSyncToleranceSeconds = std::max(.01f, ValidSeconds(settings.layerSyncToleranceSeconds));
  if (!music_) BeginMusic();
  else if (musicSettings_.file == settings.file && musicSettings_.layerFile == settings.layerFile) {
    musicSettings_ = requestedMusic_;
    if (musicFade_ == MusicFade::Out) { musicFade_ = MusicFade::In; musicFadeSeconds_ = musicSettings_.fadeInSeconds; }
    RefreshMusicVolumes();
  } else if (musicFade_ != MusicFade::Out) {
    musicFade_ = MusicFade::Out; musicFadeSeconds_ = requestedMusic_.fadeOutSeconds;
    if (musicFadeSeconds_ == 0.f || musicGain_ == 0.f) { FinishMusic(); BeginMusic(); }
  }
  return true;
}

void AudioManager::RefreshMusicVolumes() {
  if (music_) RegisterSource(music_, musicSettings_.volume * musicGain_, Bus::Music);
  if (musicLayer_) RegisterSource(musicLayer_, musicSettings_.layerVolume * musicGain_ * musicIntensity_, Bus::Music);
}

void AudioManager::BeginMusic() {
  if (requestedMusic_.file.empty()) return;
  musicSettings_ = requestedMusic_;
  music_ = musicStreams_.at(musicSettings_.file);
  musicLayer_ = musicSettings_.layerFile.empty() ? nullptr : musicStreams_.at(musicSettings_.layerFile);
  musicGain_ = musicSettings_.fadeInSeconds > 0.f ? 0.f : 1.f; musicIntensity_ = 0.f;
  musicFade_ = musicGain_ < 1.f ? MusicFade::In : MusicFade::None; musicFadeSeconds_ = musicSettings_.fadeInSeconds;
  RefreshMusicVolumes();
  music_->setPlayingOffset(sf::Time::Zero); music_->play();
  if (musicLayer_) { musicLayer_->setPlayingOffset(music_->getPlayingOffset()); musicLayer_->play(); }
  ++musicStarts_;
  LOG_INFO("Música: {} | capa: {} | {:.2f}s", musicSettings_.file, musicSettings_.layerFile, music_->getDuration().asSeconds());
}

void AudioManager::FinishMusic() {
  if (music_) music_->stop();
  if (musicLayer_) musicLayer_->stop();
  music_.reset(); musicLayer_.reset(); musicSettings_ = {};
  musicGain_ = 0.f; musicIntensity_ = 0.f; musicFade_ = MusicFade::None;
}

void AudioManager::SetMusicIntensity(float intensity) {
  requestedIntensity_ = std::isfinite(intensity) ? std::clamp(intensity, 0.f, 1.f) : 0.f;
}

void AudioManager::StopMusic(float fadeOutSeconds) {
  requestedMusic_ = {}; requestedIntensity_ = 0.f;
  if (!music_) return;
  musicFadeSeconds_ = ValidSeconds(fadeOutSeconds);
  if (musicFadeSeconds_ == 0.f || musicGain_ == 0.f) FinishMusic();
  else musicFade_ = MusicFade::Out;
}

void AudioManager::Update(float deltaTime) {
  PruneVoices();
  if (!music_ || !std::isfinite(deltaTime) || deltaTime <= 0.f) return;
  float remaining = deltaTime;
  if (musicFade_ == MusicFade::Out) {
    const float consumed = std::min(remaining, musicGain_ * musicFadeSeconds_);
    musicGain_ = musicFadeSeconds_ > 0.f ? std::max(0.f, musicGain_ - consumed / musicFadeSeconds_) : 0.f;
    remaining -= consumed;
    if (musicGain_ <= .000001f) { FinishMusic(); BeginMusic(); }
  }
  if (music_ && musicFade_ == MusicFade::In) {
    musicGain_ = musicFadeSeconds_ > 0.f ? std::min(1.f, musicGain_ + remaining / musicFadeSeconds_) : 1.f;
    if (musicGain_ == 1.f) musicFade_ = MusicFade::None;
  }
  if (!music_) return;
  const float step = musicSettings_.intensitySeconds > 0.f ? deltaTime / musicSettings_.intensitySeconds : 1.f;
  musicIntensity_ += std::clamp(requestedIntensity_ - musicIntensity_, -step, step);
  if (musicLayer_) {
    const float duration = music_->getDuration().asSeconds();
    const float difference = std::abs(music_->getPlayingOffset().asSeconds() - musicLayer_->getPlayingOffset().asSeconds());
    // La distancia circular evita saltos falsos cuando un hilo cruza antes el límite del bucle.
    if (std::min(difference, std::max(0.f, duration - difference)) > musicSettings_.layerSyncToleranceSeconds)
      musicLayer_->setPlayingOffset(music_->getPlayingOffset());
  }
  RefreshMusicVolumes();
}

AudioManager::MusicState AudioManager::GetMusicState() const {
  MusicState result;
  result.file = musicSettings_.file; result.requestedFile = requestedMusic_.file; result.layerFile = musicSettings_.layerFile;
  result.transitioning = musicFade_ != MusicFade::None;
  result.gain = musicGain_; result.layerGain = musicGain_ * musicIntensity_;
  result.intensity = musicIntensity_; result.targetIntensity = requestedIntensity_;
  result.cachedStreams = musicStreams_.size(); result.starts = musicStarts_;
  for (const auto &[file, stream] : musicStreams_)
    if (stream != musicLayer_ && stream->getStatus() == sf::SoundSource::Status::Playing) ++result.mainStreamsPlaying;
  if (music_) {
    result.playing = music_->getStatus() == sf::SoundSource::Status::Playing; result.looping = music_->isLooping();
    result.volume = music_->getVolume(); result.offsetSeconds = music_->getPlayingOffset().asSeconds();
    result.durationSeconds = music_->getDuration().asSeconds();
  }
  if (musicLayer_) {
    result.layerPlaying = musicLayer_->getStatus() == sf::SoundSource::Status::Playing;
    result.layerVolume = musicLayer_->getVolume(); result.layerOffsetSeconds = musicLayer_->getPlayingOffset().asSeconds();
  }
  return result;
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
