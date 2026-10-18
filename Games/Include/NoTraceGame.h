#pragma once

#include "Scene/Scene.h"
#include "NoTraceSession.h"
#include "NoTraceWorldRenderer.h"
#include "NoTraceFirstPersonRenderer.h"
#include "NoTraceOptionsUi.h"
#include "NoTraceAudioPlayback.h"
#include "Config/NoTraceControlsConfig.h"
#include "Types/DisplayTypes.h"
#include <array>
#include <functional>
#include <memory>

namespace EngineTests { struct GuiAccess; }

namespace NoTrace {
  class Game : public Scene {
  public:
    explicit Game(EngineContext &context);
    bool UsesGlobalOverlay() const override { return false; }
    void Start() override;
    void Update() override;
    void UpdateBackground() override;
    void Render() const override;
    void OnEvent(const sf::Event &event) override;
    void OnPause(bool paused) override;
    void OnCleanup() override;

  private:
    friend struct EngineTests::GuiAccess;
    using Action = ControlsConfig::Action;
    using enum ControlsConfig::Action;
    enum class Page { Connection, Operations, Armory, Intelligence, Deployment, Results, Pause, Options, Puzzle, Weapons, Controls, None };
    Session session_;
    WorldRenderer worldRenderer_;
    FirstPersonRenderer firstPersonRenderer_;
    FirstPersonCamera firstPersonCamera_;
    AudioPlayback audioPlayback_;
    tgui::Group::Ptr root_;
    tgui::Panel::Ptr panel_;
    tgui::Panel::Ptr footer_;
    tgui::Label::Ptr title_;
    tgui::Label::Ptr mission_;
    tgui::Label::Ptr agent_;
    tgui::Label::Ptr message_;
    tgui::Label::Ptr hint_;
    tgui::Label::Ptr private_;
    tgui::Label::Ptr status_;
    tgui::Label::Ptr roster_;
    tgui::Label::Ptr briefing_;
    tgui::Button::Ptr deploy_;
    tgui::Button::Ptr lobby_;
    tgui::ProgressBar::Ptr health_;
    tgui::ProgressBar::Ptr stamina_;
    tgui::ProgressBar::Ptr suppressor_;
    tgui::ProgressBar::Ptr puzzleProgress_;
    std::array<tgui::Label::Ptr, 4> coordinationButtons_;
    tgui::Button::Ptr coordinationHold_;
    tgui::ComboBox::Ptr resolutionBox_;
    tgui::ComboBox::Ptr modeBox_;
    OptionsWidgets options_;
    DisplaySettings observedDisplay_;
    std::array<bool, Count> held_{};
    Page page_ = Page::Connection;
    GamePhase previousPhase_ = GamePhase::Base;
    bool paused_ = false;
    bool showOrders_ = false;
    bool ready_ = false;
    bool preparingDecoy_ = false;
    bool cancelledDecoy_ = false;
    bool cqcBlocked_ = false;
    bool firstPerson_ = false;
    bool perspectiveKeyDown_ = false;
    bool perspectivePulseConsumed_ = false;
    bool focused_ = true;
    bool firstPersonPointerReset_ = false;
    DisplaySettings firstPersonDisplay_;
    sf::Vector2f savedTacticalCursor_;
    sf::FloatRect firstPersonViewport_;
    std::uint64_t lastSound_ = 0;
    float lastNetworkTime_ = 0.f;
    MissionEvent puzzleFailure_;
    float puzzleFailureUntil_ = 0.f;
    GamePhase puzzleFailurePhase_ = GamePhase::Base;
    MissionEvent puzzleSuccess_;
    float puzzleSuccessUntil_ = 0.f;
    std::string puzzleSuccessText_;
    int pendingPuzzleInput_ = -1;
    bool pendingPuzzleHeld_ = false;
    int pendingWeaponChoice_ = -1;
    std::array<tgui::Button::Ptr, 2> weaponSlots_;
    std::array<tgui::Label::Ptr, 2> weaponAmmo_;
    unsigned int puzzleStageRendered_ = 0;
    std::uint32_t puzzleTargetRendered_ = 0;

    void BindInputs();
    void Open(Page page);
    void ClosePanel();
    void BuildConnection();
    void BuildStation(Page page);
    void BuildResults();
    void BuildPuzzle();
    void RefreshPuzzleControls();
    void BuildWeapons();
    void RefreshWeapons();
    void BuildControls();
    void BuildPause();
    void BuildOptions();
    void SyncDisplayOptions();
    void NavigateBack();
    void RefreshHud();
    void PersistResults();
    void PlayCue(const std::string &file, float volume = 32.f, float pan = 0.f, float pitch = 1.f);
    const Agent *LocalAgent() const;
    PlayerId ViewPlayer() const;
    bool Pressed(Action action, bool edge = false);
    bool CanUseFirstPerson() const;
    bool ToggleFirstPerson();
    void LeaveFirstPerson();
    void SyncFirstPersonInput();
    void UpdateFirstPersonLook();
    tgui::Button::Ptr Button(const std::string &text, float y, std::function<void()> callback);
    tgui::Label::Ptr Label(const std::string &text, sf::Vector2f position, sf::Vector2f size, unsigned int textSize = 20);
  };
}
