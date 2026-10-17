#include "Managers/WindowManager.h"
#include "Core/EngineConfig.h"
#include "Managers/SaveManager.h"
#include "Utils/Log.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>

WindowManager::WindowManager(sf::RenderWindow &window, SaveManager &save)
    : window_(window), save_(save) {}

void WindowManager::Initialize() {
  DiscoverResolutions();
  windowedResolution_ = window_.getSize();
  windowedPosition_ = window_.getPosition();
  if (!icon_.loadFromFile(gConfig.windowIcon)) {
    LOG_WARNING("No se pudo cargar el icono de NO TRACE: {}", gConfig.windowIcon);
  }

  DisplaySettings settings = defaultDisplaySettings_;
  try {
    const DisplaySettings saved{
        {save_.Get(DisplayConfig::WidthSaveKey, settings.resolution.x),
         save_.Get(DisplayConfig::HeightSaveKey, settings.resolution.y)},
        save_.Get(DisplayConfig::FullscreenSaveKey, settings.fullscreen)};
    const sf::Vector2u desktop = sf::VideoMode::getDesktopMode().size;
    const bool supportedWindow = fullscreenAvailable_ && !saved.fullscreen &&
        saved.resolution.x >= gConfig.minimumWindowSize.x &&
        saved.resolution.y >= gConfig.minimumWindowSize.y &&
        saved.resolution.x <= desktop.x && saved.resolution.y <= desktop.y;
    if ((!saved.fullscreen || fullscreenAvailable_) &&
        (supportedWindow || IsSupportedResolution(saved.resolution))) {
      settings = saved;
    }
  } catch (const nlohmann::json::exception &error) {
    LOG_WARNING("Ajustes de pantalla inválidos; se usan los predeterminados: {}", error.what());
  }
  if (!ApplyWindowSettings(settings)) {
    LOG_ERROR("No se pudo inicializar la ventana de NO TRACE");
  }
}

void WindowManager::DiscoverResolutions() {
  availableResolutions_.clear();
  for (const auto &mode : sf::VideoMode::getFullscreenModes()) {
    if (mode.bitsPerPixel == 32 &&
        mode.size.x >= gConfig.minimumWindowSize.x &&
        mode.size.y >= gConfig.minimumWindowSize.y && !IsSupportedResolution(mode.size)) {
      availableResolutions_.push_back(mode.size);
    }
  }
  std::ranges::sort(availableResolutions_, [](sf::Vector2u left, sf::Vector2u right) {
    return static_cast<std::uint64_t>(left.x) * left.y <
           static_cast<std::uint64_t>(right.x) * right.y;
  });
  fullscreenAvailable_ = !availableResolutions_.empty();
  defaultDisplaySettings_ = {gConfig.windowResolution, gConfig.fullscreen};
  if (!availableResolutions_.empty() && !IsSupportedResolution(gConfig.windowResolution)) {
    defaultDisplaySettings_.resolution = availableResolutions_.front();
  }
  if (!fullscreenAvailable_) {
    availableResolutions_.push_back(window_.getSize());
    defaultDisplaySettings_.resolution = window_.getSize();
    defaultDisplaySettings_.fullscreen = false;
    LOG_WARNING("SFML no ofrece modos compatibles; se conserva el tamaño real de la ventana: {}x{}",
                defaultDisplaySettings_.resolution.x, defaultDisplaySettings_.resolution.y);
  }
  displaySettings_ = defaultDisplaySettings_;
  LOG_INFO("Resoluciones disponibles: {}; pantalla completa disponible: {}",
           availableResolutions_.size(), fullscreenAvailable_);
}

const std::vector<sf::Vector2u> &WindowManager::GetAvailableResolutions() const {
  return availableResolutions_;
}

bool WindowManager::IsFullscreenAvailable() const { return fullscreenAvailable_; }

DisplaySettings WindowManager::GetDisplaySettings() const { return displaySettings_; }

DisplaySettings WindowManager::GetDefaultDisplaySettings() const {
  return defaultDisplaySettings_;
}

bool WindowManager::IsSupportedResolution(sf::Vector2u resolution) const {
  return std::ranges::find(availableResolutions_, resolution) != availableResolutions_.end();
}

bool WindowManager::RequestDisplaySettings(DisplaySettings settings) {
  if (confirmationPending_ || requestedDisplaySettings_ ||
      (settings.fullscreen && !fullscreenAvailable_) || !IsSupportedResolution(settings.resolution)) {
    return false;
  }
  if (settings != displaySettings_) {
    requestedDisplaySettings_ = settings;
  }
  return true;
}

bool WindowManager::IsDisplayConfirmationPending() const { return confirmationPending_; }

void WindowManager::ToggleFullscreen() {
  if (confirmationPending_ || requestedDisplaySettings_ || !fullscreenAvailable_) {
    return;
  }
  DisplaySettings settings = displaySettings_;
  settings.fullscreen = !settings.fullscreen;
  if (settings.fullscreen) {
    if (!IsSupportedResolution(settings.resolution)) {
      settings.resolution = sf::VideoMode::getDesktopMode().size;
    }
    if (!IsSupportedResolution(settings.resolution)) {
      return;
    }
  } else {
    settings.resolution = windowedResolution_;
  }
  // El regreso por atajo conserva los tamaños elegidos al redimensionar manualmente la ventana.
  requestedDisplaySettings_ = settings;
}

void WindowManager::ResolveDisplayChange(bool keep) {
  if (confirmationPending_) {
    displayDecision_ = keep;
  }
}

