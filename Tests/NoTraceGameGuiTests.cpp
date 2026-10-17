#include "NoTraceGame.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTracePreparationConfig.h"
#include "Core/EngineConfig.h"
#include <SFML/Graphics/Image.hpp>
#include <TGUI/Text.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace EngineTests {
  using namespace NoTrace;
  struct GuiAccess {
    static Session &GetSession(Game &game) { return game.session_; }
    static void Require(bool condition, const std::string &message) {
      if (!condition) throw std::runtime_error(message);
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
    static void Capture(Game &game, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &file) {
      window.setView(sf::View(sf::FloatRect({0.f, 0.f}, gConfig.windowSize)));
      window.clear();
      context.renderer.BeginDrawing();
      game.Render();
      window.draw(sf::Sprite(context.renderer.FinishDrawing()));
      context.gui.Render();
      sf::Texture pixels(window.getSize());
      pixels.update(window);
      window.display();
      Require(pixels.copyToImage().saveToFile(file), "No se pudo guardar la captura: " + file.string());
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
    context.audio.SetGlobalVolume(0.f);
    Access::Sync(context);
    Game game(context);
    game.Start();
    Access::Press(game, "OPERACIÓN INDIVIDUAL");
    Access::Require(Access::GetSession(game).IsConnected(), "La conexión individual desde GUI falló");
    nlohmann::json metrics = nlohmann::json::array();
    const auto capture = [&](const std::string &name) {
      Access::Capture(game, context, window, output / (name + ".png"));
      for (const auto &entry : Access::MeasureText(game, name)) metrics.push_back(entry);
    };

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

    for (const auto waypoint : {sf::Vector2f{300.f, 300.f}, {620.f, 300.f}, {720.f, 210.f}}) Walk(game, waypoint);
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
    game.OnCleanup();
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
    std::cout << "PASS GUI: cuatro estaciones navegadas, tres contratos, cuatro presets, despliegue, misión, informe por plazo y regreso; Save original intacto\n";
    std::cout << "CAPTURES=" << output.string() << "\nTEXT_OVERFLOWS=" << overflow << '\n';
    return overflow == 0 ? 0 : 2;
  } catch (const std::exception &error) {
    std::cerr << "FAIL GUI: " << error.what() << '\n';
    return 1;
  }
}
