#include "NoTraceGame.h"
#include "NoTraceLanQaPilot.h"
#include "Core/EngineConfig.h"
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Sleep.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace EngineTests {
  struct GuiAccess {
    static void Initialize(EngineContext &context) {
      context.input.hardwarePolling_ = false;
      context.window.RefreshPresentation();
      context.gui.SyncPresentation(context.window.GetLogicalSize(), context.window.GetViewport());
    }
    static NoTrace::Session &Session(NoTrace::Game &game) { return game.session_; }
    static bool Connection(const NoTrace::Game &game) { return game.page_ == NoTrace::Game::Page::Connection; }
    static bool Deployment(const NoTrace::Game &game) { return game.page_ == NoTrace::Game::Page::Deployment; }
    static bool Pause(const NoTrace::Game &game) { return game.page_ == NoTrace::Game::Page::Pause; }
    static tgui::Button::Ptr Button(NoTrace::Game &game, const std::string &prefix) {
      for (const auto &widget : game.panel_->getWidgets()) {
        const auto button = std::dynamic_pointer_cast<tgui::Button>(widget);
        if (button && button->getText().toStdString().starts_with(prefix)) return button;
      }
      throw std::runtime_error("No existe el botón GUI: " + prefix);
    }
    static bool DeployEnabled(const NoTrace::Game &game) { return game.deploy_ && game.deploy_->isEnabled(); }
    static void FillConnection(NoTrace::Game &game, const std::string &name, const std::string &ip, unsigned int port) {
      if (!Connection(game)) throw std::runtime_error("Los campos de conexión no están abiertos");
      std::vector<tgui::EditBox::Ptr> fields;
      for (const auto &widget : game.panel_->getWidgets()) if (const auto field = std::dynamic_pointer_cast<tgui::EditBox>(widget)) fields.push_back(field);
      if (fields.size() != 3) throw std::runtime_error("Cambió el contrato de los tres campos de conexión");
      fields[0]->setText(name); fields[1]->setText(ip); fields[2]->setText(std::to_string(port));
    }
    static void Click(EngineContext &context, sf::RenderWindow &window, const tgui::Button::Ptr &button) {
      if (!button || !button->isEnabled() || !button->isVisible()) throw std::runtime_error("No se puede pulsar el botón GUI");
      const auto center = button->getAbsolutePosition() + button->getSize() / 2.f;
      const auto pixel = window.mapCoordsToPixel({center.x, center.y}, context.window.GetPresentationView());
      context.gui.ProcessEvent(sf::Event::MouseMoved{pixel});
      context.gui.ProcessEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, pixel});
      context.gui.ProcessEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, pixel});
    }
    static void Lobby(NoTrace::Game &game, EngineContext &context, sf::RenderWindow &window) { Click(context, window, game.lobby_); }
    static void Time(EngineContext &context) { context.time.Update(); }
    static void Render(NoTrace::Game &game, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &capture = {}) {
      window.setView(context.window.GetPresentationView()); window.clear();
      context.renderer.BeginDrawing(); game.Render();
      window.draw(sf::Sprite(context.renderer.FinishDrawing())); context.gui.Render();
      if (!capture.empty()) {
        sf::Texture pixels(window.getSize()); pixels.update(window);
        if (!pixels.copyToImage().saveToFile(capture)) throw std::runtime_error("No se pudo guardar la captura gráfica");
      }
      window.display();
    }
  };
}

namespace {
  using Json = nlohmann::json;
  using Access = EngineTests::GuiAccess;
  using namespace NoTrace;
  using Path = std::filesystem::path;
  using Clock = std::chrono::steady_clock;

  struct Cancelled final : std::runtime_error { using std::runtime_error::runtime_error; };
  struct Options {
    std::string role, scenario, host, runId;
    Path coord, output;
    unsigned int port = 55041;
    unsigned int timeout = 360;
  };