void WindowManager::Update() {
  if (!window_.isOpen()) {
    return;
  }
  if (window_.getSize() != presentedWindowSize_) {
    RefreshPresentation();
  }
  if (!displaySettings_.fullscreen) {
    windowedPosition_ = window_.getPosition();
  }
  if (confirmationPending_) {
    if (GetConfirmationRemaining() <= 0.f) {
      displayDecision_ = false;
    }
    if (displayDecision_) {
      FinishDisplayChange(*displayDecision_);
    }
  } else if (const auto settings = std::exchange(requestedDisplaySettings_, std::nullopt)) {
    BeginDisplayChange(*settings);
  }
}

void WindowManager::ConfigureWindow() {
  if (icon_.getSize().x != 0 && icon_.getSize().y != 0) {
    window_.setIcon(icon_);
  }
  window_.setMinimumSize(gConfig.minimumWindowSize);
  window_.setKeyRepeatEnabled(false);
  window_.setMouseCursorVisible(false);
  window_.setFramerateLimit(gConfig.framerateLimit);
}

bool WindowManager::ApplyWindowSettings(DisplaySettings settings) {
  if (!displaySettings_.fullscreen) {
    windowedPosition_ = window_.getPosition();
    windowedResolution_ = window_.getSize();
  }
  window_.create(sf::VideoMode(settings.resolution), gConfig.windowTitle, sf::Style::Default,
                 settings.fullscreen ? sf::State::Fullscreen : sf::State::Windowed);
  if (!window_.isOpen()) {
    return false;
  }
  if (!settings.fullscreen) {
    window_.setPosition(windowedPosition_);
    windowedResolution_ = settings.resolution;
  }
  displaySettings_ = {window_.getSize(), settings.fullscreen};
  ConfigureWindow();
  RefreshPresentation();
  windowRecreated_ = true;
  return true;
}

void WindowManager::BeginDisplayChange(DisplaySettings settings) {
  previousDisplaySettings_ = displaySettings_;
  previousWindowedResolution_ = windowedResolution_;
  previousWindowedPosition_ = windowedPosition_;
  previousWindowPosition_ = window_.getPosition();
  displayDecision_.reset();
  if (!ApplyWindowSettings(settings)) {
    ApplyWindowSettings(previousDisplaySettings_);
    LOG_WARNING("No se pudo aplicar el cambio de pantalla; se restauró la configuración anterior");
    return;
  }
  confirmationPending_ = true;
  confirmationClock_.restart();
  LOG_INFO("Cambio de pantalla provisional: {}x{}, pantalla completa {}",
           settings.resolution.x, settings.resolution.y, settings.fullscreen);
}

void WindowManager::FinishDisplayChange(bool keep) {
  if (keep) {
    save_.Set(DisplayConfig::WidthSaveKey, displaySettings_.resolution.x);
    save_.Set(DisplayConfig::HeightSaveKey, displaySettings_.resolution.y);
    save_.Set(DisplayConfig::FullscreenSaveKey, displaySettings_.fullscreen);
    LOG_INFO("Ajustes de pantalla confirmados");
  } else {
    ApplyWindowSettings(previousDisplaySettings_);
    windowedResolution_ = previousWindowedResolution_;
    windowedPosition_ = previousWindowedPosition_;
    if (!previousDisplaySettings_.fullscreen) {
      window_.setPosition(previousWindowPosition_);
      windowedPosition_ = previousWindowPosition_;
    }
    LOG_INFO("Ajustes de pantalla revertidos");
  }
  confirmationPending_ = false;
  displayDecision_.reset();
}

void WindowManager::HandleResize(sf::Vector2u size) {
  if (size.x != 0 && size.y != 0) {
    RefreshPresentation();
  }
}

void WindowManager::RefreshPresentation() {
  const sf::Vector2u size = window_.getSize();
  if (size.x == 0 || size.y == 0) {
    return;
  }
  const sf::Vector2f logicalSize = GetLogicalSize();
  const float windowRatio = static_cast<float>(size.x) / size.y;
  const float logicalRatio = logicalSize.x / logicalSize.y;
  sf::FloatRect viewport({0.f, 0.f}, {1.f, 1.f});
  if (windowRatio > logicalRatio) {
    viewport.size.x = logicalRatio / windowRatio;
    viewport.position.x = (1.f - viewport.size.x) / 2.f;
  } else {
    viewport.size.y = windowRatio / logicalRatio;
    viewport.position.y = (1.f - viewport.size.y) / 2.f;
  }
  // SFML redondea el viewport a píxeles; la GUI puede compartir esos mismos bordes.
  viewport.position = {std::round(viewport.position.x * size.x) / size.x,
                       std::round(viewport.position.y * size.y) / size.y};
  viewport.size = {std::round(viewport.size.x * size.x) / size.x,
                   std::round(viewport.size.y * size.y) / size.y};
  presentationView_ = sf::View(sf::FloatRect({0.f, 0.f}, logicalSize));
  presentationView_.setViewport(viewport);
  window_.setView(presentationView_);
  presentedWindowSize_ = size;
  if (!displaySettings_.fullscreen) {
    displaySettings_.resolution = size;
    windowedResolution_ = size;
  }
  presentationChanged_ = true;
}

const sf::View &WindowManager::GetPresentationView() const { return presentationView_; }

sf::Vector2f WindowManager::GetLogicalSize() const { return gConfig.windowSize; }

sf::FloatRect WindowManager::GetViewport() const { return presentationView_.getViewport(); }

bool WindowManager::ConsumePresentationChange() {
  return std::exchange(presentationChanged_, false);
}

bool WindowManager::ConsumeWindowRecreated() {
  return std::exchange(windowRecreated_, false);
}

float WindowManager::GetConfirmationRemaining() const {
  if (!confirmationPending_) {
    return 0.f;
  }
  return std::max(0.f, gConfig.displayConfirmationSeconds - confirmationClock_.getElapsedTime().asSeconds());
}
