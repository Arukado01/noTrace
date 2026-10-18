#include "NoTraceGame.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceMusicConfig.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTracePreparationConfig.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceDistractionConfig.h"
#include "Config/NoTraceFirstPersonConfig.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceInteraction.h"
#include "NoTraceTraversal.h"
#include "Core/EngineConfig.h"
#include "Utils/Log.h"
#include "Utils/InputBindings.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>

namespace NoTrace {
  namespace {
    std::string StanceName(Stance stance) {
      switch (stance) {
      case Stance::Crouched: return "AGACHADO";
      case Stance::Prone: return "TUMBADO";
      default: return "DE PIE";
      }
    }
  }

  Game::Game(EngineContext &context)
      : Scene(context), session_(ctx.network, ctx.random), worldRenderer_(context), firstPersonRenderer_(context), audioPlayback_(context) {
    root_ = tgui::Group::create({"100%", "100%"});
    auto header = tgui::Panel::create({"100%", 116.f});
    header->getRenderer()->setBackgroundColor({5, 12, 20, 235});
    header->getRenderer()->setBorders(0);
    root_->add(header);
    auto label = [&](const std::string &text, float x, float y, float width, unsigned int size) {
      auto result = tgui::Label::create(text);
      result->setPosition({x, y}); result->setSize({width, 36.f}); result->setTextSize(size);
      result->getRenderer()->setTextColor(TEXT_COLOR); result->setIgnoreMouseEvents(true);
      root_->add(result); return result;
    };
    title_ = label("NO TRACE  //  CIO", 30.f, 13.f, 970.f, 26);
    mission_ = label("", 30.f, 56.f, 960.f, 18);
    agent_ = label("", 30.f, 83.f, 1540.f, 16);
    lobby_ = tgui::Button::create("BASE / SALA  [F2]");
    lobby_->setPosition({1260.f, 22.f}); lobby_->setSize({310.f, 50.f}); lobby_->setTextSize(20);
    lobby_->onPress([this] {
      if (!session_.IsConnected()) Open(Page::Connection);
      else if (session_.GetWorld().phase == GamePhase::Base) Open(Page::Deployment);
      else if (session_.GetWorld().phase == GamePhase::Mission) Open(Page::Pause);
    });
    root_->add(lobby_);
    const auto indicator = [this](float y, tgui::Color color) {
      auto bar = tgui::ProgressBar::create();
      bar->setPosition({1010.f, y}); bar->setSize({220.f, 28.f}); bar->setTextSize(16);
      bar->getRenderer()->setFillColor(color); bar->getRenderer()->setBackgroundColor({12, 22, 32});
      bar->getRenderer()->setTextColor(TEXT_COLOR); bar->getRenderer()->setTextColorFilled(tgui::Color::White);
      bar->getRenderer()->setBorderColor({84, 102, 117}); bar->setIgnoreMouseEvents(true);
      root_->add(bar); return bar;
    };
    health_ = indicator(16.f, {148, 44, 59});
    stamina_ = indicator(56.f, {37, 103, 97});
    suppressor_ = indicator(87.f, {84, 99, 111}); suppressor_->setSize({220.f, 22.f}); suppressor_->setTextSize(13);
    footer_ = tgui::Panel::create({"100%", 80.f});
    footer_->setPosition({0.f, 820.f}); footer_->getRenderer()->setBackgroundColor({5, 12, 20, 215});
    footer_->getRenderer()->setBorders(0); root_->add(footer_);
    hint_ = label("", 30.f, 828.f, 1530.f, 20);
    message_ = label("", 30.f, 864.f, 1530.f, 17);
    private_ = tgui::Label::create(); private_->setPosition({34.f, 140.f}); private_->setSize({620.f, 115.f});
    private_->setTextSize(19); private_->getRenderer()->setTextColor({239, 203, 117});
    private_->setIgnoreMouseEvents(true); private_->setVisible(false); root_->add(private_);
    panel_ = tgui::Panel::create({780.f, 596.f}); panel_->setPosition({410.f, 135.f});
    panel_->getRenderer()->setBackgroundColor({7, 17, 27, 250});
    panel_->getRenderer()->setBorderColor(ACCENT_COLOR); panel_->getRenderer()->setBorders({3.f, 1.f, 1.f, 1.f});
    root_->add(panel_);
  }

  tgui::Label::Ptr Game::Label(const std::string &text, sf::Vector2f position, sf::Vector2f size, unsigned int textSize) {
    auto value = tgui::Label::create(tgui::String(text));
    value->setPosition({position.x, position.y}); value->setSize({size.x, size.y}); value->setTextSize(textSize);
    value->getRenderer()->setTextColor(TEXT_COLOR); value->setIgnoreMouseEvents(true); panel_->add(value); return value;
  }

  tgui::Button::Ptr Game::Button(const std::string &text, float y, std::function<void()> callback) {
    auto button = tgui::Button::create(tgui::String(text));
    button->setPosition({30.f, y}); button->setSize({714.f, 48.f}); button->setTextSize(21);
    button->getRenderer()->setBackgroundColor({18, 32, 45});
    button->getRenderer()->setBackgroundColorHover({61, 25, 37});
    button->getRenderer()->setBorderColor(ACCENT_COLOR); button->getRenderer()->setTextColor(TEXT_COLOR);
    button->onPress(std::move(callback));
    button->onMouseEnter([this] { ctx.cursor.SetHovered(true); });
    button->onMouseLeave([this] { ctx.cursor.SetHovered(false); });
    panel_->add(button); return button;
  }

  void Game::BindInputs() {
    const auto &keys = ControlsConfig::DefaultKeys;
    for (int action = 0; action < Count; ++action) if (keys[action] != sf::Keyboard::Scan::Unknown) ctx.input.Bind(action, Input::Keyboard{keys[action]});
    ctx.input.Bind(Lethal, Input::Mouse{sf::Mouse::Button::Left});
    ctx.input.Bind(NonLethal, Input::Mouse{sf::Mouse::Button::Right});
    const auto &movementKeys = ControlsConfig::MovementKeys;
    const int diveKey = std::clamp(ctx.save.Get(ControlsConfig::DiveSaveKey.data(), 0), 0, 4), rollKey = std::clamp(ctx.save.Get(ControlsConfig::RollSaveKey.data(), 1), 0, 4);
    ctx.input.Bind(Dive, Input::Keyboard{movementKeys[diveKey]});
    ctx.input.Bind(Roll, Input::Keyboard{movementKeys[rollKey == diveKey ? (rollKey + 1) % 5 : rollKey]});
    ctx.input.Bind(Left, Input::Keyboard{sf::Keyboard::Scan::Left});
    ctx.input.Bind(Right, Input::Keyboard{sf::Keyboard::Scan::Right});
    ctx.input.Bind(Up, Input::Keyboard{sf::Keyboard::Scan::Up});
    ctx.input.Bind(Down, Input::Keyboard{sf::Keyboard::Scan::Down});
  }

