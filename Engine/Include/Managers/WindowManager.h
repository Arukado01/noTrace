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
  bool windowedBorderless_ = false;
  sf::Vector2u previousWindowedResolution_;
  sf::Vector2i previousWindowedPosition_;
  bool previousWindowedBorderless_ = false;
  sf::Vector2i previousWindowPosition_;
  sf::Image icon_;
  sf::View presentationView_;
  sf::Vector2u presentedWindowSize_;
  sf::Clock confirmationClock_;
  std::optional<bool> displayDecision_;
  bool presentationChanged_ = false;
  bool windowRecreated_ = false;
  bool windowConfigured_ = false;
  std::optional<bool> appliedVerticalSync_;
  double lastTransitionMilliseconds_ = 0.0;

public:
  WindowManager(sf::RenderWindow &window, SaveManager &save);
  void Initialize();
  const std::vector<sf::Vector2u> &GetAvailableResolutions() const;
  bool IsFullscreenAvailable() const;
  DisplaySettings GetDisplaySettings() const;
  DisplaySettings GetDefaultDisplaySettings() const;
  sf::Vector2u GetWindowedResolution() const;
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
  double GetLastTransitionMilliseconds() const;
  float GetConfirmationRemaining() const;

private:
  void DiscoverResolutions();
  void ConfigureWindow();
  bool ApplyWindowSettings(DisplaySettings settings);
  bool ApplyNativeWindowSettings(DisplaySettings &settings);
  void CenterWindow();
  sf::Vector2u FitWindowResolution(sf::Vector2u resolution, bool borderless) const;
  bool IsValidSettings(const DisplaySettings &settings) const;
  void RememberResolution(sf::Vector2u resolution);
  void BeginDisplayChange(DisplaySettings settings);
  void FinishDisplayChange(bool keep);
  bool IsSupportedResolution(sf::Vector2u resolution) const;
};
