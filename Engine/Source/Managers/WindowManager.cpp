#include "Managers/WindowManager.h"
#include "Core/EngineConfig.h"
#include "Managers/SaveManager.h"
#include "Utils/Log.h"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {
#ifdef _WIN32
  MONITORINFO PrimaryMonitor() {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
      const auto desktop = sf::VideoMode::getDesktopMode().size;
      monitor.rcMonitor = {0, 0, static_cast<LONG>(desktop.x), static_cast<LONG>(desktop.y)};
      monitor.rcWork = monitor.rcMonitor;
      SystemParametersInfoW(SPI_GETWORKAREA, 0, &monitor.rcWork, 0);
    }
    return monitor;
  }

  LONG_PTR WindowStyle(bool borderless) {
    return WS_CLIPSIBLINGS | WS_CLIPCHILDREN | (borderless ? WS_POPUP : WS_OVERLAPPEDWINDOW);
  }
#endif
}

WindowManager::WindowManager(sf::RenderWindow &window, SaveManager &save)
    : window_(window), save_(save) {}

void WindowManager::Initialize() {
  DiscoverResolutions();
  windowedResolution_ = FitWindowResolution(gConfig.windowResolution, gConfig.borderless);
  windowedBorderless_ = gConfig.borderless;
  windowedPosition_ = window_.getPosition();
  if (!icon_.loadFromFile(gConfig.windowIcon)) {
    LOG_WARNING("No se pudo cargar el icono de NO TRACE: {}", gConfig.windowIcon);
  }

  DisplaySettings settings = defaultDisplaySettings_;
  try {
    const DisplaySettings saved{
        {save_.Get(DisplayConfig::WidthSaveKey, settings.resolution.x),
         save_.Get(DisplayConfig::HeightSaveKey, settings.resolution.y)},
        save_.Get(DisplayConfig::FullscreenSaveKey, settings.fullscreen),
        save_.Get(DisplayConfig::BorderlessSaveKey, settings.borderless),
        save_.Get(DisplayConfig::VerticalSyncSaveKey, settings.verticalSync),
        save_.Get(DisplayConfig::FramerateLimitSaveKey, settings.framerateLimit)};
    if (IsValidSettings(saved)) {
      settings = saved;
    }
    const sf::Vector2u savedWindowed{
        save_.Get(DisplayConfig::WindowedWidthSaveKey, settings.fullscreen ? windowedResolution_.x : settings.resolution.x),
        save_.Get(DisplayConfig::WindowedHeightSaveKey, settings.fullscreen ? windowedResolution_.y : settings.resolution.y)};
    windowedBorderless_ = save_.Get(DisplayConfig::WindowedBorderlessSaveKey, settings.borderless);
    const auto desktop = sf::VideoMode::getDesktopMode().size;
    if (savedWindowed.x >= gConfig.minimumWindowSize.x && savedWindowed.y >= gConfig.minimumWindowSize.y &&
        savedWindowed.x <= desktop.x && savedWindowed.y <= desktop.y) {
      windowedResolution_ = FitWindowResolution(savedWindowed, windowedBorderless_);
    }
    RememberResolution(windowedResolution_);
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
        mode.size.y >= gConfig.minimumWindowSize.y &&
        FitWindowResolution(mode.size, false) == mode.size && !IsSupportedResolution(mode.size)) {
      availableResolutions_.push_back(mode.size);
    }
  }
  std::ranges::sort(availableResolutions_, [](sf::Vector2u left, sf::Vector2u right) {
    return static_cast<std::uint64_t>(left.x) * left.y <
           static_cast<std::uint64_t>(right.x) * right.y;
  });
  const auto desktop = sf::VideoMode::getDesktopMode().size;
  fullscreenAvailable_ = desktop.x >= gConfig.minimumWindowSize.x && desktop.y >= gConfig.minimumWindowSize.y;
  defaultDisplaySettings_ = {FitWindowResolution(gConfig.windowResolution, gConfig.borderless),
                            gConfig.fullscreen && fullscreenAvailable_, gConfig.borderless,
                            gConfig.verticalSync, gConfig.framerateLimit};
  if (defaultDisplaySettings_.fullscreen) {
    defaultDisplaySettings_.resolution = desktop;
    defaultDisplaySettings_.borderless = false;
  }
  if (defaultDisplaySettings_.framerateLimit != 0 &&
      (defaultDisplaySettings_.framerateLimit < 30 || defaultDisplaySettings_.framerateLimit > 360)) {
    defaultDisplaySettings_.framerateLimit = 60;
  }
  RememberResolution(FitWindowResolution(gConfig.windowResolution, false));
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

