#pragma once

#include "Scene/Scene.h"
#include "NoTraceSession.h"
#include "NoTraceWorldRenderer.h"
#include <SFML/Audio/Sound.hpp>
#include <array>
#include <functional>
#include <memory>

namespace EngineTests { struct GuiAccess; }

namespace NoTrace {
  class Game : public Scene {
  public:
    explicit Game(EngineContext &context);
    void Start() override;
    void Update() override;
    void UpdateBackground() override;
    void Render() const override;
    void OnPause(bool paused) override;
    void OnCleanup() override;

  private:
    friend struct EngineTests::GuiAccess;
    enum Action { Left, Right, Up, Down, Run, Crouch, Prone, Crawl, Interact, Cqc, Lethal, NonLethal, Decoy, Knock, Drag, Tool, Orders, Lobby, Count };
    enum class Page { Connection, Operations, Armory, Intelligence, Deployment, Results, None };
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
    std::array<bool, Count> held_{};
    Page page_ = Page::Connection;
    GamePhase previousPhase_ = GamePhase::Base;
    bool paused_ = false;
    bool showOrders_ = false;
    bool ready_ = false;
    std::uint64_t lastAudioEvent_ = 0;
    std::unique_ptr<sf::Sound> cue_;

    void BindInputs();
    void Open(Page page);
    void ClosePanel();
    void BuildConnection();
    void BuildStation(Page page);
    void BuildResults();
    void RefreshHud();
    void PersistResults();
    void PlayCue(const std::string &file);
    const Agent *LocalAgent() const;
    bool Pressed(Action action, bool edge = false);
    tgui::Button::Ptr Button(const std::string &text, float y, std::function<void()> callback);
    tgui::Label::Ptr Label(const std::string &text, sf::Vector2f position, sf::Vector2f size, unsigned int textSize = 20);
  };
}
