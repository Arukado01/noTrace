#include "Core/EngineConfig.h"
#include <cassert>
#include <fstream>
#include <nlohmann/json.hpp>

EngineConfig::EngineConfig() {
  std::ifstream file("Content/Config.json");
  assert(file);

  nlohmann::json json = nlohmann::json::parse(file);
  windowTitle = json["windowTitle"];
  windowSize = {json["windowSize"][0], json["windowSize"][1]};
  windowResolution = {json["windowResolution"][0], json["windowResolution"][1]};
  minimumWindowSize = {json["minimumWindowSize"][0], json["minimumWindowSize"][1]};
  windowIcon = json["windowIcon"];
  fullscreen = json["fullscreen"];
  framerateLimit = json["framerateLimit"];
  displayConfirmationSeconds = json["displayConfirmationSeconds"];
  disableSfmlLogs = json["disableSfmlLogs"];
  maximumDeltaTime = sf::seconds(json["maximumDeltaTime"]);
  globalVolume = json["globalVolume"];
  backgroundColor = {
      json["backgroundColor"][0],
      json["backgroundColor"][1],
      json["backgroundColor"][2],
  };
  cursorRadius = json["cursorRadius"];
  cursorSpeed = json["cursorSpeed"];
  cursorTexture = json["cursorTexture"];
  cursorHoverTexture = json["cursorHoverTexture"];
  cursorSize = json["cursorSize"];
  cursorColor = {json["cursorColor"][0], json["cursorColor"][1],
                 json["cursorColor"][2]};
  joystickDeadzone = json["joystickDeadzone"];
}
