#include "NoTraceGame.h"
#include "Bounce.h"
#include "Clicker.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceManeuverConfig.h"
#include "Config/NoTracePreparationConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include "Config/NoTraceConfig.h"
#include "Core/EngineConfig.h"
#include "Core/Overlay.h"
#include "NoTraceWorldGuiFixtures.h"
#include "NoTraceOptionsGuiTests.h"
#include "NoTraceMenuGuiTests.h"
#include "NoTraceWorldGeometry.h"
#include <SFML/Graphics/Image.hpp>
#include <SFML/System/Sleep.hpp>
#include <TGUI/Text.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <format>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace EngineTests {
  using namespace NoTrace;
  struct GuiAccess {
    static void UseSyntheticInput(EngineContext &context) { context.input.hardwarePolling_ = false; }
    static Session &GetSession(Game &game) { return game.session_; }
    static void VerifyCursorSampling(EngineContext &context, sf::RenderWindow &window) {
      const sf::Vector2f initial{500.f,300.f}; context.cursor.SetPosition(initial);
      sf::Mouse::setPosition(window.mapCoordsToPixel({650.f,410.f},context.window.GetPresentationView()),window);
      const auto native=sf::Mouse::getPosition(window);
      Require(context.cursor.GetPosition()==initial,"El cursor vuelve a consultar el ratón entre la simulación y el dibujo");
      context.cursor.Update(0.f);
      const auto expected=window.mapPixelToCoords(native,context.window.GetPresentationView());
      Require((context.cursor.GetPosition()-expected).length()<3.f,"La actualización del cursor no toma la posición real con la presentación actual");
      sf::View view(sf::FloatRect({100.f,200.f},{400.f,300.f})); view.setViewport({{.1f,.1f},{.8f,.8f}});
      const auto presentation=context.window.GetViewport(); auto composed=view;
      composed.setViewport({presentation.position+view.getViewport().position.componentWiseMul(presentation.size),presentation.size.componentWiseMul(view.getViewport().size)});
      const auto pixel=window.mapCoordsToPixel(context.cursor.GetPosition(),context.window.GetPresentationView());
      Require((context.cursor.GetPosition(view)-window.mapPixelToCoords(pixel,composed)).length()<.001f,"La posición cacheada no comparte la transformación del viewport de escena");
    }
    static void VerifyMainMenu(EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output) {
      VerifyMainMenuOptions(context,window,output,[&] { return context.gui.gui_.getWidgets(); },
        [&](Menu &menu,const std::filesystem::path &file) {
          const auto previous=context.time.deltaTime_; context.time.deltaTime_=sf::seconds(1.f); menu.Update(); context.time.deltaTime_=previous;
          CaptureExample(menu,context,window,file);
        });
    }
    static void Require(bool condition, const std::string &message) {
      if (!condition) throw std::runtime_error(message);
    }
    static bool HasButton(Game &game, const std::string &text) {
      for (const auto &widget : game.panel_->getWidgets()) if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget); button && button->getText().toStdString().starts_with(text)) return true;
      return false;
    }
    static void Escape(Game &game) {
      sf::Event::KeyPressed pressed; pressed.scancode = sf::Keyboard::Scan::Escape;
      game.OnEvent(pressed);
    }
    static void VerifyOverlay(EngineContext &context) {
      Overlay overlay(context.gui);
      const auto group = context.gui.gui_.getWidgets().back();
      Require(!overlay.IsVisible() && !group->isEnabled(), "El overlay oculto conserva entradas habilitadas");
      overlay.SetVisible(true);
      Require(overlay.IsVisible() && group->isEnabled(), "El overlay de los ejemplos no se habilita");
      const auto container = std::dynamic_pointer_cast<tgui::Container>(group);
      const auto layout = std::dynamic_pointer_cast<tgui::Container>(container->getWidgets().back());
      const auto resume = std::dynamic_pointer_cast<tgui::Button>(layout->getWidgets().front());
      resume->onPress.emit(resume.get(), resume->getText());
      Require(overlay.FetchSelection() == OverlaySelection::Resume, "El overlay genérico perdió su acción Reanudar");
      resume->onPress.emit(resume.get(), resume->getText());
      overlay.SetVisible(false);
      Require(!group->isEnabled() && !overlay.FetchSelection(), "El overlay oculto retiene entradas o selección");
      context.gui.Remove(group);
    }
    static void VerifyProductionSave() {
      struct RestoreEnvironment {
        std::wstring previous;
        RestoreEnvironment() { if (const auto *value=_wgetenv(L"AWESOME_ENGINE_SAVE_FILE")) previous=value; }
        ~RestoreEnvironment() { _wputenv_s(L"AWESOME_ENGINE_SAVE_FILE",previous.c_str()); }
      } restore;
      const auto path=std::filesystem::absolute(L"production-save-test/Partida compartida ñ.json");
      std::filesystem::create_directories(path.parent_path());
      _wputenv_s(L"AWESOME_ENGINE_SAVE_FILE",path.c_str());
      { SaveManager save; save.Set("ProductionPersistence",55); }
      { SaveManager save; Require(save.Get("ProductionPersistence",0)==55,"El guardado común de producción no persiste al reabrir SaveManager"); }
    }
    static void VerifyHud(Game &game) {
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      auto &agent = world.agents.front();
      const float health = agent.health, stamina = agent.stamina;
      agent.health = 37.f; agent.stamina = 12.f; game.RefreshHud();
      Require(game.health_->isVisible() && game.health_->getValue() == static_cast<unsigned int>(std::lround(37.f / GameplayConfig::MaxHealth * 100.f)) && game.stamina_->getValue() == 12, "El HUD no refleja salud y resistencia de la simulación");
      unsigned int bars = 0;
      for (const auto &widget : game.root_->getWidgets()) bars += std::dynamic_pointer_cast<tgui::ProgressBar>(widget) ? 1u : 0u;
      const float suppressor = agent.selectedWeapon == WeaponKind::Pistol ? agent.pistolSuppressor : agent.nonLethalSuppressor;
      Require(bars == 3 && game.suppressor_->isVisible() && game.suppressor_->getValue() == static_cast<unsigned int>(suppressor), "El HUD debe conservar salud, resistencia y desgaste del silenciador del jugador local");
      agent.health = health; agent.stamina = stamina; game.RefreshHud();
    }
    static void VerifyGameOverAndRetry(Game &game) {
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      const auto seed = world.seed;
      world.agents[0].lifeState = LifeState::Incapacitated; world.agents[0].incapacitated = true; world.agents[0].health = 0.f; world.agents[0].incapacityRemaining = GameplayConfig::IncapacityDuration;
      Step(game);
      Require(world.phase == GamePhase::Results && !world.results.at(1).success && game.page_ == Game::Page::Results, "El desmayo no abre la derrota separada de la pausa");
      Require(HasButton(game, "REINTENTAR") && HasButton(game, "REGRESAR A LA BASE"), "La derrota no conserva las acciones de recuperación");
      Press(game, "REINTENTAR OPERACIÓN");
      Require(world.phase == GamePhase::Mission && world.seed == seed && !world.agents[0].incapacitated && game.page_ == Game::Page::None, "Reintentar no restablece la operación tras el desmayo");
    }
    static void VerifyAudioRestart(Game &game) {
      const auto previous = game.lastSound_;
      const auto *agent = game.LocalAgent();
      const auto expectedSound = agent->pistolSuppressorEquipped && agent->pistolSuppressor > 0.f ? SoundKind::Suppressed : SoundKind::Pistol;
      ActionCommand fire; fire.fireLethal = true; fire.aimX = 1.f;
      game.session_.Submit(fire); Step(game);
      const auto &sounds = game.session_.GetWorld().sounds;
      Require(std::ranges::any_of(sounds, [previous, expectedSound](const SoundEvent &sound) { return sound.sequence > previous && sound.kind == expectedSound; }),
        "El disparo posterior al reinicio no genera una señal de audio nueva");
      Require(!sounds.empty() && game.lastSound_ == sounds.back().sequence && game.lastSound_ > previous && !game.voices_.empty(),
        "Reiniciar la misión impide reproducir las nuevas señales de audio de la simulación");
      const auto played = game.lastSound_;
      game.UpdateBackground();
      Require(game.lastSound_ == played, "Actualizar la GUI vuelve a consumir la señal de audio ya reproducida");
    }
    static void VerifyPauseLifecycle(Game &game, EngineContext &context) {
      Escape(game); Require(game.page_ == Game::Page::Pause, "No se abrió la pausa antes de perder foco");
      game.OnPause(true);
      Require(!game.root_->isEnabled() && game.paused_, "Perder foco no neutraliza la GUI local");
      game.OnPause(false);
      Require(game.root_->isEnabled() && game.page_ == Game::Page::Pause, "Recuperar foco cierra indebidamente la pausa propia");
      Press(game, "OPCIONES"); game.OnPause(true);
      context.gui.SetDisplayConfirmation(true, 5.f);
      Require(!game.root_->isEnabled() && context.gui.displayConfirmationGroup_->isVisible(), "La confirmación de pantalla no bloquea las opciones");
      context.gui.SetDisplayConfirmation(false, 0.f);
      Require(!game.root_->isEnabled(), "Cerrar el modal habilita controles antes de resolver la pausa de escena");
      game.OnPause(false);
      Require(game.root_->isEnabled() && game.page_ == Game::Page::Options, "Resolver la confirmación pierde la página de opciones");
      Escape(game); Escape(game);
    }
    static void VerifyZoom(Game &game, EngineContext &context) {
      context.input.BeginFrame();
      sf::Event::MouseWheelScrolled wheel; wheel.wheel = sf::Mouse::Wheel::Vertical; wheel.delta = 2.f;
      const auto origin = game.LocalAgent()->position;
      const auto barPosition = game.health_->getPosition();
      const float zoom = game.worldRenderer_.GetZoom();
      context.input.CaptureEvent(wheel); game.Update(); Step(game); context.input.BeginFrame();
      Require(game.worldRenderer_.GetZoom() > zoom, "La rueda no llega al zoom de la presentación");
      Require(game.LocalAgent()->position == origin && game.health_->getPosition() == barPosition, "El zoom alteró la posición física o el HUD");
      const float magnified = game.worldRenderer_.GetZoom();
      Escape(game); context.input.CaptureEvent(wheel); game.Update(); context.input.BeginFrame();
      Require(game.worldRenderer_.GetZoom() == magnified, "La rueda modifica el mundo mientras el menú está abierto");
      Escape(game); game.worldRenderer_.ResetZoom();
    }
    static void VerifyOptionsKeyboard(Game &game) {
      tgui::Slider::Ptr volume;
      for (const auto &widget : game.panel_->getWidgets()) if (const auto slider = std::dynamic_pointer_cast<tgui::Slider>(widget)) { volume = slider; break; }
      Require(volume != nullptr, "No existe control de volumen en la pausa");
      volume->setFocused(true);
      sf::Event::KeyPressed key; key.scancode = sf::Keyboard::Scan::Right; game.OnEvent(key);
      Require(volume->getValue() == 1.f, "El teclado no ajusta el volumen desde la pausa");
      key.scancode = sf::Keyboard::Scan::Home; game.OnEvent(key);
      Require(volume->getValue() == 0.f, "Home no restaura el volumen mínimo");
    }
    static void Key(Game &game, EngineContext &context, sf::Keyboard::Scan scan) {
      context.input.BeginFrame();
      sf::Event::KeyPressed pressed; pressed.scancode = scan; context.input.CaptureEvent(pressed);
      sf::Event::KeyReleased released; released.scancode = scan; context.input.CaptureEvent(released);
      game.Update(); Step(game); context.input.BeginFrame();
    }
    static void VerifyAutomaticCrawl(Game &game, EngineContext &context) {
      Key(game, context, sf::Keyboard::Scan::Z);
      Require(game.LocalAgent()->stance == Stance::Prone && !game.LocalAgent()->moving, "Z no deja al agente tumbado inmóvil");
      const auto origin = game.LocalAgent()->position;
      Key(game, context, sf::Keyboard::Scan::D);
      const float distance = (game.LocalAgent()->position - origin).length();
      Require(game.LocalAgent()->stance == Stance::Prone && game.LocalAgent()->moving && distance > 0.1f && distance < GameplayConfig::WalkSpeed / GameplayConfig::SimulationHz, "Moverse tumbado exige aún una acción adicional o usa velocidad de pie");
      game.Update(); Step(game); context.input.BeginFrame();
      const auto stopped = game.LocalAgent()->position;
      Require(game.LocalAgent()->stance == Stance::Prone && !game.LocalAgent()->moving, "Al soltar movimiento no permanece tumbado");
      Key(game, context, sf::Keyboard::Scan::A);
      Require(game.LocalAgent()->stance == Stance::Prone && game.LocalAgent()->position != stopped, "No reanuda el arrastre al cambiar de dirección");
      game.Update(); Step(game); context.input.BeginFrame();
      Key(game, context, sf::Keyboard::Scan::Z);
      Require(game.LocalAgent()->stance == Stance::Standing, "Z no permite levantarse después del arrastre");
    }
    static void VerifyTacticalControls(Game &game, EngineContext &context) {
      context.input.Clear(); game.BindInputs(); game.held_.fill(false);
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      const auto saved = world;
      world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear(); world.events.clear();
      auto &agent = world.agents[0]; agent.position = {150.f, 350.f}; agent.facing = {1.f, 0.f}; agent.actionCooldown = 0.f;
      context.cursor.SetPosition({1050.f, 480.f});
      const auto press = [&](sf::Keyboard::Scan scan) {
        context.input.BeginFrame(); sf::Event::KeyPressed event; event.scancode = scan; context.input.CaptureEvent(event); game.Update(); Step(game);
      };
      const auto release = [&](sf::Keyboard::Scan scan) {
        context.input.BeginFrame(); sf::Event::KeyReleased event; event.scancode = scan; context.input.CaptureEvent(event); game.Update(); Step(game);
      };
      const int initialDecoys = agent.loadout.decoys;
      agent.actionCooldown = 0.5f;
      press(sf::Keyboard::Scan::G);
      Require(!game.preparingDecoy_, "G ofrece un lanzamiento disponible durante el tiempo de otra acción");
      release(sf::Keyboard::Scan::G); agent.actionCooldown = 0.f;
      press(sf::Keyboard::Scan::G);
      Require(game.preparingDecoy_ && agent.loadout.decoys == initialDecoys, "Mantener G no prepara el señuelo antes de consumirlo");
      release(sf::Keyboard::Scan::G);
      Require(!game.preparingDecoy_ && agent.loadout.decoys == initialDecoys - 1 && world.decoys.size() == 1 && world.noises.empty(), "Soltar G no envía un señuelo físico silencioso durante su vuelo");
      for (int frame = 0; frame < 90 && !world.decoys.empty(); ++frame) { context.input.BeginFrame(); game.Update(); Step(game); }
      Require(world.decoys.empty() && std::ranges::any_of(world.noises, [](const NoiseEvent &noise) { return noise.kind == SoundKind::Decoy; }), "El señuelo no genera su ruido autoritativo al aterrizar");
      agent.actionCooldown = 0.f; press(sf::Keyboard::Scan::G); Escape(game);
      Require(!game.preparingDecoy_ && game.page_ == Game::Page::None, "Esc no cancela primero la preparación del señuelo");
      release(sf::Keyboard::Scan::G);
      Require(agent.loadout.decoys == initialDecoys - 1, "Cancelar y soltar G lanzó un señuelo involuntario");
      const int nonLethalAmmo = agent.loadout.nonLethalAmmo;
      context.input.BeginFrame();
      sf::Event::KeyPressed prepare; prepare.scancode = sf::Keyboard::Scan::G; context.input.CaptureEvent(prepare);
      context.input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Right, {1050, 480}});
      game.Update(); Step(game); release(sf::Keyboard::Scan::G);
      Require(!game.preparingDecoy_ && agent.loadout.nonLethalAmmo == nonLethalAmmo, "Cancelar G con clic derecho en el mismo frame disparó el arma");
      context.input.BeginFrame(); context.input.CaptureEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Right, {1050, 480}}); game.Update(); Step(game);
      press(sf::Keyboard::Scan::G); game.Open(Game::Page::Pause); game.ClosePanel();
      press(sf::Keyboard::Scan::G);
      Require(!game.preparingDecoy_, "Reanudar con G mantenida prepara otro lanzamiento sin pulsación nueva");
      release(sf::Keyboard::Scan::G);
      Require(agent.loadout.decoys == initialDecoys - 1, "Abrir la pausa y soltar G conservó un lanzamiento cancelado");
      world.noises.clear(); agent.actionCooldown = 0.f;
      press(sf::Keyboard::Scan::Q);
      Require(game.page_ == Game::Page::Weapons && game.weaponSlots_[0] && game.weaponSlots_[1] && game.weaponSlots_[0]->getText().empty() && game.weaponSlots_[1]->getText().empty() && world.noises.empty(), "Q no abre el equipo táctico o produce una distracción involuntaria");
      Escape(game); release(sf::Keyboard::Scan::Q);
      Guard body; body.id = 77; body.position = agent.position + sf::Vector2f{20.f, 0.f}; body.state = GuardState::Incapacitated; body.incapacityRemaining = 60.f; world.guards.push_back(body);
      press(sf::Keyboard::Scan::E);
      Require(agent.draggingGuard == 77, "E mantenida no inicia arrastre contextual");
      press(sf::Keyboard::Scan::E);
      Require(agent.draggingGuard == 77, "Mantener E alterna o interrumpe el arrastre");
      release(sf::Keyboard::Scan::E);
      Require(agent.draggingGuard < 0, "Soltar E no termina el arrastre");
      world.guards.clear(); world.obstacles.push_back({{{200.f, 150.f}, {40.f, 100.f}}, CoverKind::High, true, "Pared"});
      agent.position = {187.99f, 155.f}; agent.cover = {};
      press(sf::Keyboard::Scan::LAlt); release(sf::Keyboard::Scan::LAlt);
      Require(agent.cover.active, "ALT no activa la cobertura desde la entrada del juego");
      const float anchorY = agent.cover.anchor.y;
      context.input.BeginFrame();
      context.input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Right, {1050, 480}});
      sf::Event::KeyPressed right; right.scancode = sf::Keyboard::Scan::D; context.input.CaptureEvent(right);
      game.Update(); Step(game);
      Require(agent.aiming && agent.cover.peekSide == PeekSide::Left && agent.position.y < anchorY, "Apuntar con clic derecho y dirección no asoma al extremo de la pared");
      context.input.Clear(); game.BindInputs(); game.held_.fill(false); world = saved;
    }
    static tgui::Button::Ptr FindButton(Game &game, const std::string &text) {
      for (const auto &widget : game.panel_->getWidgets()) {
        if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget)) {
          if (button->getText().toStdString().starts_with(text)) return button;
        }
      }
      throw std::runtime_error("No existe el botón: " + text);
    }
    static void Press(Game &game, const std::string &text) {
      const auto button = FindButton(game, text);
      Require(button->isEnabled(), "El botón está deshabilitado: " + text);
      button->onPress.emit(button.get(), button->getText());
      game.UpdateBackground();
    }
    static void Click(Game &game, EngineContext &context, sf::RenderWindow &window, const std::string &text) {
      const auto button = FindButton(game, text);
      const auto position = button->getAbsolutePosition() + button->getSize() / 2.f;
      const auto pixel = window.mapCoordsToPixel({position.x, position.y});
      context.gui.ProcessEvent(sf::Event::MouseMoved{pixel});
      context.gui.ProcessEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, pixel});
      context.gui.ProcessEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, pixel});
      game.UpdateBackground();
    }
    static void VerifyFireInput(Game &game, EngineContext &context, sf::RenderWindow &window) {
      context.input.Clear(); game.BindInputs(); game.held_.fill(false);
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      const auto saved = world;
      const auto savedSound = game.lastSound_;
      world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear(); world.events.clear();
      auto &agent = world.agents[0]; agent.position = {300.f, 350.f}; agent.stance = Stance::Standing; agent.cover = {}; agent.actionCooldown = 0.f;
      agent.selectedWeapon = WeaponKind::Pistol; agent.reloadRemaining = 0.f;
      agent.loadout.lethalAmmo = 6; agent.loadout.nonLethalAmmo = 6;
      agent.lethalMagazine = WeaponConfig::PistolCapacity; agent.nonLethalMagazine = WeaponConfig::NonLethalCapacity;
      agent.pistolSuppressor = agent.nonLethalSuppressor = WeaponConfig::MaximumSuppressor;
      agent.pistolSuppressorEquipped = agent.nonLethalSuppressorEquipped = true;
      context.cursor.SetPosition({1000.f, 480.f});
      const auto idle = [&](int frames = 1) {
        for (int frame = 0; frame < frames; ++frame) { context.input.BeginFrame(); game.Update(); Step(game); }
      };
      const auto mouse = [&](sf::Mouse::Button button, bool pressed) {
        context.input.BeginFrame();
        context.cursor.SetPosition({1000.f, 480.f});
        if (pressed) context.input.CaptureEvent(sf::Event::MouseButtonPressed{button, {1000, 480}});
        else context.input.CaptureEvent(sf::Event::MouseButtonReleased{button, {1000, 480}});
        game.Update(); Step(game);
      };
      const auto fire = [&] {
        context.input.BeginFrame();
        context.cursor.SetPosition({1000.f, 480.f});
        context.input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, {1000, 480}});
        context.input.CaptureEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, {1000, 480}});
        game.Update(); Step(game);
      };
      const auto clickPanel = [&](int index) {
        const auto button = game.weaponSlots_[index];
        const auto position = button->getAbsolutePosition() + button->getSize() / 2.f;
        const auto pixel = window.mapCoordsToPixel({position.x, position.y});
        context.input.BeginFrame(); context.cursor.SetPosition({position.x, position.y});
        context.gui.ProcessEvent(sf::Event::MouseMoved{pixel});
        const sf::Event pressed = sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, pixel};
        const sf::Event released = sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, pixel};
        context.input.CaptureEvent(pressed); context.gui.ProcessEvent(pressed);
        context.input.CaptureEvent(released); context.gui.ProcessEvent(released);
        Require(game.page_ == Game::Page::Weapons && game.pendingWeaponChoice_ == index, "El clic real no alcanza la silueta del arma");
        game.Update(); Step(game);
      };
      const auto q = [&](bool held) {
        context.input.BeginFrame();
        if (held) { sf::Event::KeyPressed event; event.scancode=sf::Keyboard::Scan::Q; context.input.CaptureEvent(event); }
        else { sf::Event::KeyReleased event; event.scancode=sf::Keyboard::Scan::Q; context.input.CaptureEvent(event); game.OnEvent(event); }
        game.Update(); Step(game);
      };
      mouse(sf::Mouse::Button::Right, true); idle(3);
      Require(agent.aiming && world.shots.empty() && agent.loadout.lethalAmmo == 6 && agent.loadout.nonLethalAmmo == 6, "Mantener clic derecho no conserva el apuntado o consume munición sin disparar");
      mouse(sf::Mouse::Button::Right, false);
      Require(!agent.aiming && world.shots.empty(), "Soltar clic derecho no termina el apuntado sin disparar");
      fire();
      Require(agent.loadout.lethalAmmo == 5 && agent.lethalMagazine == WeaponConfig::PistolCapacity - 1 && !world.shots.empty() && world.shots.back().lethal, "El clic breve izquierdo no dispara una sola vez la pistola seleccionada");
      Require(agent.pistolSuppressor == WeaponConfig::MaximumSuppressor - WeaponConfig::PistolSuppressorWear && agent.nonLethalSuppressor == WeaponConfig::MaximumSuppressor, "El disparo no desgasta exclusivamente el silenciador del arma utilizada");
      idle(42);
      Require(agent.loadout.lethalAmmo == 5, "El clic izquierdo se procesó más de una vez");
      Key(game, context, sf::Keyboard::Scan::Num2); idle();
      Require(agent.selectedWeapon == WeaponKind::NonLethal && agent.loadout.nonLethalAmmo == 6 && agent.nonLethalMagazine == WeaponConfig::NonLethalCapacity, "2 no selecciona el arma no letal conservando su munición");
      mouse(sf::Mouse::Button::Right, true); fire();
      Require(agent.aiming && agent.loadout.nonLethalAmmo == 5 && !world.shots.empty() && !world.shots.back().lethal && agent.loadout.lethalAmmo == 5,
        std::format("Apuntar y pulsar clic izquierdo no dispara el arma no letal seleccionada: apunta {}, NL {}, L {}, trazos {}, letal {}, cooldown {}, recarga {}, página {}, fase {}, cursor ({}, {})",
          agent.aiming, agent.loadout.nonLethalAmmo, agent.loadout.lethalAmmo, world.shots.size(), !world.shots.empty() && world.shots.back().lethal,
          agent.actionCooldown, agent.reloadRemaining, static_cast<int>(game.page_), static_cast<int>(world.phase), context.cursor.GetPosition().x, context.cursor.GetPosition().y));
      mouse(sf::Mouse::Button::Right, false); idle(42);
      Key(game, context, sf::Keyboard::Scan::H); idle();
      Require(!agent.nonLethalSuppressorEquipped && agent.pistolSuppressorEquipped && agent.nonLethalSuppressor == WeaponConfig::MaximumSuppressor - WeaponConfig::NonLethalSuppressorWear, "H no desmonta solo el silenciador seleccionado o restaura su desgaste");
      Require(game.suppressor_->getText().toStdString() == "SILENCIADOR DESMONTADO", "El HUD no refleja que el silenciador seleccionado está desmontado");
      Key(game, context, sf::Keyboard::Scan::Num1); idle();
      Require(agent.selectedWeapon == WeaponKind::Pistol, "1 no vuelve a seleccionar la pistola");
      const float durability = agent.pistolSuppressor;
      agent.lethalMagazine = 0;
      Key(game, context, sf::Keyboard::Scan::R);
      Require(agent.reloadRemaining > 0.f && agent.lethalMagazine == 0 && !agent.cover.active, "R no inicia una recarga temporal o activa la antigua cobertura");
      fire();
      Require(agent.loadout.lethalAmmo == 5 && agent.lethalMagazine == 0 && agent.reloadRemaining > 0.f, "El clic izquierdo dispara antes de completar la recarga");
      idle(static_cast<int>(std::ceil(WeaponConfig::ReloadDuration * GameplayConfig::SimulationHz)) + 2);
      Require(agent.reloadRemaining == 0.f && agent.lethalMagazine == WeaponConfig::PistolCapacity && agent.loadout.lethalAmmo == 5 && agent.pistolSuppressor == durability, "Recargar no conserva el total de cartuchos o restablece el silenciador gastado");
      const float elapsed = world.elapsed;
      q(true);
      Require(game.page_ == Game::Page::Weapons && game.weaponSlots_[0] && game.weaponSlots_[1] && game.weaponSlots_[0]->getText().empty() && game.weaponSlots_[1]->getText().empty() && world.elapsed == elapsed, "Q no abre ambos equipos o deja avanzar la operación individual mientras se selecciona");
      const auto picker=Capture(game,context,window,"captures/final-held-weapons.png");
      const auto savedVisibility=world.visibility; world.visibility.authoritative=true; world.rescueSignals={{2,{-0.70710678f,-0.70710678f},42.f}};
      const auto pickerRescue=Capture(game,context,window,"captures/final-held-weapons-rescue.png");
      unsigned int countdownPixels=0;
      for (int y=215;y<250;++y) for(int x=825;x<895;++x) {
        const sf::Vector2u pixel{static_cast<unsigned int>(x*picker.getSize().x/1600),static_cast<unsigned int>(y*picker.getSize().y/900)};
        countdownPixels+=picker.getPixel(pixel)!=pickerRescue.getPixel(pixel);
      }
      Require(countdownPixels>10,"El selector Q oculta la cuenta atrás del rescate hacia el borde superior");
      world.visibility=savedVisibility; world.rescueSignals.clear();
      const auto beforeSelection = agent.position;
      sf::Event::KeyPressed move; move.scancode = sf::Keyboard::Scan::D; context.input.CaptureEvent(move);
      clickPanel(1);
      Require(game.page_ == Game::Page::Weapons, "Elegir una silueta cierra Q antes de soltarla");
      for (const auto &widget:game.panel_->getWidgets()) if (const auto label=std::dynamic_pointer_cast<tgui::Label>(widget)) Require(label->getText().toStdString().find(" / ")!=std::string::npos, "El selector contiene texto distinto a cargador/capacidad");
      q(false);
      Require(game.page_ == Game::Page::None && agent.selectedWeapon == WeaponKind::NonLethal && agent.loadout.lethalAmmo == 5 && agent.loadout.nonLethalAmmo == 5 && !agent.nonLethalSuppressorEquipped,
        std::format("El menú no aplica la elección autoritativa o altera munición y silenciador: página {}, arma {}, L {}, NL {}, silenciador {}, pendiente {}, fase {}, pausado {}",
          static_cast<int>(game.page_), static_cast<int>(agent.selectedWeapon), agent.loadout.lethalAmmo, agent.loadout.nonLethalAmmo, agent.nonLethalSuppressorEquipped,
          game.pendingWeaponChoice_, static_cast<int>(world.phase), game.paused_));
      Require(agent.position != beforeSelection, "Cerrar el selector elimina el movimiento que permanece mantenido");
      sf::Event::KeyReleased stop; stop.scancode = sf::Keyboard::Scan::D; context.input.CaptureEvent(stop); idle();
      q(true);
      const auto widgets=game.panel_->getWidgets().size();
      q(true); Require(game.panel_->getWidgets().size()==widgets, "La repetición de Q reconstruye el selector");
      agent.nonLethalMagazine=1; game.RefreshWeapons(); Require(game.weaponAmmo_[1]->getText().toStdString()=="1 / 4", "El contador del selector no se actualiza");
      agent.nonLethalMagazine=WeaponConfig::NonLethalCapacity-1;
      q(false);
      Require(game.page_ == Game::Page::None && agent.loadout.lethalAmmo == 5 && agent.loadout.nonLethalAmmo == 5 && agent.nonLethalMagazine == WeaponConfig::NonLethalCapacity - 1,
        "El clic real para cerrar el panel dispara el arma o consume munición al volver al mundo");
      context.input.Clear(); game.BindInputs(); game.held_.fill(false); game.pendingWeaponChoice_ = -1; game.session_.Submit({});
      world = saved; game.lastSound_ = savedSound; game.UpdateBackground();
    }
    static void VerifyCursorOrientation(Game &game, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output) {
      auto &world=const_cast<WorldState &>(game.session_.GetWorld());
      const auto saved=world; const auto previousSettings=context.window.GetDisplaySettings(); const auto previousZoom=game.worldRenderer_.GetZoom();
      world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.securityEntries.clear(); world.shots.clear(); world.visibility={};
      const std::array<sf::Keyboard::Scan,4> scans{sf::Keyboard::Scan::W,sf::Keyboard::Scan::D,sf::Keyboard::Scan::S,sf::Keyboard::Scan::A};
      const std::array<sf::Vector2f,8> movements{{{0.f,-1.f},{1.f,-1.f},{1.f,0.f},{1.f,1.f},{0.f,1.f},{-1.f,1.f},{-1.f,0.f},{-1.f,-1.f}}};
      const auto hold=[&](sf::Keyboard::Scan scan) { sf::Event::KeyPressed event; event.scancode=scan; context.input.CaptureEvent(event); };
      nlohmann::json evidence=nlohmann::json::array();
      for(const int layout:{0,1,2,3,4}) {
        if(layout<3) window.setSize(std::array<sf::Vector2u,3>{{{1280,720},{1000,700},{1600,900}}}[layout]);
        else {
          auto settings=context.window.GetDisplaySettings(); settings.fullscreen=layout==3; settings.borderless=layout==4; settings.resolution={1600,900};
          Require(context.window.RequestDisplaySettings(settings),"El escenario de cursor no acepta pantalla completa o ventana sin bordes");
          context.window.Update(); context.window.ResolveDisplayChange(true); context.window.Update();
          Require(context.window.GetDisplaySettings().fullscreen==(layout==3) && context.window.GetDisplaySettings().borderless==(layout==4),"El escenario de cursor no aplicó el modo solicitado");
        }
        const auto size=window.getSize();
        context.window.RefreshPresentation(); context.gui.SyncPresentation(context.window.GetLogicalSize(),context.window.GetViewport());
        for(const float requestedZoom:{PresentationConfig::MinimumZoom,PresentationConfig::DefaultZoom,PresentationConfig::MaximumZoom}) {
          game.worldRenderer_.AdjustZoom(std::log(requestedZoom/game.worldRenderer_.GetZoom())/std::log(PresentationConfig::ZoomFactorPerStep));
          const float zoom=game.worldRenderer_.GetZoom();
          for(std::size_t index=0;index<movements.size();++index) {
            context.input.Clear(); game.BindInputs(); game.held_.fill(false);
            auto &agent=world.agents.front(); const auto id=agent.id; agent=Agent{}; agent.id=id; agent.position={500.f,500.f};
            const auto movement=movements[index];
            if(movement.y<0.f) hold(scans[0]); if(movement.x>0.f) hold(scans[1]); if(movement.y>0.f) hold(scans[2]); if(movement.x<0.f) hold(scans[3]);
            const sf::Vector2f cursor{index%2==0?850.f:750.f,480.f}; context.cursor.SetPosition(cursor);
            const auto expectedLook=WorldRenderer::UnprojectGround(cursor,agent.position,context.window.GetLogicalSize(),zoom)-agent.position;
            const auto expectedMove=WorldRenderer::UnprojectGround({800.f+movement.x,480.f+movement.y},agent.position,context.window.GetLogicalSize(),zoom)-WorldRenderer::UnprojectGround({800.f,480.f},agent.position,context.window.GetLogicalSize(),zoom);
            const auto start=agent.position; game.Update(); Step(game);
            Require((agent.position-start).normalized().dot(expectedMove.normalized())>.999f,"WASD cambia de convención al mover el ratón o cambiar resolución/zoom");
            Require(agent.facing.dot(expectedLook.normalized())>.999f,std::format("Sin apuntar, la mirada difiere del cursor: resolución {}x{}, zoom {}, movimiento {}, facing ({},{}), esperado ({},{}), cursor ({},{}), página {}, fase {}, pose {}",size.x,size.y,zoom,index,agent.facing.x,agent.facing.y,expectedLook.normalized().x,expectedLook.normalized().y,context.cursor.GetPosition().x,context.cursor.GetPosition().y,static_cast<int>(game.page_),static_cast<int>(world.phase),static_cast<int>(agent.actionPose)));
            Require(game.worldRenderer_.GetZoom()==zoom,"Cambiar ventana o mover el ratón altera el zoom elegido");
            if(size==sf::Vector2u{1280,720} && std::abs(zoom-PresentationConfig::DefaultZoom)<0.0001f)
              Capture(game,context,window,output/std::format("cursor-movement-{}.png",index));
          }
          context.input.Clear(); game.BindInputs(); game.held_.fill(false);
          auto &agent=world.agents.front(); agent.actionCooldown=0.f; agent.lethalMagazine=WeaponConfig::PistolCapacity; agent.loadout.lethalAmmo=12; agent.selectedWeapon=WeaponKind::Pistol;
          const sf::Vector2f cursor{1050.f,480.f}; context.cursor.SetPosition(cursor);
          context.input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Right,{0,0}});
          context.input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left,{0,0}});
          context.input.CaptureEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left,{0,0}});
          world.shots.clear(); game.Update(); Step(game);
          Require(agent.aiming && !world.shots.empty(),"Apuntar y disparar falla después de cambiar resolución y zoom");
          const auto shotDirection=(world.shots.back().to-world.shots.back().from).normalized();
          const auto laser=WorldRenderer::LaserFor(world,agent);
          Require(shotDirection.dot(agent.facing)>.999f && (laser.to-laser.from).normalized().dot(agent.facing)>.999f,"Disparo, láser y orientación no comparten dirección");
          agent.lethalMagazine=0; agent.actionCooldown=0.f; context.input.BeginFrame(); hold(sf::Keyboard::Scan::R); game.Update(); Step(game);
          Require(agent.reloadRemaining>0.f && agent.facing.dot(shotDirection)>.999f,"Recargar cambia la orientación exigida por el cursor");
          evidence.push_back({{"layout",layout},{"size",{size.x,size.y}},{"zoom",zoom},{"movements",8},{"aim_fire_laser_reload","PASS"}});
          if(layout>=3 && std::abs(zoom-PresentationConfig::DefaultZoom)<0.0001f) Capture(game,context,window,output/std::format("cursor-mode-{}.png",layout));
        }
      }
      context.input.Clear(); game.BindInputs(); game.held_.fill(false); game.session_.Submit({}); world=saved;
      Require(context.window.RequestDisplaySettings(previousSettings),"No se pudo restaurar la ventana después de comprobar el cursor");
      context.window.Update(); context.window.ResolveDisplayChange(true); context.window.Update();
      context.window.RefreshPresentation(); context.gui.SyncPresentation(context.window.GetLogicalSize(),context.window.GetViewport());
      std::ofstream(output/"cursor-orientation.json")<<nlohmann::json{{"result","PASS"},{"movement_cases",120},{"layouts",evidence}}.dump(2);
      game.worldRenderer_.AdjustZoom(std::log(previousZoom/game.worldRenderer_.GetZoom())/std::log(PresentationConfig::ZoomFactorPerStep)); game.RefreshHud();
    }
    static void VerifyManeuverControls(Game &game, EngineContext &context) {
      context.input.Clear(); game.BindInputs(); game.held_.fill(false);
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      const auto saved = world;
      const auto savedSound = game.lastSound_;
      const int savedDive = context.save.Get("NoTrace:DiveKey", 0), savedRoll = context.save.Get("NoTrace:RollKey", 1);
      world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear(); world.events.clear();
      auto &agent = world.agents[0]; agent = Agent{}; agent.id = saved.agents[0].id; agent.name = saved.agents[0].name; agent.position = {300.f, 350.f};
      const auto idle = [&](int frames = 1) {
        for (int frame = 0; frame < frames; ++frame) { context.input.BeginFrame(); game.Update(); Step(game); }
      };
      const auto controls = [&] {
        Escape(game); Press(game, "OPCIONES"); Press(game, "CONTROLES Y TECLAS ASIGNADAS");
        std::vector<tgui::ComboBox::Ptr> selectors;
        for (const auto &widget : game.panel_->getWidgets()) if (const auto box = std::dynamic_pointer_cast<tgui::ComboBox>(widget)) selectors.push_back(box);
        Require(game.page_ == Game::Page::Controls && selectors.size() == 2, "Las opciones no exponen un selector independiente para cada maniobra");
        return selectors;
      };
      auto selectors = controls();
      selectors[0]->setSelectedItemByIndex(3); selectors[1]->setSelectedItemByIndex(3);
      Press(game, "GUARDAR CONTROLES");
      Require(game.page_ == Game::Page::Controls && context.save.Get("NoTrace:DiveKey", 0) == savedDive && context.save.Get("NoTrace:RollKey", 1) == savedRoll, "La GUI acepta teclas duplicadas o altera el guardado al rechazarlas");
      selectors[0]->setSelectedItemByIndex(0); selectors[1]->setSelectedItemByIndex(1);
      Press(game, "GUARDAR CONTROLES");
      Require(game.page_ == Game::Page::Options && context.save.Get("NoTrace:DiveKey", -1) == 0 && context.save.Get("NoTrace:RollKey", -1) == 1, "Guardar no aplica ESPACIO y CTRL IZQUIERDO como controles distintos");
      Escape(game); Escape(game);
      const auto dive = [&](sf::Keyboard::Scan scan) {
        context.input.BeginFrame();
        for (const auto key : {sf::Keyboard::Scan::LShift, sf::Keyboard::Scan::D}) { sf::Event::KeyPressed pressed; pressed.scancode = key; context.input.CaptureEvent(pressed); }
        game.Update(); Step(game);
        Require(agent.running && agent.moving, "La entrada de Shift y dirección no inicia la carrera previa a zambullirse");
        context.input.BeginFrame();
        for (const auto key : {sf::Keyboard::Scan::LShift, sf::Keyboard::Scan::D, scan}) { sf::Event::KeyPressed pressed; pressed.scancode = key; context.input.CaptureEvent(pressed); }
        sf::Event::KeyReleased released; released.scancode = scan; context.input.CaptureEvent(released);
        game.Update(); Step(game);
      };
      dive(ManeuverConfig::DefaultDiveKey);
      Require(agent.maneuver.kind == ManeuverKind::Dive && agent.maneuver.phase == ManeuverPhase::Start, "ESPACIO no inicia una zambullida autoritativa durante la carrera");
      const auto diveOrigin = agent.position;
      bool diveTravel = false, diveRecovery = false;
      for (int frame = 0; frame < 90 && agent.maneuver.kind != ManeuverKind::None; ++frame) {
        idle(); diveTravel |= agent.maneuver.phase == ManeuverPhase::Travel; diveRecovery |= agent.maneuver.phase == ManeuverPhase::Recover;
      }
      Require(diveTravel && diveRecovery && agent.maneuver.kind == ManeuverKind::None && agent.stance == Stance::Prone && (agent.position - diveOrigin).length() > 100.f, "La zambullida desde la GUI no atraviesa impulso y recuperación hasta aterrizar tumbado");
      Key(game, context, sf::Keyboard::Scan::C); idle(static_cast<int>(std::ceil(ManeuverConfig::DiveCooldown * GameplayConfig::SimulationHz)) + 1);
      const auto rollOrigin = agent.position;
      Key(game, context, ManeuverConfig::DefaultRollKey);
      Require(agent.maneuver.kind == ManeuverKind::Roll && agent.stance == Stance::Crouched, "CTRL IZQUIERDO no inicia la rodada desde agachado");
      bool rollRecovery = false;
      for (int frame = 0; frame < 90 && agent.maneuver.kind != ManeuverKind::None; ++frame) { idle(); rollRecovery |= agent.maneuver.phase == ManeuverPhase::Recover; }
      Require(rollRecovery && agent.maneuver.kind == ManeuverKind::None && agent.stance == Stance::Crouched && (agent.position - rollOrigin).length() > 60.f, "La rodada desde la GUI no desplaza al agente y recupera la postura agachada");
      selectors = controls(); selectors[0]->setSelectedItemByIndex(3); selectors[1]->setSelectedItemByIndex(4);
      Press(game, "GUARDAR CONTROLES"); Escape(game); Escape(game);
      idle(static_cast<int>(std::ceil(ManeuverConfig::RollCooldown * GameplayConfig::SimulationHz)) + 1);
      Key(game, context, ManeuverConfig::DefaultRollKey); idle();
      Require(agent.maneuver.kind == ManeuverKind::None, "CTRL IZQUIERDO conserva la rodada después de remapearla a K");
      Key(game, context, sf::Keyboard::Scan::K);
      Require(agent.maneuver.kind == ManeuverKind::Roll, "El control guardado K no llega a la rodada autoritativa");
      for (int frame = 0; frame < 90 && agent.maneuver.kind != ManeuverKind::None; ++frame) idle();
      idle(static_cast<int>(std::ceil(ManeuverConfig::RollCooldown * GameplayConfig::SimulationHz)) + 1);
      Key(game, context, sf::Keyboard::Scan::C); idle();
      dive(ManeuverConfig::DefaultDiveKey);
      Require(agent.maneuver.kind == ManeuverKind::None, "ESPACIO conserva la zambullida después de remapearla a J");
      dive(sf::Keyboard::Scan::J);
      Require(agent.maneuver.kind == ManeuverKind::Dive, "El control guardado J no llega a la zambullida autoritativa");
      context.save.Set("NoTrace:DiveKey", savedDive); context.save.Set("NoTrace:RollKey", savedRoll);
      context.input.Clear(); game.BindInputs(); game.held_.fill(false); game.session_.Submit({});
      world = saved; game.lastSound_ = savedSound; game.UpdateBackground();
    }
    static void Sync(EngineContext &context) {
      context.window.RefreshPresentation();
      context.gui.SyncPresentation(context.window.GetLogicalSize(),context.window.GetViewport());
    }
    static void Step(Game &game, float dt = 1.f / GameplayConfig::SimulationHz) {
      game.session_.Tick(dt, game.paused_ || (game.page_ != Game::Page::None && game.page_ != Game::Page::Puzzle));
      game.UpdateBackground();
    }
    static nlohmann::json VerifyPuzzleGui(Game &game, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output) {
      context.input.Clear(); game.BindInputs(); game.held_.fill(false);
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      const auto saved = world;
      const auto savedSound = game.lastSound_;
      nlohmann::json metrics = nlohmann::json::array();
      world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear(); world.noises.clear(); world.shots.clear(); world.events.clear(); world.sounds.clear();
      auto &agent = world.agents[0]; agent.position = {200.f, 300.f}; agent.facing = {1.f, 0.f}; agent.health = GameplayConfig::MaxHealth; agent.stamina = GameplayConfig::MaxStamina;
      agent.actionCooldown = 0.f; agent.stance = Stance::Standing; agent.cover = {}; agent.puzzle = {}; agent.hidden = false; agent.hideoutId = 0; agent.draggingGuard = -1;
      const auto capture = [&](const std::string &name) {
        Capture(game, context, window, output / (name + ".png"));
        for (const auto &entry : MeasureText(game, name)) metrics.push_back(entry);
      };
      Interactable door{90, ObjectKind::Door, {240.f, 300.f}, "Cerradura de prueba"};
      door.footprint = {24.f, 24.f}; door.locked = true; door.puzzle.kind = PuzzleKind::Door; door.puzzle.difficulty = 3; world.objects.push_back(door);
      Interact(game, context);
      Require(game.page_ == Game::Page::Puzzle && agent.puzzle.target == 90, "E no abre la interfaz contextual del puzle autoritativo");
      for (int choice = 1; choice <= 4; ++choice) Require(HasButton(game, std::to_string(choice)), "La interfaz no conserva las cuatro opciones del circuito");
      Require(HasButton(game, "CANCELAR SIN RESOLVER"), "El circuito no ofrece una cancelación explícita");
      capture("12-puzzle-door");
      const auto elapsed = world.elapsed;
      Step(game, 0.2f);
      Require(world.elapsed > elapsed && world.objects[0].locked, "La interfaz de puzle congela el mundo o abre la puerta sin resolverla");
      const auto answer = [&] { const auto value = agent.puzzle.sequence.at(agent.puzzle.stage); const std::array<const char *, 4> names{"ARRIBA", "DERECHA", "ABAJO", "IZQUIERDA"}; return std::to_string(value) + "  " + names.at(value - 1); };
      Click(game, context, window, answer()); game.Update(); Step(game);
      Require(agent.puzzle.stage == 1 && world.objects[0].locked && game.puzzleStageRendered_ == 1, "El clic de respuesta no actualiza la etapa real y su representación");
      game.Update(); Step(game);
      Require(agent.puzzle.stage == 1, "Un clic de circuito avanza varias etapas sin una nueva elección");
      Escape(game); game.Update(); Step(game);
      Require(game.page_ == Game::Page::None && agent.puzzle.target == 0 && world.objects[0].user == 0 && world.objects[0].locked && !world.objects[0].open,
        "ESC desbloquea una cerradura o deja abierto su puzle");
      Interact(game, context);
      for (unsigned int stage = 0; stage < 3; ++stage) { Click(game, context, window, answer()); game.Update(); Step(game); }
      Require(!world.objects[0].locked && world.objects[0].open && game.page_ == Game::Page::None, "Resolver mediante botones no abre la puerta real ni cierra el circuito");

      world.objects.clear();
      Interactable computer{91, ObjectKind::Computer, {240.f, 300.f}, "Computador contextual"};
      computer.footprint = {24.f, 24.f}; computer.puzzle.kind = PuzzleKind::Computer; computer.puzzle.family = PuzzleFamily::Arrows; computer.puzzle.difficulty = 2;
      computer.puzzle.intelligence = "INTELIGENCIA: acceso norte y cámaras del grupo 1."; world.objects.push_back(computer);
      const auto secondary = world.secondary;
      Interact(game, context); capture("13-puzzle-computer");
      for (unsigned int stage = 0; stage < 2; ++stage) { Click(game, context, window, answer()); game.Update(); Step(game); }
      Require(world.objects[0].completed && world.secondary == secondary + 1 && game.message_->getText().toStdString().find("INTELIGENCIA: acceso norte") != std::string::npos,
        "Descifrar el computador no presenta inteligencia real ni su objetivo secundario");
      capture("14-computer-intelligence");

      world.objects.clear();
      Interactable panel{93, ObjectKind::CameraPanel, {240.f, 300.f}, "Cableado contextual"};
      panel.footprint = {24.f, 24.f}; panel.puzzle.kind = PuzzleKind::CameraPanel; world.objects.push_back(panel);
      Interactable camera{94, ObjectKind::Camera, {400.f, 300.f}, "Cámara del circuito"}; camera.facing = {1.f, 0.f}; world.objects.push_back(camera);
      Interact(game, context);
      Require(agent.puzzle.family == PuzzleFamily::Wires && HasButton(game, "CABLE A") && HasButton(game, "PUERTO 4"),
        "El cableado no presenta sus cuatro orígenes y puertos");
      const auto connect = [&](unsigned int source) {
        const auto port = MatchingWirePort(agent.puzzle, source);
        Click(game, context, window, "CABLE " + std::string(1, static_cast<char>('A' + source - 1)));
        Click(game, context, window, "PUERTO " + std::to_string(port)); game.Update(); Step(game);
      };
      connect(1);
      Require(agent.puzzle.stage == 1 && agent.puzzle.connectedSources == 1 && world.objects[1].active,
        "Elegir cable y puerto no produce una conexión autoritativa o resuelve todo el panel");
      capture("17-puzzle-wires");
      Escape(game); game.Update(); Step(game);
      Require(agent.puzzle.target == 0 && world.objects[1].active && !world.objects[0].completed,
        "Cancelar un cableado parcial desactiva la cámara");
      Interact(game, context);
      for (unsigned int source = 1; source <= 4; ++source) connect(source);
      Require(world.objects[0].completed && !world.objects[1].active && game.page_ == Game::Page::None,
        "Las cuatro conexiones válidas mediante GUI no desactivan el circuito real");

      world.objects.clear();
      Interactable relay{95, ObjectKind::CoopSwitch, {240.f, 300.f}, "Coordinación contextual"};
      relay.footprint = {24.f, 24.f}; relay.puzzle.kind = PuzzleKind::Coordination; relay.puzzle.family = PuzzleFamily::Coordination;
      relay.solid = SpatialOverride::Disabled; relay.blocksVision = SpatialOverride::Disabled; world.objects.push_back(relay);
      Interact(game, context);
      Require(game.page_ == Game::Page::Puzzle && agent.puzzle.family == PuzzleFamily::Coordination && game.puzzleProgress_,
        "La coordinación no presenta su interfaz mantenida y barra de progreso");
      const auto holdButton = FindButton(game, "MANTENER INTERRUPTOR");
      const auto holdPosition = holdButton->getAbsolutePosition() + holdButton->getSize() / 2.f;
      const auto holdPixel = window.mapCoordsToPixel({holdPosition.x, holdPosition.y});
      context.gui.ProcessEvent(sf::Event::MouseMoved{holdPixel});
      context.gui.ProcessEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, holdPixel});
      for (int frame = 0; frame < 5; ++frame) { game.Update(); Step(game, 0.1f); }
      Require(game.pendingPuzzleHeld_ && agent.puzzle.holding && agent.puzzle.coordinationProgress > 0.4f && game.puzzleProgress_->getValue() > 0 &&
        FindButton(game, "MANTENER INTERRUPTOR") == holdButton && !world.objects[0].completed,
        "La GUI reconstruye el botón mantenido, pierde su nivel de entrada o no muestra el progreso real");
      capture("18-puzzle-coordination");
      context.gui.ProcessEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, holdPixel}); game.Update(); Step(game, 0.1f);
      Require(!game.pendingPuzzleHeld_ && !agent.puzzle.holding && agent.puzzle.coordinationProgress == 0.f && !world.objects[0].completed,
        "Soltar el botón de coordinación conserva progreso o resuelve el relé");
      Escape(game); game.Update(); Step(game);
      Require(game.page_ == Game::Page::None && agent.puzzle.target == 0 && world.objects[0].active,
        "Cancelar la coordinación desde GUI activa el dispositivo");
      Interact(game, context);
      const auto retryButton = FindButton(game, "MANTENER INTERRUPTOR");
      const auto retryPosition = retryButton->getAbsolutePosition() + retryButton->getSize() / 2.f;
      const auto retryPixel = window.mapCoordsToPixel({retryPosition.x, retryPosition.y});
      context.gui.ProcessEvent(sf::Event::MouseMoved{retryPixel}); context.gui.ProcessEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, retryPixel});
      for (int frame = 0; frame < 22 && !world.objects[0].completed; ++frame) { game.Update(); Step(game, 0.1f); }
      context.gui.ProcessEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, retryPixel});
      Require(world.objects[0].completed && !world.objects[0].active && !game.pendingPuzzleHeld_ && game.page_ == Game::Page::None,
        "Mantener el control real no completa la coordinación individual o deja un control fantasma");

      world.objects.clear();
      Interactable levers{96, ObjectKind::Computer, {240.f, 300.f}, "Palancas contextuales"};
      levers.footprint = {24.f, 24.f}; levers.puzzle.kind = PuzzleKind::Computer; levers.puzzle.family = PuzzleFamily::Levers;
      levers.puzzle.leverOrder = {2, 4, 1, 3}; levers.puzzle.intelligence = "INTELIGENCIA: secuencia física comprobada."; world.objects.push_back(levers);
      for (unsigned int index = 0; index < 4; ++index) {
        Interactable node{97u + index, ObjectKind::PuzzleNode, {360.f + index * 100.f, 450.f}, "Palanca contextual"};
        node.footprint = {16.f, 16.f}; node.solid = SpatialOverride::Disabled; node.blocksVision = SpatialOverride::Disabled;
        node.puzzle.kind = PuzzleKind::Computer; node.puzzle.parentDevice = 96; node.puzzle.nodeIndex = index + 1; world.objects.push_back(node);
      }
      Interact(game, context);
      Require(agent.puzzle.family == PuzzleFamily::Levers && agent.puzzle.sequence == levers.puzzle.leverOrder && HasButton(game, "VOLVER A LAS PALANCAS") && !HasButton(game, "1"),
        "El computador distribuido no presenta la secuencia física o conserva respuestas matemáticas");
      capture("19-puzzle-levers");
      Click(game, context, window, "VOLVER A LAS PALANCAS"); game.Update(); Step(game);
      Require(game.page_ == Game::Page::None && !world.objects[0].completed && world.objects[0].puzzle.sequenceProgress == 0,
        "Cerrar la instrucción de palancas activa el mecanismo automáticamente");
      for (const auto node : levers.puzzle.leverOrder) {
        agent.position = world.objects[node].position - sf::Vector2f{30.f, 0.f}; agent.actionCooldown = 0.f; Interact(game, context);
        Require(world.objects[node].open && game.page_ == Game::Page::None, "E no acciona la palanca física o abre una respuesta remota");
        // Cada pulsación física necesita un fotograma liberado antes de la siguiente.
        context.input.BeginFrame(); game.Update(); Step(game);
      }
      Require(world.objects[0].completed && world.objects[0].puzzle.sequenceProgress == 4 && game.message_->getText().toStdString().find("secuencia física comprobada") != std::string::npos,
        "Completar las palancas con E no revela la inteligencia real");

      world.objects.clear();
      agent.position = {200.f, 300.f}; agent.facing = {1.f, 0.f};
      Interactable trap{92, ObjectKind::Explosive, {240.f, 300.f}, "Temporizador contextual"}; trap.footprint = {16.f, 12.f};
      trap.solid = SpatialOverride::Disabled; trap.blocksVision = SpatialOverride::Disabled; trap.puzzle.kind = PuzzleKind::Explosive; trap.trap.fuse = 0.6f;
      world.objects.push_back(trap);
      Interact(game, context);
      Require(game.page_ == Game::Page::Puzzle && world.objects[0].trap.remaining > 0.f, "El explosivo no abre un circuito con temporizador real");
      const auto fuse = world.objects[0].trap.remaining;
      Step(game, 0.2f);
      Require(world.objects[0].trap.remaining < fuse && game.page_ == Game::Page::Puzzle, "Abrir el circuito detiene indebidamente la mecha");
      capture("15-puzzle-timer");
      Escape(game); game.Update(); Step(game);
      Require(game.page_ == Game::Page::None && world.objects[0].trap.state == TrapState::Detected && world.objects[0].active,
        "Cancelar un temporizador se interpreta como un desarme satisfactorio");
      for (int frame = 0; frame < 7 && world.objects[0].trap.state != TrapState::Detonated; ++frame) Step(game, 0.1f);
      Require(world.objects[0].trap.state == TrapState::Detonated && agent.health < GameplayConfig::MaxHealth && world.alarm,
        "La mecha cancelada no conserva sus consecuencias reales");
      capture("16-trap-explosion");
      game.ClosePanel(); context.input.Clear(); game.BindInputs(); game.held_.fill(false); game.pendingPuzzleInput_ = -1; game.session_.Submit({});
      world = saved; game.lastSound_ = savedSound; game.UpdateBackground();
      return metrics;
    }
    static void VerifyPrivatePuzzleFeedback(Game &game) {
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      const auto saved = world;
      const auto savedFailure = game.puzzleFailure_;
      const float savedUntil = game.puzzleFailureUntil_;
      const auto savedPhase = game.puzzleFailurePhase_;
      game.puzzleFailure_ = {}; game.puzzleFailureUntil_ = 0.f;
      Simulation authority, replica;
      authority.ResetBase({{1, "Local"}, {2, "Ajeno"}}); authority.Deploy(17);
      auto &host = const_cast<WorldState &>(authority.GetState());
      host.objects.clear(); host.obstacles.clear(); host.guards.clear(); host.securityEntries.clear(); host.events.clear(); host.sounds.clear(); host.noises.clear();
      for (unsigned int index = 0; index < 2; ++index) {
        host.agents[index].position = {200.f, 300.f + index * 70.f}; host.agents[index].facing = {1.f, 0.f};
        Interactable door{200u + index, ObjectKind::Door, {240.f, 300.f + index * 70.f}, "Cerradura privada"};
        door.footprint = {24.f, 24.f}; door.locked = true; door.puzzle.kind = PuzzleKind::Door; host.objects.push_back(door);
      }
      const auto receive = [&](bool includeEvents = true) {
        auto snapshot = authority.SnapshotFor(1, true);
        Require(snapshot.at("message") == "Operación en curso.", "La fixture LAN conserva un mensaje global sin filtrar");
        for (const auto &event : snapshot.at("events")) Require(!(event.at("actor") == 2 && event.at("kind") == static_cast<int>(EventKind::PuzzleFailed)),
          "El estado filtrado revela el fallo privado del otro actor");
        for (const auto &agent : snapshot.at("agents")) if (agent.at("id") == 2)
          Require(!agent.contains("privateOrder") && !agent.contains("puzzle"), "El estado filtrado revela la orden o el circuito del otro actor");
        // La ventana de eventos puede avanzar mientras el cooldown sigue incluido en el estado recibido.
        if (!includeEvents) snapshot["events"] = nlohmann::json::array();
        std::string error;
        Require(replica.LoadSnapshot(snapshot, error), "No se pudo aplicar la réplica LAN de puzles: " + error);
        world = replica.GetState(); game.RefreshHud();
      };
      ActionCommand interact; interact.interact = true;
      authority.Step(0.1f, {{1, interact}, {2, interact}});
      Require(host.agents[0].puzzle.target == 200 && host.agents[1].puzzle.target == 201, "Las dos interacciones no abren sus circuitos independientes");
      ActionCommand foreignError; foreignError.puzzleTarget = 201; foreignError.puzzleInput = host.agents[1].puzzle.sequence.front() % 4 + 1;
      authority.Step(0.1f, {{2, foreignError}}); receive();
      Require(std::ranges::any_of(world.objects, [](const Interactable &object) { return object.id == 201 && object.puzzle.state == PuzzleState::Failed; }),
        "La fixture no incluye el dispositivo visible con fallo ajeno");
      Require(game.message_->getText().toStdString() == "Operación en curso.", "El HUD atribuye al jugador local un fallo de otro actor");
      ActionCommand localError; localError.puzzleTarget = 200; localError.puzzleInput = host.agents[0].puzzle.sequence.front() % 4 + 1;
      authority.Step(0.1f, {{1, localError}}); receive();
      Require(game.message_->getText().toStdString().find("Intervención fallida. Reintento en 3 s") != std::string::npos &&
          host.objects[0].locked && !host.objects[0].open, "El HUD LAN pierde el fallo propio, el reintento o la protección real");
      for (int frame = 0; frame < 18; ++frame) authority.Step(0.1f, {});
      receive(false);
      Require(game.message_->getText().toStdString().find("Reintento en 2 s; después pulsa E.") != std::string::npos,
        "El HUD pierde el fallo recibido al avanzar la ventana de eventos o no actualiza el cooldown");
      for (int frame = 0; frame < 14; ++frame) authority.Step(0.1f, {});
      receive();
      Require(host.objects[0].puzzle.state == PuzzleState::Ready && game.message_->getText().toStdString() == "Operación en curso.",
        "El aviso privado de fallo continúa después de habilitar el reintento");
      authority.Step(0.1f, {{1, interact}}); receive();
      Require(host.agents[0].puzzle.target == 200 && host.objects[0].locked && game.message_->getText().toStdString() == "Operación en curso.",
        "El reintento real conserva un error antiguo o desbloquea el dispositivo sin resolver");
      localError.puzzleInput = host.agents[0].puzzle.sequence.front() % 4 + 1;
      authority.Step(0.1f, {{1, localError}}); receive();
      Require(game.puzzleFailure_.target == 200, "La segunda intervención fallida no actualiza el aviso propio");
      authority.ReturnToBase(); receive();
      Require(game.puzzleFailure_.target == 0 && game.message_->getText().toStdString().find("Intervención fallida") == std::string::npos,
        "Regresar a la base conserva un fallo de la operación anterior");
      world = saved; game.puzzleFailure_ = savedFailure; game.puzzleFailureUntil_ = savedUntil; game.puzzleFailurePhase_ = savedPhase; game.RefreshHud();
    }
    static void Close(Game &game) { game.ClosePanel(); }
    static void RequirePage(Game &game, const std::string &page) {
      const bool matches = (page == "operations" && game.page_ == Game::Page::Operations) ||
          (page == "armory" && game.page_ == Game::Page::Armory) ||
          (page == "intelligence" && game.page_ == Game::Page::Intelligence) ||
          (page == "deployment" && game.page_ == Game::Page::Deployment) ||
          (page == "results" && game.page_ == Game::Page::Results);
      Require(matches, "No se abrió el panel esperado mediante E: " + page);
    }
    static void Interact(Game &game, EngineContext &context) {
      context.input.BeginFrame();
      sf::Event::KeyPressed pressed;
      pressed.scancode = sf::Keyboard::Scan::E;
      context.input.CaptureEvent(pressed);
      sf::Event::KeyReleased released;
      released.scancode = sf::Keyboard::Scan::E;
      context.input.CaptureEvent(released);
      game.Update();
      Step(game);
      context.input.BeginFrame();
    }
    static sf::Image Capture(Game &game, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &file, bool presentation = false) {
      window.setView(presentation ? context.window.GetPresentationView() : sf::View(sf::FloatRect({0.f, 0.f}, gConfig.windowSize)));
      window.clear();
      context.renderer.BeginDrawing();
      game.Render();
      window.draw(sf::Sprite(context.renderer.FinishDrawing()));
      context.gui.Render();
      sf::Texture pixels(window.getSize());
      pixels.update(window);
      window.display();
      const auto image = pixels.copyToImage();
      Require(image.saveToFile(file), "No se pudo guardar la captura: " + file.string());
      return image;
    }
    static void VerifyHudResolutions(Game &game, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output) {
      const auto original = window.getSize();
      const auto healthPosition = game.health_->getPosition(), staminaPosition = game.stamina_->getPosition();
      for (const auto size : {sf::Vector2u{1280, 720}, sf::Vector2u{1280, 960}}) {
        window.setSize(size); context.window.HandleResize(size);
        context.gui.SyncPresentation(context.window.GetLogicalSize(), context.window.GetViewport());
        const auto image = Capture(game, context, window, output / std::format("hud-{}x{}.png", size.x, size.y), true);
        const auto healthPixel = window.mapCoordsToPixel({1015.f, 20.f}, context.window.GetPresentationView());
        const auto staminaPixel = window.mapCoordsToPixel({1015.f, 60.f}, context.window.GetPresentationView());
        Require(image.getPixel(sf::Vector2u(healthPixel)) == sf::Color(148, 44, 59) && image.getPixel(sf::Vector2u(staminaPixel)) == sf::Color(37, 103, 97), "El HUD se desplazó respecto al viewport proporcional al redimensionar");
        Require(game.health_->getPosition() == healthPosition && game.stamina_->getPosition() == staminaPosition, "El redimensionado cambió las anclas lógicas del HUD");
        Escape(game); window.setView(context.window.GetPresentationView());
        Click(game, context, window, "REANUDAR EN LA BASE");
        Require(game.page_ == Game::Page::None, "Los clics de pausa no coinciden con el viewport redimensionado");
      }
      window.setSize(original); context.window.HandleResize(original);
      context.gui.SyncPresentation(context.window.GetLogicalSize(), context.window.GetViewport());
    }
    static sf::Image CaptureWorld(WorldRenderer &renderer, const WorldState &world, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &file, bool hudVisible = true) {
      window.setView(sf::View(sf::FloatRect({0.f, 0.f}, gConfig.windowSize)));
      window.clear(); context.renderer.BeginDrawing(); renderer.Render(world, 1, hudVisible);
      window.draw(sf::Sprite(context.renderer.FinishDrawing()));
      sf::Texture pixels(window.getSize()); pixels.update(window); window.display();
      const auto image = pixels.copyToImage();
      Require(image.saveToFile(file), "No se pudo guardar la captura del mundo: " + file.string());
      return image;
    }
    static void VerifyFinalVisuals(EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output) {
      WorldState demo; demo.phase=GamePhase::Mission; demo.size={1000.f,1000.f};
      Agent covered; covered.id=1; covered.name="Agente"; covered.position={562.01f,500.f}; covered.facing={1.f,0.f};
      covered.cover.active=true; covered.cover.normal=covered.facing; covered.cover.anchor=covered.position; covered.cover.surface={{520.f,450.f},{30.f,200.f}}; covered.cover.obstacle=0;
      demo.agents={covered}; demo.obstacles.push_back({covered.cover.surface,CoverKind::High,true,"Pared"});
      WorldRenderer renderer(context);
      const auto idle=CaptureWorld(renderer,demo,context,window,output/"final-cover-back.png");
      demo.agents[0].actionPose=ActionPose::KnockWall;
      for (int phase=0; phase<4; ++phase) {
        demo.agents[0].actionPoseTime=GameplayConfig::AttackAnimationDuration*(1.f-phase*.25f);
        const auto knocking=CaptureWorld(renderer,demo,context,window,output/std::format("final-wall-knock-{}.png",phase));
        unsigned int armPixels=0;
        for(unsigned int y=0;y<idle.getSize().y;++y) for(unsigned int x=0;x<idle.getSize().x;++x) if(idle.getPixel({x,y})!=knocking.getPixel({x,y})) ++armPixels;
        Require(armPixels>5,"La animación de golpe queda totalmente ocluida en la cara visible de la pared");
      }
      demo.agents[0].actionPose=ActionPose::None; demo.agents[0].actionPoseTime=0.f;
      demo.visibility.authoritative=true; demo.rescueSignals={{2,{1.f,0.f},42.f}};
      const auto rescue=CaptureWorld(renderer,demo,context,window,output/"final-rescue-offscreen.png",false);
      unsigned int changed=0;
      for (unsigned int y=0;y<idle.getSize().y;++y) for (unsigned int x=0;x<idle.getSize().x;++x) if (idle.getPixel({x,y})!=rescue.getPixel({x,y})) ++changed;
      Require(changed>100,"El selector/ocultación del HUD elimina la flecha de rescate fuera de visión");
      demo.visibility={}; demo.rescueSignals.clear(); demo.obstacles.clear(); demo.agents[0].cover={};
      const auto quiet=CaptureWorld(renderer,demo,context,window,output/"final-noise-silent.png",false);
      demo.noises={{demo.agents[0].position,GameplayConfig::RunningNoiseRadius,GameplayConfig::NoiseDuration,1,0,SoundKind::StepRunHard}};
      const auto loud=CaptureWorld(renderer,demo,context,window,output/"final-noise-running-radius.png",false);
      const auto screen=WorldRenderer::ProjectGround(demo.agents[0].position+sf::Vector2f{GameplayConfig::RunningNoiseRadius,0.f},demo.agents[0].position,context.window.GetLogicalSize(),renderer.GetZoom());
      const auto pixel=screen.componentWiseDiv(context.window.GetLogicalSize()).componentWiseMul(sf::Vector2f(loud.getSize()));
      bool edge=false;
      for(int y=-4;y<=4;++y) for(int x=-4;x<=4;++x) {
        const sf::Vector2i sample{static_cast<int>(pixel.x)+x,static_cast<int>(pixel.y)+y};
        if(sample.x>=0 && sample.y>=0 && sample.x<static_cast<int>(loud.getSize().x) && sample.y<static_cast<int>(loud.getSize().y))
          edge|=loud.getPixel(sf::Vector2u(sample))!=quiet.getPixel(sf::Vector2u(sample));
      }
      Require(edge,"El anillo visible recorta el radio acústico utilizado por la IA");
    }
    static void CaptureExample(const Scene &scene, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &file) {
      window.setView(context.window.GetPresentationView());
      window.clear(); context.renderer.BeginDrawing(); scene.Render();
      window.draw(sf::Sprite(context.renderer.FinishDrawing())); context.gui.Render();
      sf::Texture pixels(window.getSize()); pixels.update(window); window.display();
      Require(pixels.copyToImage().saveToFile(file), "No se pudo guardar la captura del ejemplo: " + file.string());
    }
    static void VerifyExampleGames(EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output) {
      Overlay overlay(context.gui);
      const auto overlayGroup = context.gui.gui_.getWidgets().back();
      const auto container = std::dynamic_pointer_cast<tgui::Container>(overlayGroup);
      const auto layout = std::dynamic_pointer_cast<tgui::Container>(container->getWidgets().back());
      const auto resume = std::dynamic_pointer_cast<tgui::Button>(layout->getWidgets().front());
      const auto resumeOverlay = [&] {
        resume->onPress.emit(resume.get(), resume->getText());
        Require(overlay.FetchSelection() == OverlaySelection::Resume, "La pausa genérica perdió la selección Reanudar en los ejemplos");
        overlay.SetVisible(false);
        Require(!overlayGroup->isEnabled(), "El overlay genérico oculto continúa aceptando entradas");
      };
      context.input.Clear(); context.renderer.ResetView();
      Bounce::Game bounce(context);
      Require(bounce.UsesGlobalOverlay(), "Bounce dejó de utilizar la pausa genérica del motor");
      bounce.Start();
      sf::sleep(sf::milliseconds(15)); context.time.Update();
      Require(context.time.GetDeltaTime() > 0.f, "El reloj del ejemplo no proporciona un delta positivo");
      const auto paddleBefore = bounce.paddle.shape.getPosition();
      sf::Event::KeyPressed pressed; pressed.scancode = sf::Keyboard::Scan::D;
      sf::Event::KeyReleased released; released.scancode = sf::Keyboard::Scan::D;
      context.input.BeginFrame(); context.input.CaptureEvent(pressed); context.input.CaptureEvent(released);
      bounce.Update(); context.input.BeginFrame();
      Require(bounce.paddle.shape.getPosition().x > paddleBefore.x, "D sintética no mueve la paleta de Bounce");
      bounce.EventBallSpawn();
      Require(!bounce.balls.empty(), "Bounce no genera pelotas mediante su evento existente");
      bounce.OnPause(true); overlay.SetVisible(true);
      Require(!bounce.ballSpawnCooldown.IsRunning(), "La pausa de Bounce no detiene el temporizador de pelotas");
      const float ballElapsed = bounce.ballSpawnCooldown.GetElapsedTime();
      CaptureExample(bounce, context, window, output / "example-bounce-paused.png");
      Require(bounce.ballSpawnCooldown.GetElapsedTime() == ballElapsed, "El temporizador de Bounce avanza durante la pausa");
      resumeOverlay(); bounce.OnPause(false);
      Require(bounce.ballSpawnCooldown.IsRunning(), "Reanudar Bounce no recupera su temporizador");
      CaptureExample(bounce, context, window, output / "example-bounce.png");
      bounce.OnCleanup();
      context.input.Clear();
      Clicker::Game clicker(context);
      Require(clicker.UsesGlobalOverlay(), "Clicker dejó de utilizar la pausa genérica del motor");
      clicker.Start();
      Require(clicker.stats.score == 0 && !clicker.stats.finalCooldown.IsRunning(), "Clicker no inicia con puntuación cero y reloj detenido");
      clicker.EventTargetClicked();
      Require(clicker.stats.score == 1 && clicker.stats.finalCooldown.IsRunning() && clicker.target.teleportCooldown.IsRunning(), "El evento de clic de Clicker no suma un punto ni activa sus relojes");
      clicker.Update(); clicker.OnPause(true); overlay.SetVisible(true);
      Require(!clicker.stats.finalCooldown.IsRunning() && !clicker.target.teleportCooldown.IsRunning(), "La pausa de Clicker deja un temporizador activo");
      const float finalElapsed = clicker.stats.finalCooldown.GetElapsedTime(), targetElapsed = clicker.target.teleportCooldown.GetElapsedTime();
      CaptureExample(clicker, context, window, output / "example-clicker-paused.png");
      Require(clicker.stats.finalCooldown.GetElapsedTime() == finalElapsed && clicker.target.teleportCooldown.GetElapsedTime() == targetElapsed, "Los temporizadores de Clicker avanzan durante la pausa");
      resumeOverlay(); clicker.OnPause(false);
      Require(clicker.stats.finalCooldown.IsRunning() && clicker.target.teleportCooldown.IsRunning() && clicker.stats.score == 1, "Reanudar Clicker pierde sus relojes o su puntuación");
      CaptureExample(clicker, context, window, output / "example-clicker.png");
      clicker.OnCleanup(); context.gui.Remove(overlayGroup); context.input.Clear();
    }
    static nlohmann::json MeasureText(Game &game, const std::string &page) {
      nlohmann::json entries = nlohmann::json::array();
      for (const auto &widget : game.panel_->getWidgets()) {
        if (const auto label = std::dynamic_pointer_cast<tgui::Label>(widget)) {
          const auto probe = tgui::Label::copy(label);
          probe->setAutoSize(true);
          probe->setMaximumTextWidth(label->getMaximumTextWidth());
          entries.push_back({{"page", page}, {"kind", "label"}, {"text", label->getText().toStdString()},
                            {"availableHeight", label->getSize().y}, {"fullHeight", probe->getSize().y},
                            {"fits", probe->getSize().y <= label->getSize().y + 1.f}});
        } else if (const auto button = std::dynamic_pointer_cast<tgui::Button>(widget)) {
          tgui::Text probe;
          probe.setFont(button->getInheritedFont());
          probe.setCharacterSize(button->getTextSize());
          probe.setString(button->getText());
          entries.push_back({{"page", page}, {"kind", "button"}, {"text", button->getText().toStdString()},
                            {"availableWidth", button->getSize().x - 20.f}, {"fullWidth", probe.getSize().x},
                            {"availableHeight", button->getSize().y - 12.f}, {"fullHeight", probe.getSize().y},
                            {"fits", probe.getSize().x <= button->getSize().x - 20.f && probe.getSize().y <= button->getSize().y - 12.f}});
        }
      }
      return entries;
    }
  };
}