sf::Vector2u WindowManager::GetWindowedResolution() const { return windowedResolution_; }

bool WindowManager::IsSupportedResolution(sf::Vector2u resolution) const {
  return std::ranges::find(availableResolutions_, resolution) != availableResolutions_.end();
}

bool WindowManager::RequestDisplaySettings(DisplaySettings settings) {
  if (confirmationPending_ || requestedDisplaySettings_ || !IsValidSettings(settings) ||
      (!settings.fullscreen && !IsSupportedResolution(settings.resolution) && settings.resolution != windowedResolution_)) {
    return false;
  }
  if (settings.fullscreen) settings.resolution = sf::VideoMode::getDesktopMode().size;
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
    settings.resolution = sf::VideoMode::getDesktopMode().size;
    settings.borderless = false;
  } else {
    settings.resolution = windowedResolution_;
    settings.borderless = windowedBorderless_;
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
  if (!windowConfigured_) {
    if (icon_.getSize().x != 0 && icon_.getSize().y != 0) {
      window_.setIcon(icon_);
    }
    window_.setMinimumSize(FitWindowResolution(gConfig.minimumWindowSize, displaySettings_.borderless));
    window_.setKeyRepeatEnabled(false);
    window_.setMouseCursorVisible(false);
    windowConfigured_ = true;
  }
  if (appliedVerticalSync_ != displaySettings_.verticalSync) {
    window_.setVerticalSyncEnabled(displaySettings_.verticalSync);
    appliedVerticalSync_ = displaySettings_.verticalSync;
  }
  window_.setFramerateLimit(displaySettings_.verticalSync ? 0 : displaySettings_.framerateLimit);
}

bool WindowManager::ApplyWindowSettings(DisplaySettings settings) {
  const auto started = std::chrono::steady_clock::now();
  if (windowConfigured_ && !displaySettings_.fullscreen) {
    windowedPosition_ = window_.getPosition();
    windowedResolution_ = window_.getSize();
  }
  if (settings.fullscreen) {
    settings.resolution = sf::VideoMode::getDesktopMode().size;
  } else {
    settings.resolution = FitWindowResolution(settings.resolution, settings.borderless);
  }
  const bool applied = ApplyNativeWindowSettings(settings);
  if (applied) {
    displaySettings_ = settings;
    if (!settings.fullscreen) {
      windowedResolution_ = window_.getSize();
      RememberResolution(windowedResolution_);
    }
    ConfigureWindow();
    RefreshPresentation();
  }
  lastTransitionMilliseconds_ = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - started).count();
  return applied;
}