  Options Parse(int argc, char **argv) {
    Options options;
    for (int index = 1; index < argc; index += 2) {
      if (index + 1 >= argc) throw std::runtime_error("Falta el valor de una opción QA");
      const std::string key = argv[index], value = argv[index + 1];
      if (key == "--role") options.role = value;
      else if (key == "--scenario") options.scenario = value;
      else if (key == "--host") options.host = value;
      else if (key == "--port") options.port = static_cast<unsigned int>(std::stoul(value));
      else if (key == "--run-id") options.runId = value;
      else if (key == "--coord") options.coord = std::filesystem::absolute(value);
      else if (key == "--output") options.output = std::filesystem::absolute(value);
      else if (key == "--timeout") options.timeout = static_cast<unsigned int>(std::stoul(value));
      else throw std::runtime_error("Opción QA desconocida: " + key);
    }
    if ((options.role != "host" && options.role != "client") ||
        (options.scenario != "mission" && options.scenario != "client-drop" && options.scenario != "host-drop") ||
        options.host.empty() || options.port == 0 || options.port > 65535 || options.timeout == 0 || options.timeout > 360 ||
        options.runId.size() != 32 || !std::ranges::all_of(options.runId, [](unsigned char value) { return std::isxdigit(value) != 0; }) ||
        options.coord.empty() || options.output.empty()) throw std::runtime_error("Contrato CLI QA inválido");
    return options;
  }

  void WriteJson(const Path &file, const Json &value) {
    const auto temporary = Path(file.string() + ".tmp");
    {
      std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
      if (!output || !(output << value.dump(2) << '\n')) throw std::runtime_error("No se pudo escribir " + file.string());
    }
    std::error_code error;
    std::filesystem::rename(temporary, file, error);
    if (error) {
      std::filesystem::copy_file(temporary, file, std::filesystem::copy_options::overwrite_existing, error);
      if (error) throw std::runtime_error("No se pudo publicar " + file.string());
      std::filesystem::remove(temporary, error);
    }
  }

  void CreateSandbox(const Options &options) {
    const Path source = std::filesystem::current_path() / "Content";
    const Path runtime = options.output / "runtime";
    if (!std::filesystem::is_directory(source)) throw std::runtime_error("Falta Content en el directorio de ejecución");
    if (std::filesystem::exists(runtime)) throw std::runtime_error("La prueba requiere un directorio runtime nuevo");
    std::filesystem::create_directories(runtime / "Content");
    for (auto entry = std::filesystem::recursive_directory_iterator(source); entry != std::filesystem::recursive_directory_iterator(); ++entry) {
      const auto relative = std::filesystem::relative(entry->path(), source);
      const auto first = relative.begin()->string();
      if (first == "Screenshots" || first == "Sources") { if (entry->is_directory()) entry.disable_recursion_pending(); continue; }
      if (!entry->is_regular_file() || relative == Path("Save.json")) continue;
      const auto destination = runtime / "Content" / relative;
      std::filesystem::create_directories(destination.parent_path());
      std::filesystem::copy_file(entry->path(), destination);
    }
    std::filesystem::current_path(runtime);
  }

  class Runner {
  public:
    explicit Runner(Options options)
        : options_(std::move(options)), window_(sf::VideoMode({1280, 720}), "NO TRACE / QA LAN / " + options_.role + " / " + options_.scenario),
          context_(window_), game_(context_), started_(Clock::now()), entered_(started_) {
      std::filesystem::create_directories(options_.coord);
      std::filesystem::create_directories(options_.output / "captures");
      window_.setKeyRepeatEnabled(false); window_.setFramerateLimit(30); window_.setVisible(true);
      Access::Initialize(context_); context_.audio.SetGlobalVolume(gConfig.globalVolume);
      game_.Start(); recovery_ = Marker(options_.role + "-forced-drop.json").is_object();
      stage_ = recovery_ ? "recovery_start" : "start";
    }

    int Run() {
      std::string result = "PASS", reason;
      try {
        while (!complete_) {
          if (Elapsed() > options_.timeout) throw std::runtime_error("Plazo QA agotado en " + stage_);
          CheckCancellation();
          context_.input.BeginFrame();
          while (const auto event = window_.pollEvent()) {
            if (event->is<sf::Event::Closed>()) throw Cancelled("Ventana QA cerrada por el usuario");
            if (const auto *key = event->getIf<sf::Event::KeyPressed>(); key && key->scancode == sf::Keyboard::Scan::Escape) throw Cancelled("Escape cancela la prueba QA");
            if (const auto *resize = event->getIf<sf::Event::Resized>()) { context_.window.HandleResize(resize->size); Access::Initialize(context_); }
          }
          Access::Time(context_); game_.Update();
          ActionCommand idle; idle.aimX = 1.f; SessionState().Submit(idle);
          Drive(); game_.UpdateBackground();
          Access::Render(game_, context_, window_);
          if (Elapsed() - progressAt_ >= 1.f) { progressAt_ = Elapsed(); WriteJson(options_.output / "qa-progress.json", Report("RUNNING", "")); }
        }
      } catch (const Cancelled &error) { result = "CANCELLED"; reason = error.what(); }
      catch (const std::exception &error) { result = "FAIL"; reason = error.what(); }
      if (result != "PASS") {
        cases_.push_back({{"name", stage_}, {"status", result}, {"detail", reason}, {"seconds", Elapsed()}});
        try { Capture("failure"); } catch (...) {}
      }
      WriteJson(options_.output / "qa-report.json", Report(result, reason));
      std::cout << result << " QA " << options_.role << ' ' << options_.scenario << ": " << reason << '\n';
      game_.OnCleanup(); window_.close();
      return result == "PASS" ? 0 : result == "CANCELLED" ? 3 : 1;
    }