  void Game::Start() {
    ApplyUserPreferences(ctx);
    missionMusic_ = false; ctx.audio.PlayMusic(ctx.resources, MusicConfig::Lobby); ctx.audio.SetMusicIntensity(0.f);
    session_.Stop(); held_.fill(false); previousPhase_ = GamePhase::Base;
    ready_ = false; paused_ = false; showOrders_ = false; lastSound_ = 0;
    lastNetworkTime_ = ctx.time.GetElapsedTime();
    puzzleFailure_ = {}; puzzleFailureUntil_ = 0.f; puzzleFailurePhase_ = GamePhase::Base;
    preparingDecoy_ = false; cancelledDecoy_ = false; worldRenderer_.SetDecoyPreview(false, {}, 0.f);
    cqcBlocked_ = true;
    firstPerson_ = false; perspectiveKeyDown_ = false; perspectivePulseConsumed_ = false; focused_ = true; firstPersonPointerReset_ = false;
    ctx.cursor.SetCaptured(false);
    BindInputs(); worldRenderer_.ResetZoom(); ctx.renderer.ResetView(); ctx.renderer.SetPostProcessingEnabled(false);
    ctx.renderer.SetTextureSmoothing(false); ctx.cursor.SetVisible(true); ctx.cursor.SetHovered(false);
    root_->setVisible(true); root_->setEnabled(true); ctx.gui.Add(root_); Open(Page::Connection); RefreshHud();
  }

  bool Game::Pressed(Action action, bool edge) {
    const bool down = ctx.input.Pressed(action);
    const bool result = down && (!edge || !held_[action]); held_[action] = down; return result;
  }

  const Agent *Game::LocalAgent() const {
    for (const auto &agent : session_.GetWorld().agents) if (agent.id == session_.GetLocalPlayerId()) return &agent;
    return nullptr;
  }

  PlayerId Game::ViewPlayer() const {
    if (const auto *agent = LocalAgent(); agent && agent->lifeState == LifeState::Dead && agent->spectatorTarget) return agent->spectatorTarget;
    return session_.GetLocalPlayerId();
  }

  bool Game::CanUseFirstPerson() const {
    const auto *agent = LocalAgent();
    return session_.IsConnected() && IsTactical(session_.GetWorld()) && agent && agent->connected && agent->alive &&
      agent->lifeState == LifeState::Alive && !agent->incapacitated && !agent->extracted && !agent->hidden && !agent->cover.active &&
      agent->draggingGuard < 0 && !agent->cqcHoldingGuard && !agent->cqcHoldingAgent && !agent->cqcHeldBy &&
      !agent->cqcApproachGuard && !agent->cqcApproachAgent && !agent->cqcPressPending && agent->cqcAttackTime <= 0.f &&
      agent->maneuver.kind == ManeuverKind::None && agent->traversal.mode == TraversalMode::Grounded && !agent->puzzle.target &&
      agent->actionPose != ActionPose::Revive && !preparingDecoy_ && !agent->decoyGuide.active;
  }

  bool Game::ToggleFirstPerson() {
    if (firstPerson_) { LeaveFirstPerson(); return true; }
    if (paused_ || !focused_ || page_ != Page::None || ctx.window.IsDisplayConfirmationPending() || !CanUseFirstPerson()) return false;
    const auto *agent = LocalAgent();
    savedTacticalCursor_ = ctx.cursor.GetPosition();
    firstPersonCamera_.yaw = std::atan2(agent->facing.y, agent->facing.x); firstPersonCamera_.pitch = 0.f;
    firstPersonCamera_.fieldOfView = ctx.save.Get(FirstPersonConfig::FovSaveKey, FirstPersonConfig::DefaultFov);
    firstPersonRenderer_.Reset(session_.GetWorld(), agent->id);
    firstPerson_ = true; cqcBlocked_ = true; showOrders_ = false; worldRenderer_.SetNamesVisible(false);
    session_.Submit({}); SyncFirstPersonInput(); return true;
  }

  void Game::LeaveFirstPerson() {
    if (!firstPerson_) return;
    firstPerson_ = false; cqcBlocked_ = true; firstPersonPointerReset_ = false;
    ctx.cursor.SetCaptured(false); ctx.cursor.SetVisible(true); ctx.cursor.SetHovered(false);
    const auto size = ctx.window.GetLogicalSize();
    ctx.cursor.SetPosition({std::clamp(savedTacticalCursor_.x, 0.f, std::max(0.f, size.x - 1.f)),
      std::clamp(savedTacticalCursor_.y, 0.f, std::max(0.f, size.y - 1.f))});
    root_->setVisible(true); session_.Submit({});
  }

  void Game::SyncFirstPersonInput() {
    if (firstPerson_ && !CanUseFirstPerson()) LeaveFirstPerson();
    const bool capture = firstPerson_ && focused_ && !paused_ && page_ == Page::None && !ctx.window.IsDisplayConfirmationPending();
    const auto display = ctx.window.GetDisplaySettings(); const auto viewport = ctx.window.GetViewport();
    const bool changed = display != firstPersonDisplay_ || viewport != firstPersonViewport_;
    if (capture != ctx.cursor.IsCaptured() || (capture && changed)) {
      ctx.cursor.SetCaptured(capture); ctx.cursor.SetVisible(!capture); ctx.cursor.SetHovered(false);
      if (capture) { ctx.cursor.SetPosition(ctx.window.GetLogicalSize() * .5f); firstPersonPointerReset_ = true; }
    }
    firstPersonDisplay_ = display; firstPersonViewport_ = viewport;
    root_->setVisible(!firstPerson_ || page_ != Page::None);
    if (firstPerson_) firstPersonCamera_.fieldOfView = ctx.save.Get(FirstPersonConfig::FovSaveKey, FirstPersonConfig::DefaultFov);
  }

  void Game::UpdateFirstPersonLook() {
    if (!firstPerson_ || !ctx.cursor.IsCaptured()) return;
    const auto center = ctx.window.GetLogicalSize() * .5f;
    const auto delta = ctx.cursor.GetPosition() - center;
    if (!firstPersonPointerReset_) {
      firstPersonCamera_.yaw = std::remainder(firstPersonCamera_.yaw + delta.x * FirstPersonConfig::MouseSensitivity, 2.f * std::numbers::pi_v<float>);
      firstPersonCamera_.pitch = std::clamp(firstPersonCamera_.pitch - delta.y * FirstPersonConfig::MouseSensitivity,
        -FirstPersonConfig::MaximumPitch, FirstPersonConfig::MaximumPitch);
    }
    firstPersonPointerReset_ = false; ctx.cursor.SetPosition(center);
  }

