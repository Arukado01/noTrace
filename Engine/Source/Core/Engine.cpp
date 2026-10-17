#include "Core/Engine.h"
#include "Core/EngineConfig.h"
#include "Utils/Log.h"
#include <cassert>

Engine::Engine(std::string_view initialScene)
    : window_(sf::VideoMode(gConfig.windowResolution), gConfig.windowTitle),
      context_(window_),
      currentScene_(nullptr),
      overlay_(context_.gui),
      cursorWasVisible_(true) {
  context_.window.Initialize();
  SyncWindowPresentation();
  scenes_ = SceneFactory::CreateScenes(context_);

  if (gConfig.disableSfmlLogs) {
    sf::err().rdbuf(nullptr);
  }
  context_.audio.SetGlobalVolume(gConfig.globalVolume);
  const std::string sceneName(initialScene);
  context_.scenes.ChangeScene(scenes_.contains(sceneName) ? sceneName : "Menu");
  LOG_INFO("Window created");
}

bool Engine::IsRunning() const { return window_.isOpen(); }

bool Engine::HasFocus() const { return window_.hasFocus(); }

void Engine::SyncWindowPresentation() {
  if (context_.window.ConsumeWindowRecreated()) {
    context_.gui.RebindWindow();
  }
  if (context_.window.ConsumePresentationChange()) {
    context_.gui.SyncPresentation(context_.window.GetLogicalSize(), context_.window.GetViewport());
  }
}

void Engine::UpdateWindowServices() {
  if (const auto decision = context_.gui.FetchDisplayDecision()) {
    context_.window.ResolveDisplayChange(*decision);
  }
  const bool wasPending = context_.window.IsDisplayConfirmationPending();
  context_.window.Update();
  SyncWindowPresentation();
  const bool pending = context_.window.IsDisplayConfirmationPending();

  if (pending && !wasPending && currentScene_) {
    currentScene_->OnPause(true);
  }
  context_.gui.SetDisplayConfirmation(pending, context_.window.GetConfirmationRemaining());
  if (pending != wasPending) {
    context_.cursor.SetModalVisible(pending);
  }
  if (!pending && wasPending && currentScene_) {
    currentScene_->OnPause(overlay_.IsVisible() || !window_.hasFocus());
    if (overlay_.IsVisible()) {
      overlay_.SetVisible(true);
    }
  }
}

void Engine::ProcessEvent() {
  context_.input.BeginFrame();
  UpdateWindowServices();
  if (!window_.isOpen()) {
    return;
  }
  if (!context_.window.IsDisplayConfirmationPending()) {
    if (const auto nextScene = context_.scenes.FetchNextScene()) {
      EventSceneChange(*nextScene);
    }
  }

  while (const auto event = window_.pollEvent()) {
    const GlobalShortcut shortcut = context_.input.HandleGlobalEvent(
        *event, context_.window.IsDisplayConfirmationPending());
    if (shortcut != GlobalShortcut::None) {
      if (shortcut == GlobalShortcut::ToggleFullscreen) {
        context_.window.ToggleFullscreen();
      } else if (shortcut == GlobalShortcut::RevertDisplay) {
        context_.window.ResolveDisplayChange(false);
      }
      continue;
    }

    const auto *key = event->getIf<sf::Event::KeyPressed>();
    const bool typingShortcut = key && (key->scancode == sf::Keyboard::Scan::R || key->scancode == sf::Keyboard::Scan::M)
                                && context_.gui.IsTextInputFocused();
    if (!typingShortcut) {
      event->visit(EngineVisitor{*this});
    }
    if (!window_.isOpen()) {
      break;
    }
    SyncWindowPresentation();
    context_.input.CaptureEvent(*event, !overlay_.IsVisible() && !context_.window.IsDisplayConfirmationPending()
                                      && !context_.gui.IsTextInputFocused());
    context_.gui.ProcessEvent(*event);
    if (!overlay_.IsVisible() && !context_.window.IsDisplayConfirmationPending()) {
      currentScene_->OnEvent(*event);
    }
  }

  if (!context_.window.IsDisplayConfirmationPending()) {
    if (const auto selection = overlay_.FetchSelection()) {
      EventOverlaySelect(*selection);
    }
  }
  if (context_.scenes.FetchQuitRequest()) {
    EventWindowClose();
  }
  if (window_.isOpen()) {
    // La recreación espera a que terminen todos los eventos y callbacks de esta iteración.
    UpdateWindowServices();
  }
}