namespace {
  using namespace NoTrace;
  using Access = EngineTests::GuiAccess;

  const Agent &Local(Game &game) {
    const auto &session = Access::GetSession(game);
    for (const auto &agent : session.GetWorld().agents) if (agent.id == session.GetLocalPlayerId()) return agent;
    throw std::runtime_error("No existe agente local para recorrer la base");
  }

  void Walk(Game &game, sf::Vector2f target) {
    auto &session = Access::GetSession(game);
    for (const auto &object : session.GetWorld().objects) {
      if (!WorldGeometry::BlocksMovement(object) || !WorldGeometry::ObjectFootprint(object).contains(target)) continue;
      const auto closest = WorldGeometry::ClosestPoint(Local(game).position, WorldGeometry::ObjectFootprint(object));
      const auto approach = Local(game).position - closest;
      if (approach.lengthSquared() > 0.f) target = closest + approach.normalized() * (GameplayConfig::AgentRadius + 2.f);
      break;
    }
    unsigned int stationary = 0;
    for (unsigned int frame = 0; frame < 3600; ++frame) {
      const auto origin = Local(game).position;
      auto direction = target - origin;
      const float distance = std::hypot(direction.x, direction.y);
      if (distance < 3.f) { session.Submit({}); Access::Step(game); return; }
      direction /= distance;
      ActionCommand command;
      command.moveX = direction.x;
      command.moveY = direction.y;
      session.Submit(command);
      Access::Step(game);
      const auto moved = Local(game).position - origin;
      stationary = moved.lengthSquared() < 0.0001f ? stationary + 1 : 0;
      Access::Require(stationary < 90, "Ruta bloqueada hacia " + std::to_string(target.x) + "," + std::to_string(target.y));
    }
    throw std::runtime_error("Se agotó el límite de pasos de navegación");
  }

