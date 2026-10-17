#pragma once

#include "Scene/Scene.h"
#include "NoTraceSession.h"
#include "NoTraceWorldRenderer.h"
#include "Types/DisplayTypes.h"
#include <SFML/Audio/Sound.hpp>
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
    enum Action { Left, Right, Up, Down, Run, Crouch, Prone, Interact, Cqc, Lethal, NonLethal, Decoy, Knock, Drag, Tool, Orders, Lobby, WallCover, Peek, Shove, Reload, SelectPistol, SelectNonLethal, Count };
    enum class Page { Connection, Operations, Armory, Intelligence, Deployment, Results, Pause, Options, Puzzle, None };
    Session session_;
    WorldRenderer worldRenderer_;
    tgui::Group::Ptr root_;
    tgui::Panel::Ptr panel_;
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
    tgui::ComboBox::Ptr resolutionBox_;
    tgui::ComboBox::Ptr modeBox_;
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
    std::vector<std::unique_ptr<sf::Sound>> voices_;
    std::uint64_t lastSound_ = 0;
    int pendingPuzzleInput_ = -1;
    unsigned int puzzleStageRendered_ = 0;
    std::uint32_t puzzleTargetRendered_ = 0;

    void BindInputs();
    void Open(Page page);
    void ClosePanel();
    void BuildConnection();
    void BuildStation(Page page);
    void BuildResults();
    void BuildPuzzle();
    void BuildPause();
    void BuildOptions();
    void SyncDisplayOptions();
    void NavigateBack();
    void RefreshHud();
    void PersistResults();
    void PlayCue(const std::string &file, float volume = 32.f, float pan = 0.f);
    const Agent *LocalAgent() const;
    bool Pressed(Action action, bool edge = false);
    tgui::Button::Ptr Button(const std::string &text, float y, std::function<void()> callback);
    tgui::Label::Ptr Label(const std::string &text, sf::Vector2f position, sf::Vector2f size, unsigned int textSize = 20);
  };
}