void Engine::Update() {
  context_.time.Update();
  if (window_.hasFocus()) context_.cursor.Update(context_.time.GetDeltaTime());
  if (window_.hasFocus() && !overlay_.IsVisible() && !context_.window.IsDisplayConfirmationPending()) {
    currentScene_->Update();
  }
  currentScene_->UpdateBackground();
}

void Engine::Render() {
  window_.setView(context_.window.GetPresentationView());
  window_.clear();
  context_.renderer.BeginDrawing();
  currentScene_->Render();
  window_.draw(sf::Sprite(context_.renderer.FinishDrawing()));
  context_.gui.Render();
  window_.setView(context_.window.GetPresentationView());
  context_.cursor.Render();
  window_.display();
}

void Engine::EventWindowClose() {
  window_.close();
  LOG_INFO("Window closed");
}

void Engine::EventWindowResized(sf::Vector2u size) {
  context_.window.HandleResize(size);
  LOG_INFO("Window resized to: {}x{}", size.x, size.y);
}

void Engine::EventWindowFocusLost() {
  if (currentScene_) {
    currentScene_->OnPause(true);
  }
  LOG_INFO("Window focus lost");
}

void Engine::EventWindowFocusGained() {
  context_.window.RefreshPresentation();
  if (currentScene_) {
    currentScene_->OnPause(overlay_.IsVisible() || context_.window.IsDisplayConfirmationPending());
  }
  LOG_INFO("Window focus gained");
}

void Engine::EventGamepadConnected(int id) {
  LOG_INFO("Gamepad {} connected", id);
}

void Engine::EventGamepadDisconnected(int id) {
  LOG_INFO("Gamepad {} disconnected", id);
}

void Engine::EventWindowScreenshot() const { context_.screenshot.Take(); }

void Engine::EventSceneChange(const std::string &name) {
  assert(scenes_.contains(name));
  Scene *nextScene = scenes_.at(name).get();
  if (currentScene_) {
    currentScene_->OnCleanup();
  }
  context_.input.Clear();
  currentScene_ = nextScene;
  currentScene_->Start();
}

void Engine::EventSceneRestart() {
  overlay_.SetVisible(false);
  context_.scenes.RestartCurrentScene();
}

void Engine::EventSceneMenuReturn() {
  overlay_.SetVisible(false);
  context_.scenes.ChangeScene("Menu");
  context_.cursor.SetVisible(true);
  context_.cursor.SetSpeed(gConfig.cursorSpeed);
}

void Engine::EventOverlayPauseToggle() {
  const bool visible = !overlay_.IsVisible();
  overlay_.SetVisible(visible);
  if (visible) {
    cursorWasVisible_ = context_.cursor.IsVisible();
  }
  context_.cursor.SetVisible(visible || cursorWasVisible_);
  currentScene_->OnPause(visible);
  LOG_INFO(visible ? "Game paused" : "Game resumed");
}

void Engine::EventOverlaySelect(OverlaySelection selection) {
  switch (selection) {
  case OverlaySelection::Resume:
    EventOverlayPauseToggle();
    break;
  case OverlaySelection::Restart:
    EventSceneRestart();
    break;
  case OverlaySelection::Menu:
    EventSceneMenuReturn();
    break;
  case OverlaySelection::Quit:
    EventWindowClose();
    break;
  default:
    break;
  }
}