  private:
    Options options_;
    sf::RenderWindow window_;
    EngineContext context_;
    Game game_;
    NoTrace::Qa::MissionPilot pilot_;
    Clock::time_point started_, entered_;
    std::string stage_;
    Json cases_ = Json::array(), evidence_ = Json::array(), personalResult_;
    std::uint32_t missionSeed_ = 0;
    std::uint64_t baselineTick_ = 0;
    sf::Vector2f baselinePosition_, pausedLocalPosition_;
    float progressAt_ = -1.f;
    bool recovery_ = false, complete_ = false, objectiveSeen_ = false, hostExtracted_ = false;

    Session &SessionState() { return Access::Session(game_); }
    const WorldState &World() { return SessionState().GetWorld(); }
    bool Host() const { return options_.role == "host"; }
    float Elapsed() const { return std::chrono::duration<float>(Clock::now() - started_).count(); }
    float StageSeconds() const { return std::chrono::duration<float>(Clock::now() - entered_).count(); }
    void Stage(const std::string &stage) { stage_ = stage; entered_ = Clock::now(); std::cout << "QA_STAGE " << stage << '\n' << std::flush; }
    void Require(bool condition, const std::string &message) { if (!condition) throw std::runtime_error(message); }
    void Case(const std::string &name, Json detail = Json::object()) {
      cases_.push_back({{"name", name}, {"status", "PASS"}, {"detail", std::move(detail)}, {"seconds", Elapsed()}});
    }
    const Agent *AgentById(PlayerId id) { const auto &agents = World().agents; const auto found = std::ranges::find_if(agents, [id](const Agent &agent) { return agent.id == id; }); return found == agents.end() ? nullptr : &*found; }
    const Agent *Local() { return AgentById(SessionState().GetLocalPlayerId()); }
    bool Room() { return SessionState().IsConnected() && World().phase == GamePhase::Base && SessionState().GetLobbyPlayers().size() == 2 && std::ranges::count_if(World().agents, [](const Agent &agent) { return agent.connected; }) == 2; }
    bool AllReady() { return Room() && std::ranges::all_of(SessionState().GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.ready; }); }
    Json State() {
      Json state{{"connected", SessionState().IsConnected()}, {"phase", static_cast<int>(World().phase)}, {"tick", World().tick}, {"seed", World().seed}, {"elapsed", World().elapsed}, {"player", SessionState().GetLocalPlayerId()}, {"rosterCount", SessionState().GetLobbyPlayers().size()}, {"status", SessionState().GetStatus()}};
      if (const auto *local = Local()) state["local"] = {{"position", {local->position.x, local->position.y}}, {"health", local->health}, {"extracted", local->extracted}};
      return state;
    }
    Json Report(const std::string &status, const std::string &reason) {
      Json result{{"schema", 1}, {"runId", options_.runId}, {"role", options_.role}, {"scenario", options_.scenario}, {"protocol", GameplayConfig::ProtocolVersion}, {"status", status}, {"reason", reason}, {"stage", stage_}, {"seconds", Elapsed()}, {"recoveryAttempt", recovery_}, {"missionSeed", missionSeed_}, {"cases", cases_}, {"evidence", evidence_}, {"state", State()}, {"inputMode", "game_actions_and_tgui_events"}, {"saveIsolation", "output/runtime/Content"}};
      if (personalResult_.is_object()) result["personalResult"] = personalResult_;
      return result;
    }
    Json Marker(const std::string &name) {
      std::ifstream input(options_.coord / name);
      if (!input) return Json();
      const auto value = Json::parse(input, nullptr, false);
      return value.is_object() && value.value("runId", "") == options_.runId ? value : Json();
    }
    void Mark(const std::string &name, Json data = Json::object()) {
      data["schema"] = 1; data["runId"] = options_.runId; data["role"] = options_.role; data["scenario"] = options_.scenario; data["seconds"] = Elapsed(); data["state"] = State();
      WriteJson(options_.coord / name, data);
    }
    void CheckCancellation() {
      for (const auto *file : {"cancel.json", "CANCEL.json"}) if (const auto marker = Marker(file); marker.is_object()) throw Cancelled(marker.value("reason", "Cancelación solicitada"));
    }
    void Capture(const std::string &name) {
      const auto file = options_.output / "captures" / (name + ".png");
      Access::Render(game_, context_, window_, file);
      evidence_.push_back({{"name", name}, {"file", "captures/" + name + ".png"}, {"tick", World().tick}, {"seed", World().seed}, {"seconds", Elapsed()}});
    }
    void Click(const std::string &prefix) { Access::Click(context_, window_, Access::Button(game_, prefix)); }
    void SubmitPilot(bool collectObjective, bool allowExtraction = true) {
      auto command = pilot_.Update(World(), SessionState().GetLocalPlayerId(), collectObjective, allowExtraction);
      if (std::hypot(command.aimX, command.aimY) < 0.01f) command.aimX = 1.f;
      SessionState().Submit(command);
    }
    void Connect(bool host) {
      Access::FillConnection(game_, host ? "QA ANFITRION" : "QA VM", options_.host, options_.port);
      Click(host ? "CREAR SALA LAN" : "CONECTAR POR IP");
      Require(Access::Deployment(game_), "La pulsación GUI no abrió la sala");
    }
    void OpenConnection() {
      if (Access::Connection(game_)) return;
      if (Access::Deployment(game_)) Click("DESCONECTAR");
      else Access::Lobby(game_, context_, window_);
      Require(Access::Connection(game_), "No se pudo recuperar el acceso a CIO mediante la GUI");
    }

