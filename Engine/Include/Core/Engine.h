#pragma once

#include "Core/EngineContext.h"
#include "Core/EngineVisitor.h"
#include "Core/Overlay.h"
#include "Scene/SceneFactory.h"
#include <SFML/Graphics.hpp>
#include <string_view>

class Engine {
private:
  sf::RenderWindow window_;
  EngineContext context_;

  SceneFactory::Scenes scenes_;
  Scene *currentScene_;

  Overlay overlay_;
  bool cursorWasVisible_;

public:
  explicit Engine(std::string_view initialScene = "Menu");

  bool IsRunning() const;
  bool HasFocus() const;

  void ProcessEvent();
  void Update();
  void Render();

private:
  friend EngineVisitor;
  void SyncWindowPresentation();
  void UpdateWindowServices();
  void EventWindowClose();
  void EventWindowResized(sf::Vector2u size);
  void EventWindowFocusLost();
  void EventWindowFocusGained();
  void EventGamepadConnected(int id);
  void EventGamepadDisconnected(int id);
  void EventWindowScreenshot() const;
  void EventSceneChange(const std::string &name);
  void EventSceneRestart();
  void EventSceneMenuReturn();
  void EventOverlayPauseToggle();
  void EventOverlaySelect(OverlaySelection selection);
};
