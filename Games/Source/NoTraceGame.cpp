#include "NoTraceGame.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTracePreparationConfig.h"
#include "Config/NoTraceConfig.h"
#include "Core/EngineConfig.h"
#include "Utils/Log.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>

namespace NoTrace {
  namespace {
    std::string StanceName(Stance stance) {
      switch (stance) {
      case Stance::Crouched: return "AGACHADO";
      case Stance::Prone: return "TUMBADO / X para arrastrarse";
      default: return "DE PIE";
      }
    }
  }

  Game::Game(EngineContext &context)
      : Scene(context), session_(ctx.network, ctx.random), worldRenderer_(context) {
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
    mission_ = label("", 30.f, 56.f, 1160.f, 20);
    agent_ = label("", 30.f, 83.f, 1420.f, 18);
    auto lobby = tgui::Button::create("BASE / SALA  [F2]");
    lobby->setPosition({1260.f, 22.f}); lobby->setSize({310.f, 50.f}); lobby->setTextSize(20);
    lobby->onPress([this] { if (!session_.IsConnected()) Open(Page::Connection); else if (session_.GetWorld().phase == GamePhase::Base) Open(Page::Deployment); });
    root_->add(lobby);
    auto footer = tgui::Panel::create({"100%", 150.f});
    footer->setPosition({0.f, 750.f}); footer->getRenderer()->setBackgroundColor({5, 12, 20, 235});
    footer->getRenderer()->setBorders(0); root_->add(footer);
    hint_ = label("", 30.f, 758.f, 1530.f, 22);
    message_ = label("", 30.f, 798.f, 1530.f, 18);
    label("WASD mover · Mayús correr · C agacharse · Z tumbarse · X arrastrarse · E interactuar", 30.f, 832.f, 1530.f, 17);
    label("Q CQC · clic izq. letal / der. no letal · G señuelo · K golpe · B arrastrar cuerpo · T herramienta · Tab orden privada", 30.f, 863.f, 1550.f, 16);
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
    const std::array<sf::Keyboard::Scan, Count> keys{
      sf::Keyboard::Scan::A, sf::Keyboard::Scan::D, sf::Keyboard::Scan::W, sf::Keyboard::Scan::S,
      sf::Keyboard::Scan::LShift, sf::Keyboard::Scan::C, sf::Keyboard::Scan::Z, sf::Keyboard::Scan::X,
      sf::Keyboard::Scan::E, sf::Keyboard::Scan::Q, sf::Keyboard::Scan::Unknown, sf::Keyboard::Scan::Unknown,
      sf::Keyboard::Scan::G, sf::Keyboard::Scan::K, sf::Keyboard::Scan::B, sf::Keyboard::Scan::T,
      sf::Keyboard::Scan::Tab, sf::Keyboard::Scan::F2
    };
    for (int action = 0; action < Count; ++action) if (keys[action] != sf::Keyboard::Scan::Unknown) ctx.input.Bind(action, Input::Keyboard{keys[action]});
    ctx.input.Bind(Lethal, Input::Mouse{sf::Mouse::Button::Left});
    ctx.input.Bind(NonLethal, Input::Mouse{sf::Mouse::Button::Right});
    ctx.input.Bind(Left, Input::Keyboard{sf::Keyboard::Scan::Left});
    ctx.input.Bind(Right, Input::Keyboard{sf::Keyboard::Scan::Right});
    ctx.input.Bind(Up, Input::Keyboard{sf::Keyboard::Scan::Up});
    ctx.input.Bind(Down, Input::Keyboard{sf::Keyboard::Scan::Down});
  }

  void Game::Start() {
    session_.Stop(); held_.fill(false); previousPhase_ = GamePhase::Base;
    ready_ = false; paused_ = false; showOrders_ = false; lastAudioEvent_ = 0;
    BindInputs(); ctx.renderer.ResetView(); ctx.renderer.SetPostProcessingEnabled(false);
    ctx.renderer.SetTextureSmoothing(false); ctx.cursor.SetVisible(true); ctx.cursor.SetHovered(false);
    root_->setEnabled(true); ctx.gui.Add(root_); Open(Page::Connection); RefreshHud();
  }

