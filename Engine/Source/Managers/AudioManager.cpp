#include "Managers/AudioManager.h"
#include <SFML/Audio/Listener.hpp>
#include <SFML/Audio/Sound.hpp>
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

void AudioManager::SetEffectsVolume(float volume) {
  effectsVolume_ = ValidVolume(volume);
  std::erase_if(effects_, [this](const Effect &effect) {
    if (const auto sound = effect.sound.lock()) {
      sound->setVolume(effect.volume * effectsVolume_ / 100.f);
      return false;
    }
    return true;
  });
}

float AudioManager::GetEffectsVolume() const { return effectsVolume_; }

void AudioManager::RegisterEffect(const std::shared_ptr<sf::Sound> &sound, float volume) {
  if (!sound) return;
  std::erase_if(effects_, [](const Effect &effect) { return effect.sound.expired(); });
  const float base = ValidVolume(volume);
  const auto existing = std::find_if(effects_.begin(), effects_.end(), [&sound](const Effect &effect) { return effect.sound.lock() == sound; });
  if (existing != effects_.end()) existing->volume = base;
  else effects_.push_back({sound, base});
  sound->setVolume(base * effectsVolume_ / 100.f);
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
