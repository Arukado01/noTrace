#include "NoTraceGame.h"
#include "Bounce.h"
#include "Clicker.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTracePreparationConfig.h"
#include "Core/EngineConfig.h"
#include "Core/Overlay.h"
#include "NoTraceWorldGuiFixtures.h"
#include "NoTraceWorldGeometry.h"
#include <SFML/Graphics/Image.hpp>
#include <SFML/System/Sleep.hpp>
#include <TGUI/Text.hpp>
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
    static void VerifyHud(Game &game) {
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      auto &agent = world.agents.front();
      const float health = agent.health, stamina = agent.stamina;
      agent.health = 37.f; agent.stamina = 12.f; game.RefreshHud();
      Require(game.health_->isVisible() && game.health_->getValue() == 37 && game.stamina_->getValue() == 12, "El HUD no refleja salud y resistencia de la simulación");
      unsigned int bars = 0;
      for (const auto &widget : game.root_->getWidgets()) bars += std::dynamic_pointer_cast<tgui::ProgressBar>(widget) ? 1u : 0u;
      Require(bars == 2, "El HUD debe contener solo los dos indicadores del jugador local");
      agent.health = health; agent.stamina = stamina; game.RefreshHud();
    }
    static void VerifyGameOverAndRetry(Game &game) {
      auto &world = const_cast<WorldState &>(game.session_.GetWorld());
      const auto seed = world.seed;
      world.agents[0].incapacitated = true; world.agents[0].health = 0.f; world.agents[0].incapacityRemaining = GameplayConfig::IncapacityDuration;
      Step(game);
      Require(world.phase == GamePhase::Results && !world.results.at(1).success && game.page_ == Game::Page::Results, "El desmayo no abre la derrota separada de la pausa");
      Require(HasButton(game, "REINTENTAR") && HasButton(game, "REGRESAR A LA BASE"), "La derrota no conserva las acciones de recuperación");
      Press(game, "REINTENTAR OPERACIÓN");
      Require(world.phase == GamePhase::Mission && world.seed == seed && !world.agents[0].incapacitated && game.page_ == Game::Page::None, "Reintentar no restablece la operación tras el desmayo");
    }
    static void VerifyAudioRestart(Game &game) {
      game.lastAudioEvent_ = 100;
      game.UpdateBackground();
      Require(game.lastAudioEvent_ == 0, "Reiniciar una misión sin cambiar de fase conserva la secuencia de audio anterior");
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
      Require(!game.preparingDecoy_ && agent.loadout.decoys == initialDecoys - 1 && !world.noises.empty(), "Soltar G no envía el lanzamiento previsto");
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
      press(sf::Keyboard::Scan::G); game.Open(Game::Page::Pause); game.ClosePanel();
      press(sf::Keyboard::Scan::G);
      Require(!game.preparingDecoy_, "Reanudar con G mantenida prepara otro lanzamiento sin pulsación nueva");
      release(sf::Keyboard::Scan::G);
      Require(agent.loadout.decoys == initialDecoys - 1, "Abrir la pausa y soltar G conservó un lanzamiento cancelado");
      world.noises.clear(); agent.actionCooldown = 0.f;
      press(sf::Keyboard::Scan::Q); release(sf::Keyboard::Scan::Q);
      Require(world.noises.empty() && world.guards.empty(), "Q generó ruido sin pared válida o conservó la acción CQC");
      Guard body; body.id = 77; body.position = agent.position + sf::Vector2f{20.f, 0.f}; body.state = GuardState::Incapacitated; body.incapacityRemaining = 60.f; world.guards.push_back(body);
      press(sf::Keyboard::Scan::E);
      Require(agent.draggingGuard == 77, "E mantenida no inicia arrastre contextual");
      press(sf::Keyboard::Scan::E);
      Require(agent.draggingGuard == 77, "Mantener E alterna o interrumpe el arrastre");
      release(sf::Keyboard::Scan::E);
      Require(agent.draggingGuard < 0, "Soltar E no termina el arrastre");
      world.guards.clear(); world.obstacles.push_back({{{200.f, 150.f}, {40.f, 100.f}}, CoverKind::High, true, "Pared"});
      agent.position = {187.99f, 155.f}; agent.cover = {};
      press(sf::Keyboard::Scan::R); release(sf::Keyboard::Scan::R);
      Require(agent.cover.active, "R no activa la cobertura desde la entrada del juego");
      const float anchorY = agent.cover.anchor.y;
      context.input.BeginFrame();
      sf::Event::KeyPressed alt; alt.scancode = sf::Keyboard::Scan::LAlt; context.input.CaptureEvent(alt);
      sf::Event::KeyPressed right; right.scancode = sf::Keyboard::Scan::D; context.input.CaptureEvent(right);
      game.Update(); Step(game);
      Require(agent.cover.peekSide == PeekSide::Left && agent.position.y < anchorY, "Alt + dirección asoma al extremo opuesto de la pared");
      context.input.Clear(); game.held_.fill(false); world = saved;
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
    static void VerifyFireInput(Game &game, EngineContext &context) {
      context.cursor.SetPosition({1000.f, 480.f});
      const auto fire = [&](sf::Mouse::Button button) {
        context.input.BeginFrame();
        context.input.CaptureEvent(sf::Event::MouseButtonPressed{button, {1000, 480}});
        context.input.CaptureEvent(sf::Event::MouseButtonReleased{button, {1000, 480}});
        game.Update(); Step(game); context.input.BeginFrame();
      };
      const int lethal = game.LocalAgent()->loadout.lethalAmmo, nonLethal = game.LocalAgent()->loadout.nonLethalAmmo;
      fire(sf::Mouse::Button::Left);
      Require(game.LocalAgent()->loadout.lethalAmmo == lethal - 1 && !game.session_.GetWorld().shots.empty() && game.session_.GetWorld().shots.back().lethal, "El clic breve izquierdo no llega al disparo autoritativo con efecto visible");
      for (unsigned int frame = 0; frame < 42; ++frame) { game.Update(); Step(game); context.input.BeginFrame(); }
      Require(game.LocalAgent()->loadout.lethalAmmo == lethal - 1, "El clic izquierdo se procesó más de una vez");
      fire(sf::Mouse::Button::Right);
      Require(game.LocalAgent()->loadout.nonLethalAmmo == nonLethal - 1 && !game.session_.GetWorld().shots.back().lethal, "El clic breve derecho no llega al disparo no letal autoritativo");
    }
    static void Sync(EngineContext &context) {
      context.gui.SyncPresentation(gConfig.windowSize, {{0.f, 0.f}, {1.f, 1.f}});
    }
    static void Step(Game &game, float dt = 1.f / GameplayConfig::SimulationHz) {
      game.session_.Tick(dt, game.paused_ || game.page_ != Game::Page::None);
      game.UpdateBackground();
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

  nlohmann::json Run(const std::filesystem::path &output) {
    sf::RenderWindow window(sf::VideoMode(sf::Vector2u(gConfig.windowSize)), "NO TRACE GUI TEST", sf::Style::None);
    window.setVisible(false);
    window.setKeyRepeatEnabled(false);
    EngineContext context(window);
    Access::UseSyntheticInput(context);
    context.audio.SetGlobalVolume(0.f);
    Access::Sync(context);
    Access::VerifyOverlay(context);
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
    Access::VerifyFireInput(game, context);
    Access::VerifyAutomaticCrawl(game, context);
    Access::VerifyTacticalControls(game, context);
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

int main() {
  try {
    const auto project = std::filesystem::current_path();
    const auto userSave = ReadFile(project / "Content/Save.json");
    const auto sandbox = project / "build/gui-test-sandbox";
    const auto output = sandbox / "captures";
    std::filesystem::create_directories(output);
    std::filesystem::copy(project / "Content", sandbox / "Content", std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
    std::filesystem::current_path(sandbox);
    const auto metrics = Run(output);
    std::ofstream(output / "text-metrics.json") << metrics.dump(2);
    std::filesystem::current_path(project);
    Access::Require(ReadFile(project / "Content/Save.json") == userSave, "La prueba alteró el guardado original");
    unsigned int overflow = 0;
    for (const auto &entry : metrics) if (!entry.at("fits").get<bool>()) {
      ++overflow;
      std::cout << "TEXT_OVERFLOW " << entry.at("page") << ' ' << entry.at("kind") << ' ' << entry.at("text") << '\n';
    }
    std::cout << "PASS GUI: overlay separado, pausa propia, foco/modal, opciones por teclado, HUD local y resolución, zoom, arrastre sin X, armas, estaciones, equipo, reinicio, sesión y regresiones Bounce/Clicker; Save original intacto\n";
    std::cout << "CAPTURES=" << output.string() << "\nTEXT_OVERFLOWS=" << overflow << '\n';
    return overflow == 0 ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "FAIL GUI: " << error.what() << '\n';
    return 1;
  }
}
