#pragma once
#include <SFML/System/Vector3.hpp>
#include <memory>
#include <vector>

namespace sf { class Sound; }

class AudioManager {
private:
  struct Effect {
    std::weak_ptr<sf::Sound> sound;
    float volume = 100.f;
  };
  std::vector<Effect> effects_;
  float effectsVolume_ = 100.f;

public:
  void SetGlobalVolume(float volume);
  float GetGlobalVolume() const;
  void SetEffectsVolume(float volume);
  float GetEffectsVolume() const;
  void RegisterEffect(const std::shared_ptr<sf::Sound> &sound, float volume);

  void SetListenerPosition(sf::Vector3f position);
  sf::Vector3f GetListenerPosition() const;

  void SetListenerDirection(sf::Vector3f direction);
  sf::Vector3f GetListenerDirection() const;
};