  bool Game::Pressed(Action action, bool edge) {
    const bool down = ctx.input.Pressed(action);
    const bool result = down && (!edge || !held_[action]); held_[action] = down; return result;
  }

  const Agent *Game::LocalAgent() const {
    for (const auto &agent : session_.GetWorld().agents) if (agent.id == session_.GetLocalPlayerId()) return &agent;
    return nullptr;
  }

  void Game::Update() {
    if (page_ != Page::None || !session_.IsConnected()) {
      session_.Submit({}); held_.fill(false); return;
    }
    if (Pressed(Lobby, true) && session_.GetWorld().phase == GamePhase::Base) { Open(Page::Deployment); return; }
    if (Pressed(Orders, true)) showOrders_ = !showOrders_;
    const auto *agent = LocalAgent();
    if (!agent) return;
    ActionCommand command;
    const sf::Vector2f screenMove{static_cast<float>(Pressed(Right)) - static_cast<float>(Pressed(Left)), static_cast<float>(Pressed(Down)) - static_cast<float>(Pressed(Up))};
    const auto &world = session_.GetWorld();
    auto direction = worldRenderer_.ScreenToGround({800.f + screenMove.x, 480.f + screenMove.y}, world, agent->id) - worldRenderer_.ScreenToGround({800.f, 480.f}, world, agent->id);
    const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length > 0.f) direction /= length;
    command.moveX = direction.x; command.moveY = direction.y;
    const auto cursor = ctx.cursor.GetPosition();
    auto aim = worldRenderer_.ScreenToGround(cursor, world, agent->id) - agent->position;
    const float aimLength = std::sqrt(aim.x * aim.x + aim.y * aim.y);
    if (aimLength > 1.f) aim /= aimLength; else aim = agent->facing;
    command.aimX = aim.x; command.aimY = aim.y;
    command.run = Pressed(Run); command.crouch = Pressed(Crouch, true); command.prone = Pressed(Prone, true);
    command.crawl = Pressed(Crawl); command.interact = Pressed(Interact, true);
    command.cqc = Pressed(Cqc, true); command.throwDecoy = Pressed(Decoy, true); command.knock = Pressed(Knock, true);
    command.dragBody = Pressed(Drag, true); command.tool = Pressed(Tool, true);
    const bool lethal = Pressed(Lethal, true), nonLethal = Pressed(NonLethal, true);
    if (cursor.y > 116.f && cursor.y < 750.f) { command.fireLethal = lethal; command.fireNonLethal = nonLethal; }
    if (command.interact && world.phase == GamePhase::Base) {
      for (const auto &object : world.objects) {
        const auto delta = object.position - agent->position;
        if (delta.x * delta.x + delta.y * delta.y >= GameplayConfig::InteractionDistance * GameplayConfig::InteractionDistance) continue;
        switch (object.kind) {
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
    session_.Tick(ctx.time.GetDeltaTime(), paused_ || page_ != Page::None);
    const auto phase = session_.GetWorld().phase;
    if (phase != previousPhase_) {
      if (phase == GamePhase::Mission) { ClosePanel(); lastAudioEvent_ = 0; PlayCue("Bonus.mp3"); }
      else if (phase == GamePhase::Results) { PersistResults(); Open(Page::Results); }
      else { ready_ = false; ClosePanel(); }
      previousPhase_ = phase;
    }
    if (session_.IsConnected()) {
      for (const auto &event : session_.GetWorld().events) if (event.sequence > lastAudioEvent_) {
        if (event.actor == session_.GetLocalPlayerId() && (event.kind == EventKind::WeaponLethal || event.kind == EventKind::WeaponNonLethal)) PlayCue("Laser.mp3");
        else if (event.kind == EventKind::Objective || event.kind == EventKind::Extracted) PlayCue("Bonus.mp3");
        lastAudioEvent_ = event.sequence;
      }
    }
    RefreshHud();
  }

  void Game::Render() const { worldRenderer_.Render(session_.GetWorld(), session_.GetLocalPlayerId()); }

  void Game::Open(Page page) {
    page_ = page; session_.Submit({}); panel_->removeAllWidgets(); panel_->setVisible(true);
    status_.reset(); roster_.reset(); briefing_.reset(); deploy_.reset(); ctx.cursor.SetHovered(false);
    if (page == Page::Connection) BuildConnection();
    else if (page == Page::Results) BuildResults();
    else BuildStation(page);
  }

  void Game::ClosePanel() {
    page_ = Page::None; panel_->setVisible(false); panel_->setFocused(false);
    for (int action = 0; action < Count; ++action) held_[action] = ctx.input.Pressed(action);
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
    Button("CREAR SALA LAN", 374.f, [this, identity, readPort] {
      const auto value = readPort();
      if (!value) { status_->setText("Puerto inválido (1–65535)."); return; }
      if (session_.Host(identity(), value)) Open(Page::Deployment); else status_->setText(session_.GetStatus());
    });
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
        Button(std::format("{} · {} letales / {} no letales / {} señuelos / {} herramientas", PreparationConfig::Names[index], loadout.lethalAmmo, loadout.nonLethalAmmo, loadout.decoys, loadout.tools),
               160.f + static_cast<float>(index) * 75.f, [this, loadout] { session_.SetLoadout(loadout); ClosePanel(); });
      }
    } else if (page == Page::Intelligence) {
      Label("CENTRO DE INTELIGENCIA", {30.f, 20.f}, {714.f, 45.f}, 28);
      Label(contract.name + "\n\n" + contract.briefing + "\n\nLos conos muestran el campo de observación. La sospecha debe llenarse para confirmar una detección. El ruido atrae a su origen; no revela automáticamente al agente.\n\nCobertura baja: agáchate. Cobertura alta: rompe la línea de visión. Escondites: E entra o esconde un cuerpo arrastrado.\n\nCQC: cuatro pulsaciones Q separadas (agarre, derribo, inmovilización, incapacitación). B arrastra; E reanima a un compañero.\n\nRepetidor: E para iniciar; otro agente cercano acelera su operación. En solitario también es accesible.\n\nTab muestra únicamente tu orden privada. Las órdenes de los demás permanecen ocultas.", {30.f, 82.f}, {714.f, 425.f}, 18);
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

  void Game::BuildResults() {
    const auto &world = session_.GetWorld();
    Label("INFORME DE OPERACIÓN", {30.f, 20.f}, {714.f, 45.f}, 28);
    const auto found = world.results.find(session_.GetLocalPlayerId());
    if (found != world.results.end()) {
      const auto &result = found->second;
      Label(std::string(RankName(result.rank)), {30.f, 90.f}, {714.f, 65.f}, 42);
      Label(std::format("{}  ·  {} puntos\n\n{}\n\nObjetivos: {}/{}    Secundarios: {}\nAlarmas confirmadas: {}    Tiempo: {:.0f} s\n\n{}", result.success ? "OPERACIÓN CUMPLIDA" : "OPERACIÓN FALLIDA", result.score, result.reason, world.objectives, GetContract(world.contract).requiredObjectives, world.secondary, world.alarmCount, world.elapsed, result.maestroEligible ? "Disciplina Maestro: sin armas, CQC ni detección." : "La puntuación conserva cada acción y detección de la misión."), {30.f, 175.f}, {714.f, 290.f}, 21);
    } else Label("Esperando el informe del anfitrión…", {30.f, 100.f}, {714.f, 100.f}, 24);
    auto back = Button("REGRESAR A LA BASE", 475.f, [this] { session_.ReturnToBase(); }); back->setEnabled(!session_.IsClient());
    Button("SALIR AL MENÚ", 535.f, [this] { ctx.scenes.ChangeScene("Menu"); });
  }

  void Game::RefreshHud() {
    const auto &world = session_.GetWorld();
    const auto &contract = GetContract(world.contract);
    title_->setText(world.phase == GamePhase::Base ? "NO TRACE  //  BASE CIO" : "NO TRACE  //  " + contract.name);
    mission_->setText(world.phase == GamePhase::Base ? "PREPARACIÓN: operaciones · armería · inteligencia · despliegue" : std::format("OBJETIVOS {}/{}  ·  SECUNDARIOS {}  ·  {:.0f} s  ·  {}  ·  SEMILLA {}", world.objectives, contract.requiredObjectives, world.secondary, std::max(0.f, contract.timeLimit - world.elapsed), world.alarm ? "ALARMA" : "SIN ALARMA", world.seed));
    if (const auto *agent = LocalAgent()) {
      agent_->setText(std::format("{}  |  {}{}  |  Letal {} · No letal {} · Señuelos {} · Herramientas {}", agent->name, StanceName(agent->stance), !agent->alive ? " / ELIMINADO" : agent->incapacitated ? " / INCAPACITADO: un compañero puede reanimarte" : agent->hidden ? " / OCULTO" : agent->extracted ? " / EXTRAÍDO" : "", agent->loadout.lethalAmmo, agent->loadout.nonLethalAmmo, agent->loadout.decoys, agent->loadout.tools));
      private_->setText(tgui::String(std::string(PrivateOrderText(agent->privateOrder)) + (agent->privateComplete ? "\nORDEN CUMPLIDA" : "")));
      private_->setVisible(showOrders_ && world.phase == GamePhase::Mission && page_ == Page::None);
      std::string hint = world.phase == GamePhase::Base ? "Recorre la base y pulsa E en una estación. F2 abre la sala de despliegue." : "Recupera los objetivos y vuelve al punto verde de extracción. Tab: orden privada.";
      float nearest = GameplayConfig::InteractionDistance;
      for (const auto &object : world.objects) {
        const auto d = object.position - agent->position; const float distance = std::sqrt(d.x * d.x + d.y * d.y);
        if (distance < nearest) { nearest = distance; hint = "E  //  " + object.name; if (object.kind == ObjectKind::Camera || object.kind == ObjectKind::AlarmPanel) hint = "T  //  " + object.name + " — requiere herramienta"; }
      }
      hint_->setText(tgui::String(hint));
    } else { agent_->setText("Accede a CIO para preparar tu operación."); hint_->setText("Elige individual, crea una sala o conecta por IP local."); private_->setVisible(false); }
    message_->setText(tgui::String(session_.IsConnected() ? world.message : session_.GetStatus()));
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
    ctx.save.Set("NoTrace:Operations", ctx.save.Get("NoTrace:Operations", 0) + 1);
    LOG_INFO("NO TRACE: contrato {}, rango {}, puntos {}, éxito {}", static_cast<int>(world.contract), RankName(found->second.rank), found->second.score, found->second.success);
  }

  void Game::PlayCue(const std::string &file) {
    if (auto *buffer = ctx.resources.FetchSound(file)) { cue_ = std::make_unique<sf::Sound>(*buffer); cue_->setVolume(32.f); cue_->play(); }
  }

  void Game::OnPause(bool paused) {
    paused_ = paused; session_.Submit({}); root_->setEnabled(!paused);
    for (int action = 0; action < Count; ++action) held_[action] = ctx.input.Pressed(action);
  }

  void Game::OnCleanup() {
    session_.Stop(); cue_.reset(); ctx.gui.Remove(root_); ctx.cursor.SetHovered(false);
    ctx.renderer.ResetView(); ctx.renderer.SetPostProcessingEnabled(true); ctx.renderer.SetTextureSmoothing(true);
  }
}