  void Game::Update() {
    SyncFirstPersonInput();
    const bool perspectiveConsumed = perspectivePulseConsumed_; perspectivePulseConsumed_ = false;
    showOrders_ = !firstPerson_ && !paused_ && page_ == Page::None && ctx.input.Held(Orders);
    worldRenderer_.SetNamesVisible(showOrders_);
    if (page_ == Page::Weapons) {
      if (!ctx.input.Held(WeaponMenu)) ClosePanel();
      else {
        session_.Submit({}); RefreshWeapons(); return;
      }
    }
    if (!paused_ && page_ == Page::Puzzle && session_.IsConnected()) {
      ActionCommand command;
      if (const auto *agent = LocalAgent()) {
        command.puzzleInput = pendingPuzzleInput_; command.puzzleTarget = agent->puzzle.target; command.puzzleHeld = pendingPuzzleHeld_;
        if (command.puzzleInput == -1 && agent->puzzle.family == PuzzleFamily::Arrows) {
          const std::array<Action, 4> directions{Up, Right, Down, Left};
          for (std::size_t index = 0; index < directions.size(); ++index) if (ctx.input.JustPressed(directions[index])) { command.puzzleInput = static_cast<int>(index + 1); break; }
        }
      }
      pendingPuzzleInput_ = -1; session_.Submit(command); return;
    }
    if (paused_ || page_ != Page::None || !session_.IsConnected()) {
      preparingDecoy_ = false; worldRenderer_.SetDecoyPreview(false, {}, 0.f);
      session_.Submit({}); held_.fill(false); return;
    }
    if (Pressed(Lobby, true) && session_.GetWorld().phase == GamePhase::Base) { Open(Page::Deployment); return; }
    if (ctx.input.Held(WeaponMenu)) { Open(Page::Weapons); return; }
    const auto *agent = LocalAgent();
    if (!agent) return;
    if (agent->lifeState == LifeState::Dead) {
      ActionCommand spectator;
      spectator.spectateNext = ctx.input.JustPressed(SpectateNext);
      spectator.spectatePrevious = ctx.input.JustPressed(SpectatePrevious);
      preparingDecoy_ = false; worldRenderer_.SetDecoyPreview(false, {}, 0.f);
      session_.Submit(spectator); return;
    }
    if (firstPerson_) UpdateFirstPersonLook(); else worldRenderer_.AdjustZoom(ctx.input.WheelDelta());
    ActionCommand command;
    const auto moving = [this](Action action) { return Pressed(action) || ctx.input.Held(action); };
    const sf::Vector2f screenMove{static_cast<float>(moving(Right)) - static_cast<float>(moving(Left)), static_cast<float>(moving(Down)) - static_cast<float>(moving(Up))};
    const auto &world = session_.GetWorld();
    auto direction = worldRenderer_.ScreenToGround({800.f + screenMove.x, 480.f + screenMove.y}, world, agent->id) - worldRenderer_.ScreenToGround({800.f, 480.f}, world, agent->id);
    if (firstPerson_) {
      const sf::Vector2f forward{std::cos(firstPersonCamera_.yaw), std::sin(firstPersonCamera_.yaw)}, right{-forward.y, forward.x};
      direction = forward * -screenMove.y + right * screenMove.x;
    }
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length > 0.f) direction /= length;
    command.moveX = direction.x; command.moveY = direction.y;
    const auto cursor = ctx.cursor.GetPosition();
    command.aiming = ctx.input.Held(NonLethal);
    const bool weaponRay = command.aiming || ctx.input.JustPressed(Lethal);
    const auto aimSolution = firstPerson_ ? firstPersonRenderer_.Aim(world, agent->id, firstPersonCamera_) : worldRenderer_.AimAt(cursor, world, agent->id);
    const auto lookAt = firstPerson_ || weaponRay ? aimSolution.ground : worldRenderer_.ScreenToGround(cursor, world, agent->id);
    auto aim = lookAt - agent->position;
    const float aimLength = std::sqrt(aim.x * aim.x + aim.y * aim.y);
    if (aimLength > 1.f) aim /= aimLength; else aim = agent->facing;
    command.aimX = aim.x; command.aimY = aim.y;
    command.aimSlope = firstPerson_ || weaponRay ? aimSolution.slope : 0.f;
    command.orientBodyToAim = firstPerson_;
    command.coverToggle = !firstPerson_ && ctx.input.JustPressed(WallCover);
    command.dive = !firstPerson_ && ctx.input.JustPressed(Dive); command.roll = !firstPerson_ && ctx.input.JustPressed(Roll);
    command.toggleSuppressor = ctx.input.JustPressed(Suppressor);
    command.run = Pressed(Run); command.crouch = Pressed(Crouch, true); command.prone = Pressed(Prone, true);
    command.traverse = !firstPerson_ && ctx.input.JustPressed(Traverse) && !command.run;
    command.drop = !firstPerson_ && ctx.input.JustPressed(Traverse) && command.run;
    command.interact = Pressed(Interact, true);
    command.dragBody = !firstPerson_ && held_[Interact];
    if (!ctx.input.Held(Cqc) && !perspectiveConsumed) cqcBlocked_ = false;
    if (firstPerson_) cqcBlocked_ = true;
    command.cqc = !perspectiveConsumed && !cqcBlocked_ && ctx.input.JustPressed(Cqc);
    command.cqcHeld = !perspectiveConsumed && !cqcBlocked_ && ctx.input.Held(Cqc);
    command.push = !firstPerson_ && ctx.input.JustPressed(Shove); command.knock = !firstPerson_ && ctx.input.JustPressed(Knock);
    command.tool = Pressed(Tool, true) && !firstPerson_;
    command.reloadWeapon = Pressed(Reload, true);
    command.weaponChoice = Pressed(SelectPistol, true) ? 0 : Pressed(SelectNonLethal, true) ? 1 : -1;
    const bool choosingWeapon = pendingWeaponChoice_ >= 0;
    if (pendingWeaponChoice_ >= 0) { command.weaponChoice = pendingWeaponChoice_; pendingWeaponChoice_ = -1; }
    const bool lethal = ctx.input.JustPressed(Lethal), nonLethal = ctx.input.JustPressed(NonLethal);
    const bool decoyHeld = Pressed(Decoy) && !firstPerson_;
    const bool wasPreparing = preparingDecoy_;
    if (!decoyHeld && cancelledDecoy_) cancelledDecoy_ = false;
    const bool canThrow = IsTactical(world) && agent->alive && !agent->incapacitated && !agent->hidden && !agent->extracted && agent->draggingGuard < 0 &&
        agent->loadout.decoys > 0 && agent->actionCooldown <= 0.f && !command.dragBody && !command.interact && !command.tool && !command.cqc && !command.cqcHeld && !command.push && !command.knock &&
        std::ranges::none_of(world.guards, [&](const Guard &guard) { return guard.heldBy == agent->id && guard.cqcState != CqcState::None; });
    if (decoyHeld && canThrow && !cancelledDecoy_) preparingDecoy_ = true;
    const bool decoyContext = preparingDecoy_ || wasPreparing;
    if (decoyContext) {
      const auto destination = worldRenderer_.ScreenToGround(cursor, world, agent->id);
      const auto delta = destination - agent->position;
      command.decoyDistance = std::min(delta.length(), DistractionConfig::MaximumDecoyDistance);
      const auto throwDirection = delta.lengthSquared() > 1.f ? delta / delta.length() : agent->facing;
      command.aimX = throwDirection.x; command.aimY = throwDirection.y;
      if (nonLethal || !canThrow) { preparingDecoy_ = false; cancelledDecoy_ = decoyHeld; }
      else if (!decoyHeld) { command.throwDecoy = true; preparingDecoy_ = false; }
      worldRenderer_.SetDecoyPreview(preparingDecoy_, throwDirection, command.decoyDistance);
    } else worldRenderer_.SetDecoyPreview(false, {}, 0.f);
    command.prepareDecoy = preparingDecoy_;
    if (!choosingWeapon && !decoyContext && (firstPerson_ || (cursor.y > 116.f && cursor.y < (footer_->isVisible() ? 820.f : 900.f)))) { command.fireSelected = lethal; }
    if (command.interact && world.phase == GamePhase::Base && !agent->hidden && agent->draggingGuard < 0 &&
        agent->cqcHoldingGuard == 0 && agent->cqcHoldingAgent == 0 && agent->cqcAttackTime <= 0.f && !Interaction::FindBody(world, *agent)) {
      if (const auto *object = Interaction::FindObject(world, *agent)) {
        switch (object->kind) {
        case ObjectKind::Operations: Open(Page::Operations); return;
        case ObjectKind::Armory: Open(Page::Armory); return;
        case ObjectKind::Intelligence: Open(Page::Intelligence); return;
        case ObjectKind::Deployment: Open(Page::Deployment); return;
        default: break;
        }
      }
    }
    session_.Submit(command);
  }

  void Game::UpdateBackground() {
    const float networkTime = ctx.time.GetElapsedTime();
    const float networkDelta = std::max(0.f, networkTime - lastNetworkTime_);
    lastNetworkTime_ = networkTime;
    session_.Tick(ctx.time.GetDeltaTime(), paused_ || (page_ != Page::None && page_ != Page::Puzzle), networkDelta);
    SyncFirstPersonInput();
    if (firstPerson_) firstPersonRenderer_.Update(session_.GetWorld(), session_.GetLocalPlayerId(), ctx.time.GetDeltaTime());
    const auto phase = session_.GetWorld().phase;
    const bool missionMusic = session_.IsConnected() && phase != GamePhase::Base;
    if (missionMusic != missionMusic_) {
      ctx.audio.PlayMusic(ctx.resources, missionMusic ? MusicConfig::Mission : MusicConfig::Lobby);
      missionMusic_ = missionMusic;
    }
    ctx.audio.SetMusicIntensity(phase == GamePhase::Mission && session_.IsConnected() ? MusicConfig::Intensity(session_.GetWorld().securityAlert) : 0.f);
    if (phase != previousPhase_) {
      if (phase == GamePhase::Mission) { ClosePanel(); lastSound_ = 0; }
      else if (phase == GamePhase::Results) {
        PersistResults(); Open(Page::Results);
        const auto result = session_.GetWorld().results.find(session_.GetLocalPlayerId());
        if (result != session_.GetWorld().results.end()) audioPlayback_.Play(result->second.success ? SoundKind::MissionSuccess : SoundKind::MissionFailure);
      }
      else { ready_ = false; ClosePanel(); }
      previousPhase_ = phase;
    }
    if (session_.IsConnected()) {
      const auto &world = session_.GetWorld();
      const auto *agent = LocalAgent();
      const auto listener = std::ranges::find(world.agents, ViewPlayer(), &Agent::id);
      audioPlayback_.UpdateAmbience(world, ViewPlayer());
      for (const auto &sound : world.sounds) if (sound.sequence > lastSound_) {
        if (listener != world.agents.end()) audioPlayback_.PlayEvent(sound, world, *listener,
          firstPerson_ ? std::optional<sf::Vector2f>{{-std::sin(firstPersonCamera_.yaw), std::cos(firstPersonCamera_.yaw)}} : std::nullopt);
        lastSound_ = sound.sequence;
      }
      if (agent && agent->puzzle.target && (page_ == Page::None || page_ == Page::Puzzle)) {
        if (page_ != Page::Puzzle) Open(Page::Puzzle);
        else if (puzzleStageRendered_ != agent->puzzle.stage || puzzleTargetRendered_ != agent->puzzle.target) { panel_->removeAllWidgets(); BuildPuzzle(); }
      } else if (page_ == Page::Puzzle) ClosePanel();
      if (page_ == Page::Puzzle && puzzleProgress_ && agent) puzzleProgress_->setValue(static_cast<unsigned int>(std::clamp(agent->puzzle.coordinationProgress / DevicesConfig::CoordinationHoldDuration, 0.f, 1.f) * 100.f));
      if (page_ == Page::Puzzle) RefreshPuzzleControls();
    } else audioPlayback_.StopAmbience();
    if (page_ == Page::Options && observedDisplay_ != ctx.window.GetDisplaySettings()) SyncDisplayOptions();
    if (page_ == Page::Weapons) RefreshWeapons();
    RefreshHud();
  }

  void Game::Render() const {
    if (firstPerson_) firstPersonRenderer_.Render(session_.GetWorld(), session_.GetLocalPlayerId(), firstPersonCamera_, !paused_ && page_ == Page::None);
    else worldRenderer_.Render(session_.GetWorld(), ViewPlayer(), !paused_ && page_ == Page::None && IsTactical(session_.GetWorld()));
  }

  void Game::OnEvent(const sf::Event &event) {
    if (event.is<sf::Event::FocusLost>()) { focused_ = false; if (page_ == Page::Weapons) ClosePanel(); pendingPuzzleHeld_ = false; session_.Submit({}); SyncFirstPersonInput(); }
    if (event.is<sf::Event::FocusGained>()) { focused_ = true; SyncFirstPersonInput(); }
    if (const auto *key = event.getIf<sf::Event::KeyReleased>(); key && key->scancode == ControlsConfig::PerspectiveKey) perspectiveKeyDown_ = false;
    if (const auto *key = event.getIf<sf::Event::KeyReleased>(); key && key->scancode == sf::Keyboard::Scan::Q && page_ == Page::Weapons) ClosePanel();
    if (const auto *key = event.getIf<sf::Event::KeyPressed>()) {
      if (key->scancode == ControlsConfig::PerspectiveKey) {
        const bool edge = !perspectiveKeyDown_; perspectiveKeyDown_ = true;
        if (key->shift) { cqcBlocked_ = true; perspectivePulseConsumed_ = true; if (edge) ToggleFirstPerson(); return; }
      }
      if (key->scancode == sf::Keyboard::Scan::Escape) { NavigateBack(); return; }
      if (page_ == Page::Options) HandleOptionsKey(event, options_);
    }
    if (const auto *joystick = event.getIf<sf::Event::JoystickButtonPressed>()) {
      const auto button = Input::HardwareToLogical(joystick->button, joystick->joystickId);
      if (button == GamepadButton::Start || button == GamepadButton::Select) NavigateBack();
    }
  }

  void Game::NavigateBack() {
    if (paused_) return;
    if (page_ == Page::Puzzle) { pendingPuzzleInput_ = 0; return; }
    if (preparingDecoy_) {
      preparingDecoy_ = false; cancelledDecoy_ = true;
      worldRenderer_.SetDecoyPreview(false, {}, 0.f); session_.Submit({}); return;
    }
    if (page_ == Page::Controls) Open(Page::Options);
    else if (page_ == Page::Options) Open(Page::Pause);
    else if (page_ == Page::Connection) ctx.scenes.ChangeScene(MENU_SCENE);
    else if (page_ == Page::None && session_.IsConnected() && session_.GetWorld().phase != GamePhase::Results) Open(Page::Pause);
    else if (page_ != Page::Results) ClosePanel();
  }

  void Game::Open(Page page) {
    if (page == Page::Puzzle || page == Page::Connection || page == Page::Results) LeaveFirstPerson();
    pendingPuzzleHeld_ = false;
    cqcBlocked_ = true;
    if (preparingDecoy_) cancelledDecoy_ = true;
    preparingDecoy_ = false; worldRenderer_.SetDecoyPreview(false, {}, 0.f);
    page_ = page; session_.Submit({}); panel_->removeAllWidgets(); panel_->setVisible(true);
    panel_->setSize({780.f, 596.f}); panel_->setPosition({410.f, 135.f});
    status_.reset(); roster_.reset(); briefing_.reset(); deploy_.reset(); ctx.cursor.SetHovered(false);
    if (page == Page::Connection) BuildConnection();
    else if (page == Page::Results) BuildResults();
    else if (page == Page::Pause) BuildPause();
    else if (page == Page::Options) BuildOptions();
    else if (page == Page::Weapons) BuildWeapons();
    else if (page == Page::Controls) BuildControls();
    else if (page == Page::Puzzle) BuildPuzzle();
    else BuildStation(page);
    lobby_->setEnabled(false);
    for (const auto &widget : panel_->getWidgets()) if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget); button && button->isEnabled()) { button->setFocused(true); break; }
    if (page == Page::Options) resolutionBox_->setFocused(true);
    SyncFirstPersonInput();
  }

  void Game::ClosePanel() {
    pendingPuzzleHeld_ = false;
    page_ = Page::None; panel_->setVisible(false); panel_->setFocused(false);
    lobby_->setEnabled(true);
    // Consume las pulsaciones del panel y conserva las entradas que siguen mantenidas.
    ctx.input.BeginFrame();
    for (int action = 0; action < Count; ++action) held_[action] = ctx.input.Pressed(action);
    SyncFirstPersonInput();
  }

  void Game::BuildConnection() {
    Label("ACCESO A CIO", {30.f, 20.f}, {714.f, 45.f}, 30);
    Label("Operación individual o cooperación LAN para 2–4 agentes.", {30.f, 72.f}, {714.f, 40.f});
    Label("Identificación del agente", {30.f, 120.f}, {300.f, 30.f}, 18);
    auto name = tgui::EditBox::create(); name->setPosition({340.f, 115.f}); name->setSize({404.f, 42.f});
    name->setText(tgui::String(ctx.save.Get("NoTrace:AgentName", std::string("Agente")))); name->setMaximumCharacters(24); name->setTextSize(20); panel_->add(name);
    Label("IP del anfitrión", {30.f, 180.f}, {280.f, 32.f}, 18);
    auto ip = tgui::EditBox::create(); ip->setPosition({340.f, 173.f}); ip->setSize({270.f, 42.f}); ip->setDefaultText("192.168.1.10"); ip->setMaximumCharacters(15); ip->setTextSize(20); panel_->add(ip);
    auto port = tgui::EditBox::create(); port->setPosition({625.f, 173.f}); port->setSize({119.f, 42.f}); port->setText(std::to_string(NetworkConfig::DefaultPort)); port->setMaximumCharacters(5); port->setTextSize(20); panel_->add(port);
    status_ = Label("El anfitrión debe aceptar el tráfico en su red privada. No requiere Internet.", {30.f, 238.f}, {714.f, 60.f}, 17);
    auto identity = [this, name] { auto value = name->getText().toStdString(); if (value.empty()) value = "Agente"; ctx.save.Set("NoTrace:AgentName", value); return value; };
    auto readPort = [port] { const auto value = port->getText().toStdString(); unsigned int number = 0; const auto result = std::from_chars(value.data(), value.data() + value.size(), number); return result.ec == std::errc{} && result.ptr == value.data() + value.size() && number > 0 && number <= 65535 ? static_cast<std::uint16_t>(number) : std::uint16_t{}; };
    Button("OPERACIÓN INDIVIDUAL", 312.f, [this, identity] { session_.StartSolo(identity()); ClosePanel(); });
    auto host = Button("CREAR SALA LAN", 374.f, [this, identity, readPort] {
      const auto value = readPort();
      if (!value) { status_->setText("Puerto inválido (1–65535)."); return; }
      if (session_.Host(identity(), value)) Open(Page::Deployment); else status_->setText(session_.GetStatus());
    });
    host->setSize({345.f, 48.f});
    auto localAddress = Button("MOSTRAR MI IP", 374.f, [this] {
      const auto address = ctx.network.GetLocalAddress();
      status_->setText(tgui::String(address == "IP local no disponible" ? address + ". Conéctate a una red local y vuelve a intentarlo." :
        "Tu IP local: " + address + "\nCompártela con quien vaya a conectarse a tu sala LAN."));
    });
    localAddress->setPosition({399.f, 374.f}); localAddress->setSize({345.f, 48.f});
    Button("CONECTAR POR IP", 436.f, [this, ip, identity, readPort] {
      const auto value = readPort();
      if (!value) { status_->setText("Puerto inválido (1–65535)."); return; }
      if (session_.Join(ip->getText().toStdString(), value, identity())) Open(Page::Deployment); else status_->setText(session_.GetStatus());
    });
    Button("VOLVER AL MENÚ", 520.f, [this] { ctx.scenes.ChangeScene("Menu"); });
  }

  void Game::BuildStation(Page page) {
    const auto &contract = GetContract(session_.GetWorld().contract);
    if (page == Page::Operations) {
      Label("SALA DE OPERACIONES", {30.f, 20.f}, {714.f, 45.f}, 28);
      Label("Selecciona contrato. Solo el anfitrión modifica la misión compartida.", {30.f, 76.f}, {714.f, 62.f}, 18);
      float y = 150.f;
      for (const auto &entry : Contracts()) {
        auto button = Button(entry.name, y, [this, id = entry.id] { session_.SetContract(id); Open(Page::Operations); });
        button->setEnabled(!session_.IsClient());
        y += 64.f;
      }
      Label(contract.briefing, {30.f, 350.f}, {714.f, 100.f}, 20);
      Label("S++ Maestro exige objetivos, extracción y cero armas, CQC o detecciones.", {30.f, 458.f}, {714.f, 55.f}, 18);
    } else if (page == Page::Armory) {
      Label("PREPARACIÓN / ARMERÍA", {30.f, 20.f}, {714.f, 45.f}, 28);
      Label("Cada agente prepara su equipo. Munición y dispositivos son finitos.", {30.f, 78.f}, {714.f, 68.f}, 19);
      for (std::size_t index = 0; index < PreparationConfig::Presets.size(); ++index) {
        const auto loadout = PreparationConfig::Presets[index];
        auto preset = Button(std::format("{}\n{} letales / {} no letales / {} señuelos / {} herramientas", PreparationConfig::Names[index], loadout.lethalAmmo, loadout.nonLethalAmmo, loadout.decoys, loadout.tools),
                             160.f + static_cast<float>(index) * 75.f, [this, loadout] { session_.SetLoadout(loadout); PlayCue(AudioConfig::File(SoundKind::Equipment)); ClosePanel(); });
        preset->setTextSize(18); preset->setSize({714.f, 64.f});
      }
    } else if (page == Page::Intelligence) {
      Label("CENTRO DE INTELIGENCIA", {30.f, 20.f}, {714.f, 45.f}, 28);
      Label(contract.name + "\n\n" + contract.briefing, {30.f, 82.f}, {714.f, 450.f}, 18);
    } else {
      Label("DESPLIEGUE / SALA LAN", {30.f, 20.f}, {714.f, 45.f}, 28);
      status_ = Label("", {30.f, 79.f}, {714.f, 65.f}, 18);
      roster_ = Label("", {30.f, 150.f}, {714.f, 160.f}, 22);
      briefing_ = Label(contract.name + "\n" + contract.briefing, {30.f, 315.f}, {714.f, 100.f}, 18);
      Button("ALTERNAR PREPARADO", 430.f, [this] { ready_ = !ready_; session_.SetReady(ready_); });
      deploy_ = Button("DESPLEGAR OPERACIÓN", 490.f, [this] { session_.Deploy(); });
    }
    auto close = Button("RECORRER LA BASE", 548.f, [this] { ClosePanel(); });
    close->setSize({714.f, 36.f}); close->setTextSize(18);
    if (page == Page::Deployment) {
      close->setSize({345.f, 36.f});
      auto leave = Button("DESCONECTAR", 548.f, [this] { session_.Stop(); ready_ = false; Open(Page::Connection); });
      leave->setPosition({399.f, 548.f}); leave->setSize({345.f, 36.f}); leave->setTextSize(18);
    }
  }

  void Game::BuildPause() {
    const bool mission = session_.GetWorld().phase == GamePhase::Mission;
    Label(mission ? "OPERACIÓN / PAUSA" : "BASE CIO / MENÚ", {30.f, 20.f}, {714.f, 45.f}, 29);
    Label(session_.IsClient() || session_.IsHost() ? "Pausa local: la operación LAN continúa para todos.\nTus controles quedan neutralizados mientras esté abierto este menú." : "La operación individual permanece detenida hasta reanudar.", {30.f, 78.f}, {714.f, 65.f}, 18);
    float y = 156.f;
    Button(mission ? "REANUDAR OPERACIÓN" : "REANUDAR EN LA BASE", y, [this] { ClosePanel(); });
    if (mission && !session_.IsClient()) {
      y += 62.f;
      Button("REINICIAR OPERACIÓN", y, [this] {
        if (session_.RestartOperation()) { ClosePanel(); }
      });
    }
    y += 62.f;
    Button("OPCIONES", y, [this] { Open(Page::Options); });
    if (mission && !session_.IsClient()) {
      y += 62.f;
      Button("VOLVER A LA BASE", y, [this] { session_.ReturnToBase(); ClosePanel(); });
    }
    y += 62.f;
    Button("REGRESAR AL MENÚ PRINCIPAL", y, [this] { ctx.scenes.ChangeScene(MENU_SCENE); });
    y += 62.f;
    Button("SALIR DEL JUEGO", y, [this] { ctx.scenes.RequestQuit(); });
  }

  void Game::BuildResults() {
    const auto &world = session_.GetWorld();
    Label("INFORME DE OPERACIÓN", {30.f, 20.f}, {714.f, 45.f}, 28);
    const auto found = world.results.find(session_.GetLocalPlayerId());
    if (found != world.results.end()) {
      const auto &result = found->second;
      Label(std::string(RankName(result.rank)), {30.f, 90.f}, {714.f, 65.f}, 42);
      Label(std::format("{}  ·  {} puntos\n\n{}\n\nObjetivos: {}/{}    Secundarios: {}\nAlarmas confirmadas: {}    Tiempo: {:.0f} s\n\n{}", result.success ? "OPERACIÓN CUMPLIDA" : "OPERACIÓN FALLIDA", result.score, result.reason, world.objectives, GetContract(world.contract).requiredObjectives, world.secondary, world.alarmCount, world.elapsed, result.maestroEligible ? "Disciplina Maestro: sin armas, CQC ni detección." : "S++ no disponible: " + result.maestroDeniedReason), {30.f, 175.f}, {714.f, 290.f}, 18);
    } else Label("Esperando el informe del anfitrión…", {30.f, 100.f}, {714.f, 100.f}, 24);
    auto retry = Button("REINTENTAR OPERACIÓN", 475.f, [this] { if (session_.RestartOperation()) { ClosePanel(); } }); retry->setEnabled(!session_.IsClient());
    auto back = Button("REGRESAR A LA BASE", 535.f, [this] { session_.ReturnToBase(); }); back->setEnabled(!session_.IsClient()); back->setSize({344.f, 44.f});
    auto menu = Button("SALIR AL MENÚ", 535.f, [this] { ctx.scenes.ChangeScene("Menu"); }); menu->setPosition({400.f, 535.f}); menu->setSize({344.f, 44.f});
  }

  void Game::RefreshHud() {
    const auto &world = session_.GetWorld();
    const auto &contract = GetContract(world.contract);
    const auto alert = world.securityAlert == SecurityAlert::Red ? std::string("ROJA") : world.securityAlert == SecurityAlert::Orange ?
        std::format("NARANJA {:.0f}s",std::ceil(world.securityRemaining)) : world.securityAlert == SecurityAlert::Yellow ? std::format("AMARILLA {:.0f}s",std::ceil(world.securityRemaining)) : "SIN ALERTA";
    title_->setText(world.phase == GamePhase::Base ? "NO TRACE  //  BASE CIO" : "NO TRACE  //  " + contract.name);
    mission_->setText(world.phase == GamePhase::Base ? "BASE CIO / ENTRENAMIENTO" : std::format("OBJETIVOS {}/{}  ·  SECUNDARIOS {}  ·  {:.0f} s  ·  {}", world.objectives, contract.requiredObjectives, world.secondary, std::max(0.f, contract.timeLimit - world.elapsed), alert));
    mission_->getRenderer()->setTextColor(world.securityAlert == SecurityAlert::Red ? tgui::Color{221,67,77} : world.securityAlert == SecurityAlert::Orange ? tgui::Color{238,139,58} : world.securityAlert == SecurityAlert::Yellow ? tgui::Color{233,202,83} : tgui::Color{225,232,238});
    if (const auto *agent = LocalAgent()) {
      health_->setVisible(true); stamina_->setVisible(true);
      const bool pistol = agent->selectedWeapon == WeaponKind::Pistol; const float durability = pistol ? agent->pistolSuppressor : agent->nonLethalSuppressor;
      const bool attached = pistol ? agent->pistolSuppressorEquipped : agent->nonLethalSuppressorEquipped;
      suppressor_->setVisible(true); suppressor_->setValue(static_cast<unsigned int>(std::clamp(durability, 0.f, 100.f)));
      suppressor_->setText(durability <= 0.f ? "SILENCIADOR ROTO" : attached ? "SILENCIADOR " + std::to_string(suppressor_->getValue()) + "%" : "SILENCIADOR DESMONTADO");
      health_->setValue(static_cast<unsigned int>(std::lround(std::clamp(agent->health / GameplayConfig::MaxHealth, 0.f, 1.f) * 100.f)));
      stamina_->setValue(static_cast<unsigned int>(std::lround(std::clamp(agent->stamina / GameplayConfig::MaxStamina, 0.f, 1.f) * 100.f)));
      health_->setText("VIDA " + std::to_string(health_->getValue()) + "%");
      stamina_->setText("RESIST. " + std::to_string(stamina_->getValue()) + "%");
      lobby_->setText(world.phase == GamePhase::Mission ? "PAUSA  [ESC]" : "BASE / SALA  [F2]");
      agent_->setText(std::format("{}{} | {}{} | L {}/{}+{} · NL {}/{}+{} · G {} · T {}", StanceName(agent->stance), !agent->alive ? " / ELIMINADO" : agent->incapacitated ? " / INCONSCIENTE" : agent->hidden ? " / OCULTO" : agent->extracted ? " / EXTRAÍDO" : agent->cover.active ? " / COBERTURA" : "", agent->selectedWeapon == WeaponKind::Pistol ? "PISTOLA" : "NO LETAL", agent->reloadRemaining > 0.f ? " / RECARGANDO" : "", agent->lethalMagazine, WeaponConfig::PistolCapacity, ReserveAmmo(*agent, WeaponKind::Pistol), agent->nonLethalMagazine, WeaponConfig::NonLethalCapacity, ReserveAmmo(*agent, WeaponKind::NonLethal), agent->loadout.decoys, agent->loadout.tools));
      private_->setText(tgui::String(std::string(PrivateOrderText(agent->privateOrder)) + (agent->privateComplete ? "\nORDEN CUMPLIDA" : "")));
      private_->setVisible((showOrders_ || world.elapsed < PresentationConfig::PrivateOrderIntroDuration) && world.phase == GamePhase::Mission && page_ == Page::None);
      std::string hint = Interaction::Prompt(world, *agent);
      if (hint.empty()) hint = Traversal::Prompt(world, *agent);
      if (hint.empty()) hint = Traversal::Prompt(world, *agent, true);
      if (hint.empty() && Simulation::FindCqcTarget(world, *agent, Simulation::CqcAction::Push)) hint = "V · Empujar";
      else if (hint.empty() && Simulation::FindCqcTarget(world, *agent, Simulation::CqcAction::Grab)) hint = "F · Agarrar";
      if (preparingDecoy_) hint.clear();
      if (agent->incapacitated) hint = std::format("RESCATE · {:.0f} s", std::ceil(agent->incapacityRemaining));
      if (agent->lifeState == LifeState::Dead) {
        const auto target = std::ranges::find(world.agents, ViewPlayer(), &Agent::id);
        agent_->setText(target != world.agents.end() && target->id != agent->id ? "ESPECTADOR · " + target->name : "ESPECTADOR · Esperando superviviente");
        hint = "Re Pág / Av Pág · Cambiar observación";
        private_->setVisible(false); health_->setVisible(false); stamina_->setVisible(false); suppressor_->setVisible(false);
      }
      hint_->setText(tgui::String(hint));
    } else { agent_->setText(""); hint_->setText(""); private_->setVisible(false); health_->setVisible(false); stamina_->setVisible(false); }
    std::string feedback;
    for (const auto &shot : world.shots) if (shot.actor == session_.GetLocalPlayerId()) {
      if (shot.blocked) feedback = "Impacto contra una superficie sólida.";
      else if (shot.hit) {
        const std::string zone = shot.zone == HitZone::Head ? "cabeza" : shot.zone == HitZone::Torso ? "torso" : "extremidades";
        feedback = std::string(shot.lethal ? "Impacto letal en " : "Impacto no letal en ") + zone + ".";
      } else feedback = "Disparo sin impacto.";
    }
    if (world.phase != puzzleFailurePhase_ || !session_.IsConnected() || !IsTactical(world) || world.tick < puzzleFailure_.tick) {
      puzzleFailure_ = {}; puzzleFailureUntil_ = 0.f;
      puzzleSuccess_ = {}; puzzleSuccessUntil_ = 0.f; puzzleSuccessText_.clear();
      if (world.phase == GamePhase::Base && world.phase != puzzleFailurePhase_ && !world.events.empty()) puzzleFailure_.sequence = world.events.back().sequence;
      puzzleFailurePhase_ = world.phase;
    }
    if (!session_.IsConnected() || !IsTactical(world) || world.tick < puzzleSuccess_.tick || world.elapsed + PresentationConfig::IntelligenceFeedbackDuration < puzzleSuccessUntil_) {
      puzzleSuccess_ = {}; puzzleSuccessUntil_ = 0.f; puzzleSuccessText_.clear();
    }
    for (const auto &event : world.events) if (event.actor == session_.GetLocalPlayerId() && event.kind == EventKind::PuzzleSolved && event.sequence > puzzleSuccess_.sequence) {
      puzzleSuccess_ = event; puzzleSuccessUntil_ = world.elapsed + PresentationConfig::IntelligenceFeedbackDuration; puzzleSuccessText_.clear();
      const auto device = std::ranges::find(world.objects, event.target, &Interactable::id);
      if (device != world.objects.end() && device->kind == ObjectKind::Computer) puzzleSuccessText_ = device->puzzle.intelligence;
      else if (device == world.objects.end() && world.message.starts_with("INTELIGENCIA:")) puzzleSuccessText_ = world.message;
    }
    if (!puzzleSuccessText_.empty() && world.elapsed < puzzleSuccessUntil_) feedback = puzzleSuccessText_;
    // Solo un fallo propio autoriza el aviso; el estado público de un dispositivo ajeno no lo activa.
    if (session_.IsConnected() && IsTactical(world)) for (const auto &event : world.events) if (event.actor == session_.GetLocalPlayerId() && event.kind == EventKind::PuzzleFailed && event.sequence > puzzleFailure_.sequence) {
      puzzleFailure_ = event; puzzleFailureUntil_ = world.elapsed + DevicesConfig::PuzzleFailureCooldown;
    }
    bool puzzleError = false;
    if (puzzleFailure_.target && world.elapsed < puzzleFailureUntil_) {
      const auto object = std::ranges::find_if(world.objects, [this](const Interactable &value) { return value.id == puzzleFailure_.target; });
      if (object == world.objects.end()) { feedback = "Intervención fallida. Comprueba el dispositivo antes de reintentar."; puzzleError = true; }
      else if (object->puzzle.state == PuzzleState::Failed && object->puzzle.cooldown > 0.f) {
        puzzleFailureUntil_ = std::min(puzzleFailureUntil_, world.elapsed + object->puzzle.cooldown);
        const auto seconds = static_cast<unsigned int>(std::ceil(std::min(object->puzzle.cooldown, puzzleFailureUntil_ - world.elapsed)));
        feedback = object->trap.state == TrapState::Detonated ? "Intervención fallida: el explosivo ha detonado." : std::format("Intervención fallida. Reintento en {} s; después pulsa E.", seconds);
        puzzleError = true;
      } else puzzleFailure_.target = 0;
    } else puzzleFailure_.target = 0;
    message_->getRenderer()->setTextColor(puzzleError ? tgui::Color{239, 115, 106} : TEXT_COLOR);
    message_->setText(tgui::String(session_.IsConnected() ? feedback : session_.GetStatus()));
    message_->setVisible(!message_->getText().empty());
    hint_->setVisible(!hint_->getText().empty());
    footer_->setVisible(hint_->isVisible() || message_->isVisible());
    if (page_ == Page::Deployment) {
      status_->setText(tgui::String(session_.GetStatus() + (session_.IsHost() && ctx.network.GetRole() == NetworkRole::Host ? "\nIP local: " + ctx.network.GetLocalAddress() + " : " + std::to_string(ctx.network.GetPort()) : "")));
      std::string roster;
      for (const auto &player : session_.GetLobbyPlayers()) {
        roster += std::format("{:02}  {}   [{}]\n", player.id, player.name, player.ready ? "PREPARADO" : "EN PREPARACIÓN");
        if (player.id == session_.GetLocalPlayerId()) ready_ = player.ready;
      }
      roster_->setText(tgui::String(roster)); deploy_->setEnabled(session_.CanDeploy());
      briefing_->setText(contract.name + "\n" + contract.briefing);
    }
  }

  void Game::PersistResults() {
    const auto &world = session_.GetWorld(); const auto found = world.results.find(session_.GetLocalPlayerId());
    if (found == world.results.end()) return;
    const auto key = "NoTrace:Best:" + std::to_string(static_cast<int>(world.contract));
    ctx.save.Set(key, std::max(ctx.save.Get(key, 0), found->second.score));
    ctx.save.Set("NoTrace:LastMaestroDeniedReason", found->second.maestroDeniedReason);
    ctx.save.Set("NoTrace:Operations", ctx.save.Get("NoTrace:Operations", 0) + 1);
    LOG_INFO("NO TRACE: contrato {}, rango {}, puntos {}, éxito {}", static_cast<int>(world.contract), RankName(found->second.rank), found->second.score, found->second.success);
  }

  void Game::PlayCue(const std::string &file, float volume, float pan, float pitch) {
    for (std::size_t index = 0; index < AudioConfig::Files.size(); ++index) {
      const auto kind = static_cast<SoundKind>(index);
      if (file == AudioConfig::File(kind)) { audioPlayback_.Play(kind, volume, pan, pitch); return; }
    }
  }

  void Game::OnPause(bool paused) {
    if (paused) { showOrders_ = false; worldRenderer_.SetNamesVisible(false); private_->setVisible(false); }
    if (paused) pendingPuzzleHeld_ = false;
    paused_ = paused; session_.Submit({}); root_->setEnabled(!paused);
    if (paused) { preparingDecoy_ = false; cancelledDecoy_ = true; worldRenderer_.SetDecoyPreview(false, {}, 0.f); }
    if (paused) cqcBlocked_ = true;
    for (int action = 0; action < Count; ++action) held_[action] = ctx.input.Pressed(action);
    SyncFirstPersonInput();
  }

  void Game::OnCleanup() {
    ctx.audio.StopMusic(MusicConfig::SceneFadeOutSeconds);
    LeaveFirstPerson(); ctx.cursor.SetCaptured(false); ctx.cursor.SetVisible(true);
    preparingDecoy_ = false; worldRenderer_.SetDecoyPreview(false, {}, 0.f);
    session_.Stop(); audioPlayback_.StopAmbience(); ctx.audio.StopAllVoices(); ctx.gui.Remove(root_); ctx.cursor.SetHovered(false);
    ctx.renderer.ResetView(); ctx.renderer.SetPostProcessingEnabled(true); ctx.renderer.SetTextureSmoothing(true);
  }
}