  std::string ReadFile(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }

  nlohmann::json Run(const std::filesystem::path &output, bool finalVisualsOnly = false) {
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(gConfig.windowSize)), "NO TRACE GUI TEST", sf::Style::None);
    window.setVisible(false);
    window.setKeyRepeatEnabled(false);
    EngineContext context(window);
    Access::UseSyntheticInput(context);
    context.audio.SetGlobalVolume(0.f);
    context.save.Set(VOLUME_SAVE_KEY,0.f);
    context.save.Set(DisplayConfig::WidthSaveKey,1600u); context.save.Set(DisplayConfig::HeightSaveKey,900u);
    context.save.Set(DisplayConfig::FullscreenSaveKey,false); context.save.Set(DisplayConfig::BorderlessSaveKey,false);
    context.save.Set(DisplayConfig::VerticalSyncSaveKey,false); context.save.Set(DisplayConfig::FramerateLimitSaveKey,0u);
    context.window.Initialize();
    Access::Sync(context);
    Access::VerifyCursorSampling(context,window);
    Access::VerifyProductionSave();
    EngineTests::VerifyOptionsPreferences(window,output);
    Access::VerifyOverlay(context);
    Access::VerifyMainMenu(context,window,output);
    struct ExampleScene final : Scene {
      using Scene::Scene;
      void Start() override {}
      void Update() override {}
      void Render() const override {}
    };
    ExampleScene example(context);
    Access::Require(example.UsesGlobalOverlay(), "Las escenas de ejemplo perdieron el overlay por defecto");
    Game game(context);
    game.Start();
    Access::Require(!game.UsesGlobalOverlay(), "NO TRACE todavía permite el overlay genérico");
    Access::Click(game, context, window, "OPERACIÓN INDIVIDUAL");
    if (finalVisualsOnly) {
      Access::GetSession(game).SetReady(true); Access::GetSession(game).Deploy(); game.UpdateBackground();
      Access::Require(Access::GetSession(game).GetWorld().phase==GamePhase::Mission,"La comprobación visual no pudo desplegar su operación aislada");
      Access::VerifyFireInput(game,context,window); Access::VerifyCursorOrientation(game,context,window,output); Access::VerifyFinalVisuals(context,window,output);
      game.OnCleanup(); window.close(); return nlohmann::json::array();
    }
    Access::Require(Access::GetSession(game).IsConnected(), "La conexión individual desde GUI falló");
    nlohmann::json metrics = nlohmann::json::array();
    const auto capture = [&](const std::string &name) {
      Access::Capture(game, context, window, output / (name + ".png"));
      for (const auto &entry : Access::MeasureText(game, name)) metrics.push_back(entry);
    };

    Access::VerifyHud(game);
    Access::Escape(game);
    Access::Require(!Access::HasButton(game, "REINICIAR") && !Access::HasButton(game, "VOLVER A LA BASE"), "La pausa de la base muestra acciones de una misión inexistente");
    capture("07-base-pause");
    Access::Press(game, "OPCIONES");
    capture("08-pause-options");
    Access::VerifyOptionsKeyboard(game);
    Access::Escape(game);
    Access::Click(game, context, window, "REANUDAR EN LA BASE");
    Access::VerifyZoom(game, context);
    Access::VerifyPauseLifecycle(game, context);

    Walk(game, {210.f, 210.f});
    Access::Interact(game, context);
    Access::RequirePage(game, "operations");
    for (const auto &contract : Contracts()) {
      Access::Press(game, contract.name);
      Access::Require(Access::GetSession(game).GetWorld().contract == contract.id, "El botón no seleccionó el contrato");
    }
    Access::Press(game, Contracts()[0].name);
    capture("01-operations");
    Access::Press(game, "RECORRER LA BASE");

    for (const auto waypoint : {sf::Vector2f{150.f, 280.f}, {300.f, 300.f}, {620.f, 300.f}, {720.f, 210.f}}) Walk(game, waypoint);
    Access::Interact(game, context);
    Access::RequirePage(game, "armory");
    capture("02-armory");
    for (std::size_t index = 0; index < PreparationConfig::Presets.size(); ++index) {
      Access::Press(game, std::string(PreparationConfig::Names[index]));
      const auto expected = PreparationConfig::Presets[index];
      const auto actual = Local(game).loadout;
      Access::Require(actual.lethalAmmo == expected.lethalAmmo && actual.nonLethalAmmo == expected.nonLethalAmmo &&
                      actual.decoys == expected.decoys && actual.tools == expected.tools, "El callback no aplicó el preset");
      if (index + 1 < PreparationConfig::Presets.size()) Access::Interact(game, context);
    }

    for (const auto waypoint : {sf::Vector2f{620.f, 300.f}, {300.f, 300.f}, {300.f, 600.f}, {210.f, 560.f}}) Walk(game, waypoint);
    Access::Interact(game, context);
    Access::RequirePage(game, "intelligence");
    capture("03-intelligence");
    Access::Press(game, "RECORRER LA BASE");

    for (const auto waypoint : {sf::Vector2f{300.f, 420.f}, {620.f, 420.f}, {720.f, 560.f}}) Walk(game, waypoint);
    Access::Interact(game, context);
    Access::RequirePage(game, "deployment");
    capture("04-deployment");
    Access::Press(game, "DESPLEGAR OPERACIÓN");
    Access::Require(Access::GetSession(game).GetWorld().phase == GamePhase::Mission, "El callback no desplegó la operación");
    Access::Capture(game, context, window, output / "05-mission.png");
    Access::VerifyGameOverAndRetry(game);
    Access::VerifyFireInput(game, context, window);
    Access::VerifyCursorOrientation(game,context,window,output);
    Access::VerifyAutomaticCrawl(game, context);
    Access::VerifyTacticalControls(game, context);
    Access::VerifyManeuverControls(game, context);
    for (const auto &entry : Access::VerifyPuzzleGui(game, context, window, output)) metrics.push_back(entry);
    Access::VerifyPrivatePuzzleFeedback(game);
    Access::Escape(game);
    Access::Require(Access::HasButton(game, "REINICIAR") && Access::HasButton(game, "VOLVER A LA BASE"), "La misión individual no ofrece las acciones de pausa correspondientes");
    capture("09-mission-pause");
    const auto pausedPosition = Local(game).position;
    const auto pausedElapsed = Access::GetSession(game).GetWorld().elapsed;
    Access::Step(game, 0.5f);
    Access::Require(Local(game).position == pausedPosition && Access::GetSession(game).GetWorld().elapsed == pausedElapsed, "La pausa individual deja avanzar la simulación");
    const auto seed = Access::GetSession(game).GetWorld().seed;
    Access::Press(game, "REINICIAR OPERACIÓN");
    Access::Require(Access::GetSession(game).GetWorld().seed == seed && Access::GetSession(game).GetWorld().elapsed == 0.f, "El reinicio no conserva la operación o no restablece su reloj");
    Access::VerifyAudioRestart(game);
    Access::Escape(game);
    Access::Press(game, "VOLVER A LA BASE");
    Access::Require(Access::GetSession(game).IsConnected() && Access::GetSession(game).GetWorld().phase == GamePhase::Base, "Volver a base perdió la sesión o no cambió de fase");
    Access::GetSession(game).Deploy(); game.UpdateBackground();
    Access::Require(Access::GetSession(game).GetWorld().phase == GamePhase::Mission, "No se pudo desplegar después de volver a base");
    Access::VerifyFinalVisuals(context,window,output);
    EngineTests::RunWorldRenderFixtures(context, output, [&](WorldRenderer &renderer, const WorldState &world, const std::filesystem::path &file, bool hudVisible = true) {
      return Access::CaptureWorld(renderer, world, context, window, file, hudVisible);
    });

    ActionCommand crouch;
    crouch.crouch = true;
    Access::GetSession(game).Submit(crouch);
    Access::Step(game);
    Walk(game, {95.f, 295.f});
    ActionCommand hide;
    hide.interact = true;
    Access::GetSession(game).Submit(hide);
    Access::Step(game);
    Access::Require(Local(game).hidden, "El agente no entró en el escondite antes de esperar el plazo");
    for (unsigned int frame = 0; frame < 40000 && Access::GetSession(game).GetWorld().phase == GamePhase::Mission; ++frame) {
      Access::GetSession(game).Submit({});
      Access::Step(game);
    }
    Access::RequirePage(game, "results");
    const auto &result = Access::GetSession(game).GetWorld().results.at(1);
    Access::Require(result.reason.find("ventana de extracción") != std::string::npos, "El informe no se produjo por tiempo agotado");
    capture("06-results");
    Access::Press(game, "REGRESAR A LA BASE");
    Access::Require(Access::GetSession(game).GetWorld().phase == GamePhase::Base, "No se regresó a la base desde el informe");
    Access::VerifyHudResolutions(game, context, window, output);
    game.OnCleanup();
    Access::VerifyExampleGames(context, window, output);
    window.close();
    return metrics;
  }
}