bool WindowManager::ApplyNativeWindowSettings(DisplaySettings &settings) {
  if (windowConfigured_ && settings.resolution == displaySettings_.resolution &&
      settings.fullscreen == displaySettings_.fullscreen && settings.borderless == displaySettings_.borderless) {
    return true;
  }
#ifdef _WIN32
  if (!window_.isOpen()) {
    window_.create(sf::VideoMode(settings.resolution), gConfig.windowTitle);
    windowRecreated_ = true;
    windowConfigured_ = false;
    appliedVerticalSync_.reset();
  }
  if (!window_.isOpen()) return false;
  const auto handle = window_.getNativeHandle();
  const auto previousStyle = GetWindowLongPtrW(handle, GWL_STYLE);
  const auto style = WindowStyle(settings.fullscreen || settings.borderless) | (previousStyle & WS_VISIBLE);
  SetLastError(ERROR_SUCCESS);
  if (SetWindowLongPtrW(handle, GWL_STYLE, style) == 0 && GetLastError() != ERROR_SUCCESS) return false;
  RECT frame{0, 0, static_cast<LONG>(settings.resolution.x), static_cast<LONG>(settings.resolution.y)};
  AdjustWindowRectEx(&frame, static_cast<DWORD>(style), FALSE,
                     static_cast<DWORD>(GetWindowLongPtrW(handle, GWL_EXSTYLE)));
  // El modo sin exclusividad conserva la ventana, el contexto y todos los recursos de SFML.
  if (!SetWindowPos(handle, nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                    SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
    SetWindowLongPtrW(handle, GWL_STYLE, previousStyle);
    return false;
  }
  window_.setSize(settings.resolution);
  if (settings.fullscreen) {
    const auto monitor = PrimaryMonitor();
    window_.setPosition({static_cast<int>(monitor.rcMonitor.left), static_cast<int>(monitor.rcMonitor.top)});
  } else {
    CenterWindow();
  }
#else
  if (!window_.isOpen() || settings.fullscreen != displaySettings_.fullscreen ||
      settings.borderless != displaySettings_.borderless) {
    window_.create(sf::VideoMode(settings.resolution), gConfig.windowTitle,
                   settings.borderless ? sf::Style::None : sf::Style::Default,
                   settings.fullscreen ? sf::State::Fullscreen : sf::State::Windowed);
    windowRecreated_ = true;
    windowConfigured_ = false;
    appliedVerticalSync_.reset();
  } else {
    window_.setSize(settings.resolution);
  }
  if (!window_.isOpen()) return false;
  settings.resolution = window_.getSize();
  if (!settings.fullscreen) CenterWindow();
#endif
  return true;
}

sf::Vector2u WindowManager::FitWindowResolution(sf::Vector2u resolution, bool borderless) const {
#ifdef _WIN32
  const auto monitor = PrimaryMonitor();
  RECT frame{};
  AdjustWindowRectEx(&frame, static_cast<DWORD>(WindowStyle(borderless)), FALSE, WS_EX_APPWINDOW);
  const auto width = static_cast<unsigned int>(std::max<LONG>(1, monitor.rcWork.right - monitor.rcWork.left - (frame.right - frame.left)));
  const auto height = static_cast<unsigned int>(std::max<LONG>(1, monitor.rcWork.bottom - monitor.rcWork.top - (frame.bottom - frame.top)));
  return {std::clamp(resolution.x, std::min(width, gConfig.minimumWindowSize.x), width),
          std::clamp(resolution.y, std::min(height, gConfig.minimumWindowSize.y), height)};
#else
  const auto desktop = sf::VideoMode::getDesktopMode().size;
  return {std::clamp(resolution.x, std::min(desktop.x, gConfig.minimumWindowSize.x), desktop.x),
          std::clamp(resolution.y, std::min(desktop.y, gConfig.minimumWindowSize.y), desktop.y)};
#endif
}

void WindowManager::CenterWindow() {
#ifdef _WIN32
  const auto monitor = PrimaryMonitor();
  RECT frame{};
  GetWindowRect(window_.getNativeHandle(), &frame);
  window_.setPosition({static_cast<int>(monitor.rcWork.left + std::max<LONG>(0, (monitor.rcWork.right - monitor.rcWork.left - (frame.right - frame.left)) / 2)),
                       static_cast<int>(monitor.rcWork.top + std::max<LONG>(0, (monitor.rcWork.bottom - monitor.rcWork.top - (frame.bottom - frame.top)) / 2))});
#else
  const auto desktop = sf::VideoMode::getDesktopMode().size;
  const auto size = window_.getSize();
  window_.setPosition({static_cast<int>((desktop.x - std::min(desktop.x, size.x)) / 2),
                       static_cast<int>((desktop.y - std::min(desktop.y, size.y)) / 2)});
#endif
  windowedPosition_ = window_.getPosition();
}

bool WindowManager::IsValidSettings(const DisplaySettings &settings) const {
  const auto desktop = sf::VideoMode::getDesktopMode().size;
  return !(settings.fullscreen && settings.borderless) && (!settings.fullscreen || fullscreenAvailable_) &&
      settings.resolution.x >= std::min(gConfig.minimumWindowSize.x, desktop.x) &&
      settings.resolution.y >= std::min(gConfig.minimumWindowSize.y, desktop.y) &&
      settings.resolution.x <= desktop.x && settings.resolution.y <= desktop.y &&
      (settings.framerateLimit == 0 || (settings.framerateLimit >= 30 && settings.framerateLimit <= 360));
}

void WindowManager::RememberResolution(sf::Vector2u resolution) {
  if (!IsSupportedResolution(resolution)) {
    availableResolutions_.push_back(resolution);
    std::ranges::sort(availableResolutions_, [](sf::Vector2u left, sf::Vector2u right) {
      return static_cast<std::uint64_t>(left.x) * left.y < static_cast<std::uint64_t>(right.x) * right.y;
    });
  }
}

void WindowManager::BeginDisplayChange(DisplaySettings settings) {
  previousDisplaySettings_ = displaySettings_;
  previousWindowedResolution_ = windowedResolution_;
  previousWindowedPosition_ = windowedPosition_;
  previousWindowedBorderless_ = windowedBorderless_;
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
    save_.Set(DisplayConfig::BorderlessSaveKey, displaySettings_.borderless);
    save_.Set(DisplayConfig::VerticalSyncSaveKey, displaySettings_.verticalSync);
    save_.Set(DisplayConfig::FramerateLimitSaveKey, displaySettings_.framerateLimit);
    save_.Set(DisplayConfig::WindowedWidthSaveKey, windowedResolution_.x);
    save_.Set(DisplayConfig::WindowedHeightSaveKey, windowedResolution_.y);
    save_.Set(DisplayConfig::WindowedBorderlessSaveKey, windowedBorderless_);
    LOG_INFO("Ajustes de pantalla confirmados");
  } else {
    ApplyWindowSettings(previousDisplaySettings_);
    windowedResolution_ = previousDisplaySettings_.fullscreen ? previousWindowedResolution_ : window_.getSize();
    windowedPosition_ = previousWindowedPosition_;
    windowedBorderless_ = previousWindowedBorderless_;
    if (!previousDisplaySettings_.fullscreen) {
      auto position = previousWindowPosition_;
#ifdef _WIN32
      const auto monitor = PrimaryMonitor();
      RECT frame{};
      GetWindowRect(window_.getNativeHandle(), &frame);
      position = {static_cast<int>(std::clamp<LONG>(position.x, monitor.rcWork.left,
                      std::max<LONG>(monitor.rcWork.left, monitor.rcWork.right - (frame.right - frame.left)))),
                  static_cast<int>(std::clamp<LONG>(position.y, monitor.rcWork.top,
                      std::max<LONG>(monitor.rcWork.top, monitor.rcWork.bottom - (frame.bottom - frame.top))))};
#endif
      window_.setPosition(position);
      windowedPosition_ = position;
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
    windowedBorderless_ = displaySettings_.borderless;
    RememberResolution(size);
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

double WindowManager::GetLastTransitionMilliseconds() const { return lastTransitionMilliseconds_; }

float WindowManager::GetConfirmationRemaining() const {
  if (!confirmationPending_) {
    return 0.f;
  }
  return std::max(0.f, gConfig.displayConfirmationSeconds - confirmationClock_.getElapsedTime().asSeconds());
}
