#pragma once

#include "Scene/Scene.h"
#include "NoTraceBackdrop.h"
#include "Types/DisplayTypes.h"
#include "Types/NoTraceTypes.h"

namespace NoTrace {
  class Menu : public Scene {
  private:
    Backdrop backdrop_;
    tgui::Group::Ptr root_;
    tgui::Group::Ptr mainPage_;
    tgui::Group::Ptr optionsPage_;
    tgui::Button::Ptr mainFocus_;
    tgui::ComboBox::Ptr resolutionBox_;
    tgui::ComboBox::Ptr modeBox_;
    tgui::Slider::Ptr volumeSlider_;
    tgui::Slider::Ptr cursorSlider_;
    tgui::Label::Ptr volumeLabel_;
    tgui::Label::Ptr cursorLabel_;
    tgui::Label::Ptr currentLabel_;
    tgui::Label::Ptr statusLabel_;
    MenuPage page_ = MenuPage::Main;
    DisplaySettings observedDisplay_;
    float fade_ = 0.f;

  public:
    explicit Menu(EngineContext &context);
    bool UsesGlobalOverlay() const override { return false; }
    void Start() override;
    void Update() override;
    void Render() const override;
    void OnEvent(const sf::Event &event) override;
    void OnPause(bool paused) override;
    void OnCleanup() override;

  private:
    void InitMainPage();
    void InitOptionsPage();
    void ShowPage(MenuPage page);
    void FocusPage();
    void SyncDisplayOptions();
    void ApplyDisplayOptions();
    void RestoreDefaults();
    void SetVolume(float volume);
    void SetCursorSpeed(float speed);
  };

  class Reception : public Scene {
  private:
    Backdrop backdrop_;
    tgui::Group::Ptr root_;
    tgui::Button::Ptr backButton_;
    float fade_ = 0.f;

  public:
    explicit Reception(EngineContext &context);
    bool UsesGlobalOverlay() const override { return false; }
    void Start() override;
    void Update() override;
    void Render() const override;
    void OnPause(bool paused) override;
    void OnCleanup() override;
  };
}