int main(int argc, char **argv) {
  try {
    const bool finalVisualsOnly = argc == 2 && std::string(argv[1]) == "--final-visuals-only";
    const auto project = std::filesystem::current_path();
    const auto userSave = ReadFile(project / "Content/Save.json");
    const auto sandbox = project / "build/gui-test-sandbox";
    const auto output = sandbox / "captures";
    std::filesystem::create_directories(output);
    std::filesystem::copy(project / "Content", sandbox / "Content", std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    std::filesystem::current_path(sandbox);
    const auto metrics = Run(output,finalVisualsOnly);
    std::ofstream(output / (finalVisualsOnly ? "final-visual-metrics.json" : "text-metrics.json")) << metrics.dump(2);
    std::filesystem::current_path(project);
    Access::Require(ReadFile(project / "Content/Save.json") == userSave, "La prueba alteró el guardado original");
    if (finalVisualsOnly) { std::cout << "PASS ajustes visuales: cuerpo en cobertura, brazo al golpear, flecha fuera de visión y radio acústico real; Save original intacto\n"; return 0; }
    unsigned int overflow = 0;
    for (const auto &entry : metrics) if (!entry.at("fits").get<bool>()) {
      ++overflow;
      std::cout << "TEXT_OVERFLOW " << entry.at("page") << ' ' << entry.at("kind") << ' ' << entry.at("text") << '\n';
    }
    std::cout << "PASS GUI: overlay separado, pausa propia, foco/modal, opciones por teclado, HUD local y resolución, zoom, arrastre sin X, apuntado, armas y recarga, cobertura, equipo táctico, maniobras y remapeo, estaciones, reinicio, sesión y regresiones Bounce/Clicker; Save original intacto\n";
    std::cout << "CAPTURES=" << output.string() << "\nTEXT_OVERFLOWS=" << overflow << '\n';
    return overflow == 0 ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "FAIL GUI: " << error.what() << '\n';
    return 1;
  }
}