    void Drive() {
      if (stage_ == "start") {
        Capture("01-connection");
        if (Host()) { Connect(true); Require(SessionState().IsConnected(), "No se pudo crear la sala"); Mark("host-room-ready.json"); Stage("room_wait"); }
        else Stage("host_wait");
      } else if (stage_ == "host_wait") {
        if (Marker("host-room-ready.json").is_object()) { Connect(false); Stage("room_wait"); }
      } else if (stage_ == "room_wait") {
        if (Room()) {
          const auto &players = SessionState().GetLobbyPlayers();
          Require(std::ranges::any_of(players, [](const LobbyPlayer &player) { return player.id == GameplayConfig::HostPlayerId && player.name == "QA ANFITRION"; }) &&
              std::ranges::any_of(players, [](const LobbyPlayer &player) { return player.id != GameplayConfig::HostPlayerId && player.name == "QA VM"; }), "La sala no conserva identidades distintas de ambos agentes");
          Require((SessionState().GetLocalPlayerId() == GameplayConfig::HostPlayerId) == Host(), "La identidad local no coincide con el rol QA");
          Require(!Access::DeployEnabled(game_), "La GUI habilita despliegue antes de preparar ambos agentes");
          Case("room_two_agents", {{"localPlayer", SessionState().GetLocalPlayerId()}, {"deployDisabled", true}});
          Capture("02-room-two-agents"); Mark(options_.role + "-room-seen.json"); Stage("room_barrier");
        } else if (!SessionState().IsConnected() && StageSeconds() > 12.f) throw std::runtime_error("No se estableció la sala: " + SessionState().GetStatus());
      } else if (stage_ == "room_barrier") {
        if (Marker("host-room-seen.json").is_object() && Marker("client-room-seen.json").is_object() && StageSeconds() >= 2.f) {
          if (options_.scenario == "mission") {
            if (Host()) Stage("base_client_leave_wait");
            else { Click("DESCONECTAR"); Require(!SessionState().IsConnected() && Access::Connection(game_), "La desconexión GUI no limpió al cliente"); Capture("03-base-disconnected"); Mark("client-base-left.json"); Stage("base_leave_ack_wait"); }
          } else Stage("prepare");
        }
      } else if (stage_ == "base_client_leave_wait") {
        if (Marker("client-base-left.json").is_object() && SessionState().GetLobbyPlayers().size() == 1) {
          Require(SessionState().IsConnected() && World().phase == GamePhase::Base, "La salida del cliente cerró la base del anfitrión");
          Case("client_leaves_base_host_survives"); Capture("03-base-client-left"); Mark("host-base-left-seen.json"); Stage("base_rejoin_wait");
        }
      } else if (stage_ == "base_leave_ack_wait") {
        if (Marker("host-base-left-seen.json").is_object()) { Connect(false); Stage("base_rejoin_wait"); }
      } else if (stage_ == "base_rejoin_wait") {
        if (Room()) {
          Require(std::ranges::none_of(SessionState().GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.ready; }), "Reingresar a base conserva preparación antigua");
          Case("client_rejoins_base_readiness_cleared"); Capture("04-base-rejoined"); Mark(options_.role + "-base-rejoined.json"); Stage("base_rejoin_barrier");
        }
      } else if (stage_ == "base_rejoin_barrier") {
        if (Marker("host-base-rejoined.json").is_object() && Marker("client-base-rejoined.json").is_object()) Stage("preparation_burst");
      } else if (stage_ == "preparation_burst") {
        if (!Host()) {
          for (int change = 0; change < 8; ++change) { SessionState().SetLoadout({0, 12, 6, 6}); SessionState().SetReady(true); }
          Mark("client-preparation-burst-queued.json", {{"validMessages", 16}});
        }
        Stage("preparation_burst_wait");
      } else if (stage_ == "preparation_burst_wait") {
        Require(Room(), "Una ráfaga válida de preparación desconectó la sala");
        if (Marker("client-preparation-burst-queued.json").is_object() && StageSeconds() >= 2.f) {
          const bool clientReady = std::ranges::any_of(SessionState().GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.id != GameplayConfig::HostPlayerId && player.ready; });
          if (!clientReady) { Require(StageSeconds() < 12.f, "No se recibió el estado final de preparación de la ráfaga"); return; }
          Case("valid_preparation_burst_keeps_room", {{"validMessages", 16}, {"stableSeconds", StageSeconds()}});
          Capture("04-valid-preparation-burst"); Mark(options_.role + "-preparation-burst-seen.json"); Stage("preparation_burst_barrier");
        }
      } else if (stage_ == "preparation_burst_barrier") {
        if (Marker("host-preparation-burst-seen.json").is_object() && Marker("client-preparation-burst-seen.json").is_object()) {
          if (!Host()) SessionState().SetReady(false);
          Stage("preparation_burst_reset");
        }
      } else if (stage_ == "preparation_burst_reset") {
        Require(Room(), "La sala no se conservó después de preparar el equipo");
        if (std::ranges::none_of(SessionState().GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.ready; })) {
          Mark(options_.role + "-preparation-burst-reset.json"); Stage("preparation_burst_reset_barrier");
        }
      } else if (stage_ == "preparation_burst_reset_barrier") {
        if (Marker("host-preparation-burst-reset.json").is_object() && Marker("client-preparation-burst-reset.json").is_object()) Stage("prepare");
      } else if (stage_ == "prepare") {
        Require(World().contract == ContractId::GhostArchive, "El contrato inicial no es Archivo Fantasma");
        SessionState().SetLoadout({0, 12, 6, 6});
        Click("ALTERNAR PREPARADO"); Mark(options_.role + "-ready-clicked.json"); Stage("prepared_wait");
      } else if (stage_ == "prepared_wait") {
        if (AllReady()) {
          Require(Access::DeployEnabled(game_) == Host(), "La autoridad del botón de despliegue es incorrecta");
          Case("ready_and_host_deploy_authority", {{"deployEnabled", Access::DeployEnabled(game_)}});
          Capture("05-ready"); Mark(options_.role + "-all-ready.json"); Stage("deploy_wait");
        }
      } else if (stage_ == "deploy_wait") {
        if (Host() && Marker("client-all-ready.json").is_object()) { Click("DESPLEGAR OPERACIÓN"); Stage("mission_wait"); }
        else if (!Host()) Stage("mission_wait");
      } else if (stage_ == "mission_wait") {
        if (World().phase == GamePhase::Mission) {
          Require(Local() && Local()->alive && !Local()->incapacitated, "El despliegue no conserva al agente local activo");
          missionSeed_ = World().seed; Capture("06-mission-start"); Mark(options_.role + "-mission-seen.json"); Stage("mission_barrier");
        }
      } else if (stage_ == "mission_barrier") {
        const auto peer = Marker(Host() ? "client-mission-seen.json" : "host-mission-seen.json");
        if (peer.is_object()) {
          Require(peer.at("state").at("seed").get<std::uint32_t>() == missionSeed_, "La semilla de misión difiere entre equipos");
          Case("shared_mission_seed", {{"seed", missionSeed_}});
          if (options_.scenario != "mission") {
            const bool dropTarget = (options_.scenario == "host-drop") == Host();
            if (dropTarget && !Host()) { baselinePosition_ = Local()->position; Stage("client_drop_command"); }
            else if (dropTarget) { Mark(options_.role + "-drop-ready.json", {{"cases", cases_}, {"evidence", evidence_}}); Stage("await_forced_drop"); }
            else { baselineTick_ = World().tick; Stage(Host() ? "client_loss_wait" : "host_loss_wait"); }
          } else if (Host()) Stage("client_pause_wait");
          else {
            const auto *host = AgentById(GameplayConfig::HostPlayerId);
            Require(host != nullptr, "El cliente no conoce la posición pública del anfitrión");
            baselineTick_ = World().tick; baselinePosition_ = host->position; pausedLocalPosition_ = Local()->position;
            Access::Lobby(game_, context_, window_); Require(Access::Pause(game_), "No se abrió la pausa local LAN");
            Capture("07-client-local-pause"); Mark("client-pause-start.json"); Stage("client_paused");
          }
        }
      } else if (stage_ == "client_pause_wait") {
        if (Marker("client-pause-start.json").is_object()) {
          Require(Local() != nullptr, "Falta el anfitrión al comenzar la pausa remota");
          baselinePosition_ = Local()->position; baselineTick_ = World().tick; Stage("host_pause_probe");
        }
      } else if (stage_ == "host_pause_probe") {
        RequireMissionHealthy();
        if ((Local()->position - baselinePosition_).length() < 15.f) SubmitPilot(true);
        else SessionState().Submit({});
        if (Marker("client-pause-finished.json").is_object()) {
          Require(World().tick > baselineTick_ && (Local()->position - baselinePosition_).length() > 5.f, "El anfitrión no avanzó durante la pausa del cliente");
          Case("remote_pause_keeps_host_active", {{"ticksAdvanced", World().tick - baselineTick_}, {"hostDistance", (Local()->position - baselinePosition_).length()}});
          Stage("host_pilot");
        }
      } else if (stage_ == "client_paused") {
        if (StageSeconds() >= 4.f) {
          const auto *host = AgentById(GameplayConfig::HostPlayerId);
          Require(host && World().tick > baselineTick_ && (host->position - baselinePosition_).length() > 5.f, "La pausa local detuvo el reloj remoto o al anfitrión");
          Require(Local() && (Local()->position - pausedLocalPosition_).length() < 0.1f, "La pausa local conserva movimiento del cliente");
          Case("local_pause_remote_world_continues", {{"ticksAdvanced", World().tick - baselineTick_}, {"hostDistance", (host->position - baselinePosition_).length()}, {"realSeconds", StageSeconds()}});
          Capture("08-client-pause-world-advanced"); Click("REANUDAR OPERACIÓN"); Mark("client-pause-finished.json"); Stage("client_extraction_wait");
        }
      } else if (stage_ == "host_pilot") {
        RequireMissionHealthy(); SubmitPilot(true);
        if (World().objectives >= GetContract(World().contract).requiredObjectives && !objectiveSeen_) { objectiveSeen_ = true; Case("objective_collected_by_normal_interaction"); Capture("09-objective-collected"); Mark("objective-collected.json"); }
        if (Local() && Local()->extracted) { hostExtracted_ = true; Case("host_extracted_first"); Capture("10-host-extracted-waiting-client"); Mark("host-extracted.json"); Stage("host_results_wait"); }
      } else if (stage_ == "client_extraction_wait") {
        RequireMissionHealthy();
        SubmitPilot(false, false);
        const auto *host = AgentById(GameplayConfig::HostPlayerId);
        if (Marker("host-extracted.json").is_object() && host && host->extracted) {
          Require(Local() && !Local()->extracted && World().phase == GamePhase::Mission, "La extracción del anfitrión extrajo automáticamente al cliente");
          Capture("10-client-observes-host-extracted"); Stage("client_extraction_hold");
        }
      } else if (stage_ == "client_extraction_hold") {
        RequireMissionHealthy();
        if (StageSeconds() >= 2.f) { Case("first_extraction_waits_for_second"); Mark("extraction-wait-confirmed.json"); Stage("client_extract"); }
      } else if (stage_ == "client_extract") {
        if (World().phase == GamePhase::Mission) { RequireMissionHealthy(); SubmitPilot(false, true); }
        else if (World().phase == GamePhase::Results) Stage("results");
      } else if (stage_ == "host_results_wait") {
        if (Marker("extraction-wait-confirmed.json").is_object() && !objectiveSeen_) throw std::runtime_error("Se extrajo antes de registrar el objetivo");
        if (World().phase == GamePhase::Results) { Require(hostExtracted_, "Falta la extracción inicial del anfitrión"); Case("first_extraction_waits_for_second"); Stage("results"); }
      } else if (stage_ == "results") {
        const auto found = World().results.find(SessionState().GetLocalPlayerId());
        Require(found != World().results.end() && found->second.success && Local() && Local()->extracted, "El informe personal no confirma misión y extracción satisfactorias");
        personalResult_ = {{"success", found->second.success}, {"score", found->second.score}, {"rank", static_cast<int>(found->second.rank)}, {"reason", found->second.reason}};
        Case("mission_success_personal_report", {{"score", found->second.score}, {"reason", found->second.reason}, {"seed", missionSeed_}});
        Capture("11-success-report"); Mark(options_.role + "-results-seen.json"); Stage("return_base_wait");
      } else if (stage_ == "return_base_wait") {
        if (Host() && Marker("client-results-seen.json").is_object() && World().phase == GamePhase::Results) { Click("REGRESAR A LA BASE"); return; }
        if (World().phase == GamePhase::Base) {
          Require(Room() && std::ranges::none_of(SessionState().GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.ready; }), "Regresar a base perdió el equipo o no limpió la preparación");
          Access::Lobby(game_, context_, window_); Case("return_to_base_team_and_readiness"); Capture("12-base-after-success");
          Mark(options_.role + "-done.json"); Stage("done_barrier");
        }
      } else if (stage_ == "done_barrier") {
        if (Marker(Host() ? "client-done.json" : "host-done.json").is_object()) complete_ = true;
      } else if (stage_ == "client_drop_command") {
        RequireMissionHealthy();
        ActionCommand moving; moving.moveX = 1.f; moving.aimX = 1.f; SessionState().Submit(moving);
        if (Local()->moving && (Local()->position - baselinePosition_).length() > 5.f) {
          Case("client_command_active_before_forced_drop", {{"distance", (Local()->position - baselinePosition_).length()}});
          Capture("07-client-moving-before-drop");
          Mark("client-drop-ready.json", {{"cases", cases_}, {"evidence", evidence_}, {"activeCommand", {{"moveX", 1.f}, {"moveY", 0.f}}}});
          Stage("await_forced_drop");
        }
      } else if (stage_ == "await_forced_drop") {
        RequireMissionHealthy();
        if (!Host()) { ActionCommand moving; moving.moveX = 1.f; moving.aimX = 1.f; SessionState().Submit(moving); }
      } else if (stage_ == "client_loss_wait") {
        if (SessionState().GetLobbyPlayers().size() == 1 && Marker("client-forced-drop.json").is_object()) {
          Require(SessionState().IsConnected() && World().phase == GamePhase::Mission && World().tick > baselineTick_, "El anfitrión no mantiene la misión tras el corte del cliente");
          const auto disconnected = std::ranges::find_if(World().agents, [](const Agent &agent) { return agent.id != GameplayConfig::HostPlayerId; });
          Require(disconnected != World().agents.end() && !disconnected->connected, "El cliente cortado permanece conectado en la simulación");
          baselinePosition_ = disconnected->position; baselineTick_ = World().tick;
          Capture("07-host-client-disconnected"); Stage("client_loss_stability");
        }
      } else if (stage_ == "client_loss_stability") {
        const auto disconnected = std::ranges::find_if(World().agents, [](const Agent &agent) { return agent.id != GameplayConfig::HostPlayerId; });
        Require(disconnected != World().agents.end() && !disconnected->connected && (disconnected->position - baselinePosition_).length() < 0.1f, "El mando remoto continúa desplazando al agente tras el corte");
        if (StageSeconds() >= 0.5f) {
          Require(SessionState().IsConnected() && World().phase == GamePhase::Mission && World().tick > baselineTick_, "El anfitrión dejó de simular durante el corte remoto");
          Require(!disconnected->moving && !disconnected->running, "La presentación remota conserva movimiento tras el corte");
          Case("abrupt_client_loss_host_continues", {{"ticksAdvanced", World().tick - baselineTick_}, {"neutralizedActiveCommand", true}, {"stableSeconds", StageSeconds()}});
          Stage("rejoin_rejection_wait");
        }
      } else if (stage_ == "rejoin_rejection_wait") {
        if (Marker("rejoin-rejected.json").is_object()) {
          Require(World().phase == GamePhase::Mission, "El rechazo de reingreso alteró la misión");
          Case("mission_rejoin_rejected"); Access::Lobby(game_, context_, window_); Click("VOLVER A LA BASE"); Stage("host_recovery_base");
        }
      } else if (stage_ == "host_recovery_base") {
        if (World().phase == GamePhase::Base) { Access::Lobby(game_, context_, window_); Mark("host-base-ready.json"); Stage("recovery_room_wait"); }
      } else if (stage_ == "host_loss_wait") {
        if (!SessionState().IsConnected() && Marker("host-forced-drop.json").is_object()) {
          Require(World().phase == GamePhase::Base && World().agents.empty() && !SessionState().IsHost() && SessionState().GetStatus().starts_with("Error LAN:"), "La pérdida del anfitrión no vuelve a base con error controlado");
          Case("abrupt_host_loss_controlled_error_no_migration"); Capture("07-client-host-lost"); Mark("host-loss-seen.json"); OpenConnection(); Stage("host_restart_wait");
        }
      } else if (stage_ == "host_restart_wait") {
        if (Marker("host-restarted-ready.json").is_object()) { Connect(false); Stage("recovery_room_wait"); }
      } else if (stage_ == "recovery_start") {
        if (Host()) {
          if (Marker("host-loss-seen.json").is_object()) { Connect(true); Mark("host-restarted-ready.json"); Stage("recovery_room_wait"); }
        } else { Connect(false); Stage("mission_rejoin_rejection"); }
      } else if (stage_ == "mission_rejoin_rejection") {
        if (!SessionState().IsConnected() && SessionState().GetStatus().find("La operación ya comenzó") != std::string::npos) {
          Case("mission_rejoin_rejected", {{"status", SessionState().GetStatus()}}); Capture("08-mission-rejoin-rejected"); Mark("rejoin-rejected.json"); OpenConnection(); Stage("host_base_ready_wait");
        } else if (StageSeconds() > 12.f) throw std::runtime_error("El reingreso durante misión no produjo el rechazo previsto: " + SessionState().GetStatus());
      } else if (stage_ == "host_base_ready_wait") {
        if (Marker("host-base-ready.json").is_object()) { Connect(false); Stage("recovery_room_wait"); }
      } else if (stage_ == "recovery_room_wait") {
        if (Room()) {
          Require(std::ranges::none_of(SessionState().GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.ready; }), "La reconexión conserva preparación antigua");
          Case("recovered_room_two_agents", {{"recoveryAttempt", recovery_}}); Capture("09-recovered-room"); Mark(options_.role + "-recovery-room.json"); Stage("recovery_barrier");
        }
      } else if (stage_ == "recovery_barrier") {
        if (Marker(Host() ? "client-recovery-room.json" : "host-recovery-room.json").is_object()) complete_ = true;
      }
    }

    void RequireMissionHealthy() {
      Require(SessionState().IsConnected(), "La sesión perdió conexión durante " + stage_ + ": " + SessionState().GetStatus());
      Require(World().phase == GamePhase::Mission, "La misión terminó antes de completar " + stage_);
      Require(Local() && Local()->alive && !Local()->incapacitated && Local()->health > 0.f, "El agente quedó fuera de combate durante " + stage_);
    }
  };
}

int main(int argc, char **argv) {
  try {
    auto options = Parse(argc, argv);
    std::filesystem::create_directories(options.output);
    CreateSandbox(options);
    return Runner(std::move(options)).Run();
  } catch (const std::exception &error) {
    std::cerr << "FAIL QA bootstrap: " << error.what() << '\n';
    return 1;
  }
}
