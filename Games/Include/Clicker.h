#pragma once

#include "Types/ClickerTypes.h"

namespace EngineTests { struct GuiAccess; }

namespace Clicker {
  class Game : public Scene {
  private:
    friend struct EngineTests::GuiAccess;
    Target target;
    Stats stats;

  public:
    Game(EngineContext &);

    void Start();
    void Update();
    void Render() const;
    void OnPause(bool);

  private:
    void InitTarget();
    void InitStats();

    void StartInputs();

    void StartTarget();
    void StartStats();

    void UpdateStats();
    void UpdateTarget();
    void UpdateTargetColor();

    void EventTargetClicked();
    void EventTargetTeleport();
    void EventStatsScoreIncrease();

    bool IsTargetHovered() const;
  };
} // namespace Clicker
