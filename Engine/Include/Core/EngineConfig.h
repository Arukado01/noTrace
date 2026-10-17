#pragma once
#include <SFML/Graphics/Color.hpp>
#include <SFML/System/Time.hpp>
#include <SFML/System/Vector2.hpp>
#include <string>
#include <string_view>

namespace DisplayConfig {
inline constexpr std::string_view WidthSaveKey = "NoTrace:DisplayWidth";
inline constexpr std::string_view HeightSaveKey = "NoTrace:DisplayHeight";
inline constexpr std::string_view FullscreenSaveKey = "NoTrace:Fullscreen";
}

struct EngineConfig {
  std::string windowTitle;
  sf::Vector2f windowSize;
  sf::Vector2u windowResolution;
  sf::Vector2u minimumWindowSize;
  std::string windowIcon;
  bool fullscreen;
  unsigned int framerateLimit;
  float displayConfirmationSeconds;
  bool disableSfmlLogs;
  sf::Time maximumDeltaTime;
  float globalVolume;
  sf::Color backgroundColor;
  float cursorRadius;
  float cursorSpeed;
  std::string cursorTexture;
  std::string cursorHoverTexture;
  float cursorSize;
  sf::Color cursorColor;
  float joystickDeadzone;

  EngineConfig();
};

inline const EngineConfig gConfig;
