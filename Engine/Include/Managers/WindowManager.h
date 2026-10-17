#pragma once

#include "Types/DisplayTypes.h"
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/View.hpp>
#include <SFML/System/Clock.hpp>
#include <optional>
#include <vector>

class SaveManager;

class WindowManager {
private:
  sf::RenderWindow &window_;
  SaveManager &save_;
  std::vector<sf::Vector2u> availableResolutions_;
  bool fullscreenAvailable_ = false;
  DisplaySettings displaySettings_;
  DisplaySettings defaultDisplaySettings_;
  std::optional<DisplaySettings> requestedDisplaySettings_;
  bool confirmationPending_ = false;
  DisplaySettings previousDisplaySettings_;
  sf::Vector2u windowedResolution_;
  sf::Vector2i windowedPosition_;
  sf::Vector2u previousWindowedResolution_;
  sf::Vector2i previousWindowedPosition_;
  sf::Vector2i previousWindowPosition_;
  sf::Image icon_;
  sf::View presentationView_;
  sf::Vector2u presentedWindowSize_;
  sf::Clock confirmationClock_;
  std::optional<bool> displayDecision_;
  bool presentationChanged_ = false;
  bool windowRecreated_ = false;

public:
  WindowManager(sf::RenderWindow &window, SaveManager &save);
  void Initialize();
  const std::vector<sf::Vector2u> &GetAvailableResolutions() const;
  bool IsFullscreenAvailable() const;
  DisplaySettings GetDisplaySettings() const;
  DisplaySettings GetDefaultDisplaySettings() const;
  bool RequestDisplaySettings(DisplaySettings settings);
  bool IsDisplayConfirmationPending() const;
  void ToggleFullscreen();
  void ResolveDisplayChange(bool keep);
  void Update();
  void HandleResize(sf::Vector2u size);
  void RefreshPresentation();
  const sf::View &GetPresentationView() const;
  sf::Vector2f GetLogicalSize() const;
  sf::FloatRect GetViewport() const;
  bool ConsumePresentationChange();
  bool ConsumeWindowRecreated();
  float GetConfirmationRemaining() const;

private:
  void DiscoverResolutions();
  void ConfigureWindow();
  bool ApplyWindowSettings(DisplaySettings settings);
  void BeginDisplayChange(DisplaySettings settings);
  void FinishDisplayChange(bool keep);
  bool IsSupportedResolution(sf::Vector2u resolution) const;
};
