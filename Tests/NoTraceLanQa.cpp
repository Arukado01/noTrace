#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#undef near
#undef far
#endif
#include "NoTraceGame.h"
#include "NoTraceLanQaPilot.h"
#include "NoTraceLanQaMechanics.h"
#include "NoTraceLanQaVideo.h"
#include "NoTraceOutcomes.h"
#include "NoTraceFirstPersonGeometry.h"
#include "NoTraceInteraction.h"
#include "NoTracePerformance.h"
#include "Config/NoTraceWeaponConfig.h"
#include "Core/EngineConfig.h"
#include <SFML/GpuPreference.hpp>
#include <SFML/OpenGL.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Sleep.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

SFML_DEFINE_DISCRETE_GPU_PREFERENCE

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
    static bool FirstPerson(const NoTrace::Game &game) { return game.firstPerson_; }
    static NoTrace::FirstPersonCamera Camera(const NoTrace::Game &game) { return game.firstPersonCamera_; }
    static void LookAt(NoTrace::Game &game, const NoTrace::Agent &actor, sf::Vector3f target) {
      const auto eye = NoTrace::FirstPersonGeometry::Eye(actor);
      const auto delta = target - eye;
      game.firstPersonCamera_.yaw = std::atan2(delta.y, delta.x);
      game.firstPersonCamera_.pitch = std::atan2(delta.z, std::hypot(delta.x, delta.y));
    }
    static void Key(NoTrace::Game &game, EngineContext &context, sf::Keyboard::Scan scan, bool down, bool shift = false) {
      if (down) { sf::Event::KeyPressed event; event.scancode = scan; event.shift = shift; context.input.CaptureEvent(event); game.OnEvent(event); }
      else { sf::Event::KeyReleased event; event.scancode = scan; event.shift = shift; context.input.CaptureEvent(event); game.OnEvent(event); }
    }
    static void PerspectiveChord(NoTrace::Game &game, EngineContext &context) {
      Key(game, context, sf::Keyboard::Scan::LShift, true, true); Key(game, context, NoTrace::ControlsConfig::PerspectiveKey, true, true);
      Key(game, context, NoTrace::ControlsConfig::PerspectiveKey, false); Key(game, context, sf::Keyboard::Scan::LShift, false);
    }
    static void Fire(EngineContext &context) {
      const sf::Vector2i point{};
      context.input.CaptureEvent(sf::Event::MouseButtonPressed{sf::Mouse::Button::Left, point});
      context.input.CaptureEvent(sf::Event::MouseButtonReleased{sf::Mouse::Button::Left, point});
    }
    static void Look(EngineContext &context, sf::Vector2f delta) { context.cursor.SetPosition(context.window.GetLogicalSize() * .5f + delta); }
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
    struct RenderTimes { double render = 0., present = 0.; };
    static RenderTimes Render(NoTrace::Game &game, EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &capture = {}, NoTrace::Qa::VideoRecorder *video = nullptr) {
      const auto started = std::chrono::steady_clock::now();
      window.setView(context.window.GetPresentationView()); window.clear();
      context.renderer.BeginDrawing(); game.Render();
      window.draw(sf::Sprite(context.renderer.FinishDrawing())); context.gui.Render();
      if (!capture.empty()) {
        sf::Texture pixels(window.getSize()); pixels.update(window);
        if (!pixels.copyToImage().saveToFile(capture)) throw std::runtime_error("No se pudo guardar la captura gráfica");
      }
      const auto submitted = std::chrono::steady_clock::now();
      if (video) video->Record(window);
      window.display();
      return {std::chrono::duration<double, std::milli>(submitted - started).count(),
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - submitted).count()};
    }
  };
}

namespace {
  using Json = nlohmann::json;
  using Access = EngineTests::GuiAccess;
  using namespace NoTrace;
  using Path = std::filesystem::path;
  using Clock = std::chrono::steady_clock;
#ifdef NO_TRACE_PROFILE
  constexpr bool ProfilingCompiled = true;
#else
  constexpr bool ProfilingCompiled = false;
#endif

  struct Cancelled final : std::runtime_error { using std::runtime_error::runtime_error; };
  struct Options {
    std::string role, scenario, host, runId;
    Path coord, output, recordFfmpeg;
    unsigned int port = 55041;
    unsigned int timeout = 360;
    unsigned int frameLimit = 30, width = 1280, height = 720, warmup = 0;
    int windowX = -1, windowY = 0;
    bool profile = false;
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
      else if (key == "--record-ffmpeg") options.recordFfmpeg = std::filesystem::absolute(value);
      else if (key == "--timeout") options.timeout = static_cast<unsigned int>(std::stoul(value));
      else if (key == "--frame-limit") options.frameLimit = static_cast<unsigned int>(std::stoul(value));
      else if (key == "--width") options.width = static_cast<unsigned int>(std::stoul(value));
      else if (key == "--height") options.height = static_cast<unsigned int>(std::stoul(value));
      else if (key == "--warmup") options.warmup = static_cast<unsigned int>(std::stoul(value));
      else if (key == "--window-x") options.windowX = std::stoi(value);
      else if (key == "--window-y") options.windowY = std::stoi(value);
      else if (key == "--profile" && (value == "true" || value == "false" || value == "1" || value == "0")) options.profile = value == "true" || value == "1";
      else throw std::runtime_error("Opción QA desconocida: " + key);
    }
    if ((options.role != "host" && options.role != "client") ||
        (options.scenario != "mission" && options.scenario != "tour" && options.scenario != "table-tour" && options.scenario != "mechanics" && options.scenario != "training" && options.scenario != "pipe" &&
         options.scenario != "client-drop" && options.scenario != "host-drop" && options.scenario != "fps" && options.scenario != "showcase" && options.scenario != "betrayal" && options.scenario != "fps-enemy") ||
        options.host.empty() || options.port == 0 || options.port > 65535 || options.timeout == 0 || options.timeout > 3600 ||
        options.frameLimit > 1000 || options.width < 640 || options.width > 7680 || options.height < 360 || options.height > 4320 || options.warmup > 10000 ||
        options.runId.size() != 32 || !std::ranges::all_of(options.runId, [](unsigned char value) { return std::isxdigit(value) != 0; }) ||
        options.coord.empty() || options.output.empty()) throw std::runtime_error("Contrato CLI QA inválido");
    if (options.profile && !ProfilingCompiled) throw std::runtime_error("Este binario no incluye NO_TRACE_PROFILE; usa NoTraceLanPerformanceTests para --profile true");
    return options;
  }

  double Milliseconds(Clock::time_point started) { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); }
  double CpuMilliseconds() {
#ifdef _WIN32
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) throw std::runtime_error("No se pudo medir CPU del proceso QA");
    const auto ticks = [](FILETIME value) { ULARGE_INTEGER time{}; time.LowPart = value.dwLowDateTime; time.HighPart = value.dwHighDateTime; return time.QuadPart; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.;
#else
    return 0.;
#endif
  }
  unsigned long ProcessId() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return 0;
#endif
  }
  Json Distribution(std::vector<double> values) {
    if (values.empty()) return {{"samples", 0}};
    std::sort(values.begin(), values.end());
    const double mean = std::accumulate(values.begin(), values.end(), 0.) / values.size();
    const auto percentile = [&](double fraction) { return values[static_cast<std::size_t>(fraction * (values.size() - 1))]; };
    return {{"samples", values.size()}, {"meanMs", mean}, {"p50Ms", percentile(.5)}, {"p95Ms", percentile(.95)}, {"p99Ms", percentile(.99)},
        {"maxMs", values.back()}, {"averageFps", mean > 0. ? 1000. / mean : 0.}, {"minimumFps", values.back() > 0. ? 1000. / values.back() : 0.}};
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
        : options_(std::move(options)), window_(sf::VideoMode({options_.width, options_.height}), "NO TRACE / QA LAN / " + options_.role + " / " + options_.scenario),
          context_(window_), game_(context_), started_(Clock::now()), entered_(started_) {
      std::filesystem::create_directories(options_.coord);
      std::filesystem::create_directories(options_.output / "captures");
      window_.setKeyRepeatEnabled(false); window_.setVisible(true);
      Access::Initialize(context_); context_.audio.SetGlobalVolume(gConfig.globalVolume);
      game_.Start(); recovery_ = Marker(options_.role + "-forced-drop.json").is_object();
      window_.setVerticalSyncEnabled(false); window_.setFramerateLimit(options_.frameLimit);
      if (options_.windowX >= 0) window_.setPosition({options_.windowX, options_.windowY});
      stage_ = recovery_ ? "recovery_start" : "start";
      if (options_.scenario == "tour") pilot_.EnableTour();
      frames_.reserve(12000); measuredStarted_ = Clock::now(); cpuStarted_ = CpuMilliseconds();
      if (options_.profile) profileFrames_.reserve(12000);
      if (!options_.recordFfmpeg.empty()) {
        Require(options_.frameLimit == 30, "La grabación requiere treinta cuadros por segundo");
        video_ = std::make_unique<NoTrace::Qa::VideoRecorder>(options_.recordFfmpeg, options_.output / "gameplay.mp4", options_.width, options_.height);
      }
      Profiling::Reset(); Profiling::enabled = options_.profile;
    }

    int Run() {
      std::string result = "PASS", reason;
      try {
        while (!complete_) {
          std::optional<std::array<Profiling::Counter, Profiling::StageCount>> profileBefore;
          if (options_.profile) profileBefore = Profiling::counters;
          const auto frameStarted = Clock::now();
          const auto frameStage = MechanicsScenario() && stage_.starts_with("mechanics_") ? stage_ + "/" + (Host() ? mechanics_.Stage() : mechanicsObservedStage_) : stage_;
          capturedThisFrame_ = false;
          double updateMs = 0., pilotMs = 0., reportMs = 0.;
          if (Elapsed() > options_.timeout) throw std::runtime_error("Plazo QA agotado en " + stage_);
          CheckCancellation();
          context_.input.BeginFrame();
          while (const auto event = window_.pollEvent()) {
            if (event->is<sf::Event::Closed>()) throw Cancelled("Ventana QA cerrada por el usuario");
            if (const auto *key = event->getIf<sf::Event::KeyPressed>(); key && key->scancode == sf::Keyboard::Scan::Escape) throw Cancelled("Escape cancela la prueba QA");
            if (const auto *resize = event->getIf<sf::Event::Resized>()) { context_.window.HandleResize(resize->size); Access::Initialize(context_); }
          }
          Access::Time(context_);
          if (options_.scenario == "fps" || (Showcase() && stage_.starts_with("fps_")) || stage_.starts_with("enemy_")) {
            auto measured = Clock::now(); Drive(); pilotMs += Milliseconds(measured);
            measured = Clock::now(); game_.Update(); updateMs += Milliseconds(measured);
          }
          else {
            auto measured = Clock::now(); game_.Update(); updateMs += Milliseconds(measured);
            measured = Clock::now(); ActionCommand idle; idle.aimX = 1.f; SessionState().Submit(idle); Drive(); pilotMs += Milliseconds(measured);
          }
          auto measured = Clock::now(); game_.UpdateBackground(); updateMs += Milliseconds(measured);
          measured = Clock::now(); context_.audio.Update(context_.time.GetDeltaTime());
          const double audioManagerMs = Milliseconds(measured); updateMs += audioManagerMs;
          const auto rendered = Access::Render(game_, context_, window_, {}, video_.get());
          if (Elapsed() - progressAt_ >= 1.f) {
            measured = Clock::now(); progressAt_ = Elapsed(); WriteJson(options_.output / "qa-progress.json", Report("RUNNING", "")); reportMs = Milliseconds(measured);
          }
          FrameSample sample;
          sample.stage = frameStage; sample.seconds = Elapsed(); sample.phase = static_cast<int>(World().phase); sample.tick = World().tick;
          sample.update = updateMs; sample.pilot = pilotMs; sample.render = rendered.render; sample.present = rendered.present;
          sample.audioManager = audioManagerMs;
          sample.report = reportMs; sample.capture = capturedThisFrame_;
          if (const auto *actor = Local()) {
            sample.x = actor->position.x; sample.y = actor->position.y; sample.elevation = actor->elevation; sample.support = actor->supportId;
            sample.traversal = static_cast<int>(actor->traversal.mode); sample.hidden = actor->hidden; sample.incapacitated = actor->incapacitated;
          }
          sample.raw = Milliseconds(frameStarted); frames_.push_back(std::move(sample));
          if (profileBefore) {
            ProfileFrameSample profile;
            for (std::size_t index = 0; index < Profiling::StageCount; ++index) {
              profile.milliseconds[index] = Profiling::counters[index].milliseconds - (*profileBefore)[index].milliseconds;
              profile.calls[index] = Profiling::counters[index].calls - (*profileBefore)[index].calls;
            }
            profileFrames_.push_back(std::move(profile));
          }
        }
      } catch (const Cancelled &error) { result = "CANCELLED"; reason = error.what(); }
      catch (const std::exception &error) { result = "FAIL"; reason = error.what(); }
      if (result != "PASS") {
        cases_.push_back({{"name", stage_}, {"status", result}, {"detail", reason}, {"seconds", Elapsed()}});
        try { Capture("failure"); } catch (...) {}
      }
      Profiling::enabled = false;
      if (video_) {
        try { video_->Close(); }
        catch (const std::exception &error) {
          if (!reason.empty()) reason += "; ";
          reason += "No se pudo finalizar el video: " + std::string(error.what());
          result = "FAIL";
          cases_.push_back({{"name", "video_finalize"}, {"status", "FAIL"}, {"detail", error.what()}, {"seconds", Elapsed()}});
        }
      }
      WritePerformance();
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
    NoTrace::Qa::MechanicsPilot mechanics_;
    std::unique_ptr<NoTrace::Qa::VideoRecorder> video_;
    Clock::time_point started_, entered_;
    std::string stage_;
    Json cases_ = Json::array(), evidence_ = Json::array(), personalResult_;
    std::uint32_t missionSeed_ = 0;
    std::uint64_t baselineTick_ = 0;
    sf::Vector2f baselinePosition_, pausedLocalPosition_;
    sf::Vector2f pilotProgressPosition_;
    float pilotProgressAt_ = -1.f, pilotProgressElevation_ = 0.f;
    bool pilotStallCaptured_ = false;
    float progressAt_ = -1.f;
    float extractionDeniedAt_ = -1.f;
    bool recovery_ = false, complete_ = false, objectiveSeen_ = false, clientLossEndedMission_ = false;
    int fpsMagazine_ = 0, fpsAmmo_ = 0;
    float fpsWear_ = 0.f;
    std::size_t fpsShots_ = 0;
    FirstPersonCamera fpsCamera_;
    struct FrameSample {
      std::string stage;
      double seconds = 0., update = 0., pilot = 0., render = 0., present = 0., raw = 0., report = 0., audioManager = 0.;
      float x = 0.f, y = 0.f, elevation = 0.f;
      std::uint64_t tick = 0;
      std::uint32_t support = 0;
      int phase = 0, traversal = 0;
      bool capture = false, hidden = false, incapacitated = false;
    };
    std::vector<FrameSample> frames_;
    struct ProfileFrameSample {
      std::array<double, Profiling::StageCount> milliseconds{};
      std::array<std::uint64_t, Profiling::StageCount> calls{};
    };
    std::vector<ProfileFrameSample> profileFrames_;
    Json performance_;
    double cpuStarted_ = 0.;
    Clock::time_point measuredStarted_;
    bool capturedThisFrame_ = false;
    std::string mechanicsPublishedStage_, mechanicsObservedStage_;
    Json mechanicsObservations_ = Json::array(), mechanicsRemoteCoverage_;

    void WritePerformance() {
      const double wallSeconds = Milliseconds(measuredStarted_) / 1000.;
      const double cpuMs = CpuMilliseconds() - cpuStarted_;
      std::ofstream csv(options_.output / "qa-frames.csv", std::ios::binary | std::ios::trunc);
      if (!csv) throw std::runtime_error("No se pudo guardar la telemetría QA");
      csv << "frame,role,pid,stage,phase,tick,seconds,updateMs,renderMs,presentMs,pilotMs,reportMs,rawTotalMs,capture,x,y,elevation,support,traversal,hidden,incapacitated,audioManagerMs\n" << std::fixed << std::setprecision(6);
      std::ofstream profileCsv;
      if (options_.profile) {
        if (profileFrames_.size() != frames_.size()) throw std::runtime_error("Las muestras de perfil no corresponden a los cuadros QA");
        profileCsv.open(options_.output / "qa-profile-frames.csv", std::ios::binary | std::ios::trunc);
        if (!profileCsv) throw std::runtime_error("No se pudo guardar el perfil por cuadro QA");
        profileCsv << "frame,role,pid,stage,phase,tick,seconds,updateMs,renderMs,presentMs,pilotMs,reportMs,rawTotalMs,capture";
        for (const auto name : Profiling::StageNames) profileCsv << ',' << name << "Ms," << name << "Calls";
        profileCsv << '\n' << std::fixed << std::setprecision(6);
      }
      std::map<std::string, std::vector<const FrameSample *>> groups;
      for (std::size_t index = 0; index < frames_.size(); ++index) {
        const auto &frame = frames_[index];
        csv << index << ',' << options_.role << ',' << ProcessId() << ',' << frame.stage << ',' << frame.phase << ',' << frame.tick << ',' << frame.seconds << ','
            << frame.update << ',' << frame.render << ',' << frame.present << ',' << frame.pilot << ',' << frame.report << ',' << frame.raw << ',' << frame.capture << ','
            << frame.x << ',' << frame.y << ',' << frame.elevation << ',' << frame.support << ',' << frame.traversal << ',' << frame.hidden << ',' << frame.incapacitated << ',' << frame.audioManager << '\n';
        if (options_.profile) {
          profileCsv << index << ',' << options_.role << ',' << ProcessId() << ',' << frame.stage << ',' << frame.phase << ',' << frame.tick << ',' << frame.seconds << ','
              << frame.update << ',' << frame.render << ',' << frame.present << ',' << frame.pilot << ',' << frame.report << ',' << frame.raw << ',' << frame.capture;
          const auto &profile = profileFrames_[index];
          for (std::size_t stage = 0; stage < Profiling::StageCount; ++stage) profileCsv << ',' << profile.milliseconds[stage] << ',' << profile.calls[stage];
          profileCsv << '\n';
        }
        if (index < options_.warmup) continue;
        groups["all"].push_back(&frame);
        if (!frame.capture && frame.report == 0.) {
          groups["without_capture_or_report"].push_back(&frame);
          groups["stage/" + frame.stage].push_back(&frame);
        }
      }
      if (!csv) throw std::runtime_error("Falló la escritura de la telemetría QA");
      if (options_.profile && !profileCsv) throw std::runtime_error("Falló la escritura del perfil por cuadro QA");
      performance_ = {{"schema", 1}, {"runId", options_.runId}, {"role", options_.role}, {"pid", ProcessId()}, {"protocol", GameplayConfig::ProtocolVersion},
          {"scenario", options_.scenario}, {"frameLimit", options_.frameLimit}, {"verticalSync", false}, {"warmupFrames", options_.warmup}, {"totalFrames", frames_.size()},
          {"nativeSize", {window_.getSize().x, window_.getSize().y}}, {"logicalSize", {context_.window.GetLogicalSize().x, context_.window.GetLogicalSize().y}},
          {"instrumentation", options_.profile}, {"profilingCompiled", ProfilingCompiled}, {"audioManagerTick", true}, {"wallSeconds", wallSeconds}, {"cpuMilliseconds", cpuMs},
          {"cpuCoreEquivalent", wallSeconds > 0. ? cpuMs / (wallSeconds * 1000.) : 0.}, {"framesFile", "qa-frames.csv"},
          {"measurement", "steady_clock real; raw incluye piloto, capturas y reportes; render incluye FinishDrawing/GUI; present incluye límite si se solicita; tiempos de ámbitos inclusivos"}};
      performance_["recording"] = video_ != nullptr;
      if (video_) {
        performance_["videoFile"] = "gameplay.mp4";
        performance_["videoFrameRate"] = 30;
        performance_["measurement"] = performance_["measurement"].get<std::string>() + "; present incluye lectura GPU a CPU y escritura al pipe de FFmpeg durante la grabación";
      }
      if (options_.profile) {
        performance_["profileFramesFile"] = "qa-profile-frames.csv";
        performance_["profileFramesMeasurement"] = "Deltas por cuadro de ámbitos inclusivos en el hilo principal; incluyen trabajo del piloto y capturas señaladas; no sumar ámbitos anidados";
      }
      const auto glText = [](unsigned int field) { const auto *text = glGetString(field); return text ? std::string(reinterpret_cast<const char *>(text)) : std::string("desconocido"); };
      performance_["glVendor"] = glText(GL_VENDOR); performance_["glRenderer"] = glText(GL_RENDERER); performance_["glVersion"] = glText(GL_VERSION);
#ifdef NDEBUG
      performance_["configuration"] = "Release";
#else
      performance_["configuration"] = "Debug";
#endif
      for (const auto &[name, samples] : groups) {
        const auto metric = [&](double FrameSample::*member) { std::vector<double> values; values.reserve(samples.size()); for (const auto *frame : samples) values.push_back(frame->*member); return Distribution(std::move(values)); };
        performance_["groups"][name] = {{"rawTotal", metric(&FrameSample::raw)}, {"update", metric(&FrameSample::update)}, {"render", metric(&FrameSample::render)},
            {"present", metric(&FrameSample::present)}, {"pilot", metric(&FrameSample::pilot)}, {"report", metric(&FrameSample::report)}, {"audioManager", metric(&FrameSample::audioManager)}};
      }
      if (options_.profile) for (std::size_t index = 0; index < Profiling::StageCount; ++index) {
        const auto stage = static_cast<Profiling::Stage>(index); const auto &counter = Profiling::counters[index];
        performance_["profile"][std::string(Profiling::Name(stage))] = {{"milliseconds", counter.milliseconds}, {"calls", counter.calls}};
      }
      WriteJson(options_.output / "qa-performance.json", performance_);
    }

    Session &SessionState() { return Access::Session(game_); }
    const WorldState &World() { return SessionState().GetWorld(); }
    bool Host() const { return options_.role == "host"; }
    bool Showcase() const { return options_.scenario == "showcase" || options_.scenario == "betrayal"; }
    bool TrainingMechanicsScenario() const { return options_.scenario == "mechanics" || options_.scenario == "training" || options_.scenario == "pipe" || Showcase(); }
    bool MechanicsScenario() const { return TrainingMechanicsScenario() || options_.scenario == "table-tour"; }
    float Elapsed() const { return std::chrono::duration<float>(Clock::now() - started_).count(); }
    float StageSeconds() const { return std::chrono::duration<float>(Clock::now() - entered_).count(); }
    void Stage(const std::string &stage) { stage_ = stage; entered_ = Clock::now(); std::cout << "QA_STAGE " << stage << '\n' << std::flush; }
    void Require(bool condition, const std::string &message) { if (!condition) throw std::runtime_error(message); }
    void Case(const std::string &name, Json detail = Json::object()) {
      cases_.push_back({{"name", name}, {"status", "PASS"}, {"detail", std::move(detail)}, {"seconds", Elapsed()}});
    }
    const Agent *AgentById(PlayerId id) { const auto &agents = World().agents; const auto found = std::ranges::find_if(agents, [id](const Agent &agent) { return agent.id == id; }); return found == agents.end() ? nullptr : &*found; }
    const Agent *Local() { return AgentById(SessionState().GetLocalPlayerId()); }
    bool Room() { return SessionState().IsConnected() && World().phase == GamePhase::Base && SessionState().GetLobbyPlayers().size() == 2 && Local() && Local()->connected &&
      (!Host() || std::ranges::count_if(World().agents, [](const Agent &agent) { return agent.connected; }) == 2); }
    bool AllReady() { return Room() && std::ranges::all_of(SessionState().GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.ready; }); }
    Json State() {
      Json state{{"connected", SessionState().IsConnected()}, {"phase", static_cast<int>(World().phase)}, {"tick", World().tick}, {"seed", World().seed}, {"elapsed", World().elapsed}, {"player", SessionState().GetLocalPlayerId()}, {"rosterCount", SessionState().GetLobbyPlayers().size()}, {"status", SessionState().GetStatus()}};
      if (options_.scenario == "fps") {
        const auto camera = Access::Camera(game_);
        state["localCameraEvidence"] = {{"firstPerson", Access::FirstPerson(game_)}, {"yaw", camera.yaw}, {"pitch", camera.pitch}, {"fov", camera.fieldOfView}};
      }
      if (const auto *local = Local()) {
        state["local"] = {{"position", {local->position.x, local->position.y}}, {"elevation", local->elevation}, {"support", local->supportId},
          {"traversal", static_cast<int>(local->traversal.mode)}, {"health", local->health}, {"stamina", local->stamina}, {"incapacitated", local->incapacitated},
          {"lifeState", static_cast<int>(local->lifeState)}, {"draggingGuard", local->draggingGuard}, {"extracted", local->extracted}, {"pilot", pilot_.Stage()}};
        if (const auto *body = Interaction::FindBody(World(), *local)) state["local"]["nearbyBody"] = {{"id", body->id}, {"position", {body->position.x, body->position.y}}, {"elevation", body->elevation}};
        if (const auto *object = Interaction::FindObject(World(), *local)) state["local"]["selectedObject"] = {{"id", object->id}, {"kind", static_cast<int>(object->kind)}, {"position", {object->position.x, object->position.y}}, {"elevation", object->elevation}};
      }
      return state;
    }
    Json Report(const std::string &status, const std::string &reason) {
      Json result{{"schema", 1}, {"runId", options_.runId}, {"role", options_.role}, {"scenario", options_.scenario}, {"protocol", GameplayConfig::ProtocolVersion}, {"status", status}, {"reason", reason}, {"stage", stage_}, {"seconds", Elapsed()}, {"recoveryAttempt", recovery_}, {"missionSeed", missionSeed_}, {"cases", cases_}, {"evidence", evidence_}, {"state", State()}, {"inputMode", "game_actions_and_tgui_events"}, {"saveIsolation", "output/runtime/Content"}};
      if (personalResult_.is_object()) result["personalResult"] = personalResult_;
      result["routeCoverage"] = {{"visitedSupports", pilot_.VisitedSupports()}, {"completedCheckpoints", pilot_.CompletedCheckpoints()}, {"tourComplete", pilot_.TourDone()},
          {"method", "comandos ActionCommand sobre geometría de misión; sin posicionar al agente en puntos del recorrido"}};
      if (options_.scenario == "table-tour") result["routeCoverage"]["initialPlacement"] = "único fixture explícito de inspección de mesa antes del recorrido; no se repiten estaciones de entrenamiento";
      if (options_.scenario == "tour" || options_.scenario == "table-tour" || options_.scenario == "mechanics")
        result["routeCoverage"]["cameraPolicy"] = "recorrido permite contacto con vigilancia real; cámaras y guardias siguen activos, trampas y colisiones se evitan";
      if (Showcase() || options_.scenario == "fps-enemy") {
        result["inputMode"] = "scripted_game_actions_tgui_events_and_explicit_shot_setups";
        result["routeCoverage"]["method"] = "rodaje por planos con colocaciones explícitas de actores entre tomas; interacciones, disparos y caídas comprobados en la simulación autoritativa";
        result["routeCoverage"]["continuousRoute"] = false;
        if (options_.scenario == "fps-enemy") result["routeCoverage"]["cameraPolicy"] = "cámara local orientada por el guion LookAt para el disparo y la observación del cuerpo";
      }
      if (MechanicsScenario()) {
        result["mechanicsCoverage"] = Host() ? mechanics_.Coverage() : mechanicsRemoteCoverage_;
        result["mechanicsObservations"] = mechanicsObservations_;
        result["mechanicsCoverageSource"] = Host() ? "estados autoritativos del anfitrión tras comandos" : "informe autoritativo recibido; observaciones locales y capturas separadas";
      }
      if (performance_.is_object()) result["performance"] = {{"file", "qa-performance.json"}, {"frames", frames_.size()}, {"pid", ProcessId()}};
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
      capturedThisFrame_ = true;
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
    void CheckPilotProgress() {
      const auto *local = Local();
      const auto &stage = pilot_.Stage();
      const bool expectsProgress = stage == "esperando_ruta_transitable" || stage == "recuperando_archivo" || stage == "regresando_extraccion" || stage == "recorriendo_nivel" || stage == "abriendo_puerta_del_recorrido";
      if (!local || !expectsProgress) { pilotProgressAt_ = -1.f; pilotStallCaptured_ = false; return; }
      if (pilotProgressAt_ < 0.f || (local->position - pilotProgressPosition_).length() > Qa::MissionPilotConfig::ProgressDistance ||
          std::abs(local->elevation - pilotProgressElevation_) > Qa::MissionPilotConfig::ProgressDistance) {
        pilotProgressAt_ = Elapsed(); pilotProgressPosition_ = local->position; pilotProgressElevation_ = local->elevation; pilotStallCaptured_ = false;
      }
      const float stationary = Elapsed() - pilotProgressAt_;
      if (stationary >= Qa::MissionPilotConfig::StuckEvidenceSeconds && !pilotStallCaptured_) {
        Case("pilot_stalled_replan", State().at("local")); Capture("pilot-stalled-" + std::to_string(World().tick));
        pilot_.Reset(); pilotStallCaptured_ = true;
      }
      Require(stationary < Qa::MissionPilotConfig::StuckLimitSeconds, "Piloto sin avance físico tras replantear ruta: " + State().at("local").dump());
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

    void PlaceShowcaseArchive() {
      auto &world = const_cast<WorldState &>(World());
      const auto terminal = std::ranges::find(world.objects, ObjectKind::Terminal, &Interactable::kind);
      Require(terminal != world.objects.end() && !terminal->completed, "Falta el archivo pendiente para el cierre");
      auto placements = Json::array();
      for (auto &actor : world.agents) {
        if (actor.privateOrder == PrivateOrder::Traitor) {
          const auto camera = std::ranges::find(world.objects, 13u, &Interactable::id);
          Require(camera != world.objects.end() && camera->kind == ObjectKind::Camera, "Falta la cámara del archivo");
          const auto point = camera->position + sf::Vector2f{0.f, 36.f};
          auto probe = actor; probe.position = point; probe.elevation = camera->elevation; probe.supportId = camera->supportId;
          const auto *selected = Interaction::FindObject(world, probe);
          Require(!WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius, camera->elevation, CombatConfig::StandingHeight, camera->supportId) && selected && selected->id == camera->id, "No hay acceso de herramienta libre a la cámara del archivo");
          placements.push_back({{"actor", actor.id}, {"to", {point.x, point.y, camera->elevation}}, {"purpose", "Sabotaje real mediante T antes de recoger el archivo"}});
          actor.position = point; actor.elevation = camera->elevation; actor.supportId = camera->supportId; actor.facing = {0.f, -1.f};
          continue;
        }
        bool placed = false;
        for (const float distance : {36.f, 42.f, 54.f, 68.f}) {
          for (const auto direction : std::array<sf::Vector2f, 8>{{{-1.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}, {.7071f, .7071f}, {-.7071f, .7071f}, {.7071f, -.7071f}, {-.7071f, -.7071f}}}) {
            const auto point = terminal->position + direction * distance;
            const auto support = WorldGeometry::FindSupport(world, point, terminal->elevation, terminal->supportId, GameplayConfig::AgentRadius);
            if (!support || support->id != terminal->supportId || WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius, support->elevation, CombatConfig::StandingHeight, support->id)) continue;
            if (std::ranges::any_of(world.agents, [&](const Agent &other) { return other.id != actor.id && std::abs(other.elevation - support->elevation) < CombatConfig::StandingHeight && (other.position - point).length() < GameplayConfig::AgentRadius * 2.f + 2.f; })) continue;
            auto probe = actor; probe.position = point; probe.elevation = support->elevation; probe.supportId = support->id;
            if (actor.privateOrder != PrivateOrder::Traitor && (!Interaction::FindObject(world, probe) || Interaction::FindObject(world, probe)->id != terminal->id || Interaction::FindBody(world, probe))) continue;
            placements.push_back({{"actor", actor.id}, {"from", {actor.position.x, actor.position.y, actor.elevation}}, {"to", {point.x, point.y, support->elevation}}});
            actor.position = point; actor.elevation = support->elevation; actor.supportId = support->id; actor.facing = -direction;
            placed = true; break;
          }
          if (placed) break;
        }
        Require(placed, "No se encontró un acceso físico libre al archivo");
      }
      Case("showcase_archive_shot_setup", {{"placements", placements}, {"note", "Preparación entre cortes; recoger el objetivo requiere E autoritativa"}});
    }

    void PlaceShowcaseBetrayal() {
      auto &world = const_cast<WorldState &>(World());
      auto traitor = std::ranges::find(world.agents, PrivateOrder::Traitor, &Agent::privateOrder);
      Require(traitor != world.agents.end() && world.agents.size() == 2, "El cierre requiere un traidor real entre dos agentes");
      auto victim = std::ranges::find_if(world.agents, [&](const Agent &actor) { return actor.id != traitor->id; });
      for (const auto &surface : world.walkSurfaces) if (surface.enabled && surface.slope.lengthSquared() == 0.f && surface.elevation >= CqcConfig::FatalPushFallHeight && surface.kind != WalkSurfaceKind::ElevatorCabin) {
        const auto center = surface.bounds.position + surface.bounds.size * .5f, end = surface.bounds.position + surface.bounds.size;
        for (const auto direction : std::array<sf::Vector2f, 4>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}}}) {
          auto target = center;
          if (direction.x) target.x = direction.x > 0.f ? end.x - 16.f : surface.bounds.position.x + 16.f;
          else target.y = direction.y > 0.f ? end.y - 16.f : surface.bounds.position.y + 16.f;
          const auto position = target - direction * CqcConfig::GripSeparation;
          const auto landing = ForcedFalls::Landing(world, target + direction * CqcConfig::PushDistance, surface.elevation);
          if (!landing.id || surface.elevation - landing.elevation < CqcConfig::FatalPushFallHeight ||
              WorldGeometry::PositionBlocked(world, position, GameplayConfig::AgentRadius, surface.elevation, CombatConfig::StandingHeight, surface.id) ||
              WorldGeometry::PositionBlocked(world, target, GameplayConfig::AgentRadius, surface.elevation, CombatConfig::StandingHeight, surface.id)) continue;
          auto probeWorld = world;
          auto &attacker = *std::ranges::find(probeWorld.agents, traitor->id, &Agent::id), &other = *std::ranges::find(probeWorld.agents, victim->id, &Agent::id);
          attacker.position = position; attacker.elevation = surface.elevation; attacker.supportId = surface.id; attacker.facing = direction;
          other.position = target; other.elevation = surface.elevation; other.supportId = surface.id; other.facing = direction;
          const auto selected = Simulation::FindCqcTarget(probeWorld, attacker, Simulation::CqcAction::Push);
          if (!selected || selected->agent != victim->id) continue;
          Case("showcase_betrayal_shot_setup", {{"traitor", traitor->id}, {"victim", victim->id}, {"objectives", world.objectives}, {"alarmCount", world.alarmCount}, {"support", surface.id}, {"from", {position.x, position.y, surface.elevation}}, {"target", {target.x, target.y, surface.elevation}}, {"note", "Preparación entre cortes sobre borde real; V y la caída deben causar la muerte"}});
          traitor->position = position; traitor->elevation = surface.elevation; traitor->supportId = surface.id; traitor->facing = direction;
          victim->position = target; victim->elevation = surface.elevation; victim->supportId = surface.id; victim->facing = direction;
          return;
        }
      }
      throw std::runtime_error("No hay borde físico libre para el cierre de traición");
    }

    void PlaceEnemyShowcase() {
      auto &world = const_cast<WorldState &>(World());
      Require(world.agents.size() == 2 && world.guards.size() >= 2, "Faltan actores para el disparo no letal");
      auto shooter = std::ranges::find_if(world.agents, [](const Agent &actor) { return actor.id != GameplayConfig::HostPlayerId; });
      auto host = std::ranges::find(world.agents, GameplayConfig::HostPlayerId, &Agent::id);
      for (float y = 740.f; y < 1100.f; y += 64.f) for (float x = 80.f; x < 1150.f; x += 64.f) {
        const std::array<sf::Vector2f, 4> points{{{x, y}, {x + 96.f, y}, {x + 210.f, y + 20.f}, {x + 210.f, y + 100.f}}};
        const auto support = WorldGeometry::FindSupport(world, points[0], 0.f, 5, GameplayConfig::AgentRadius);
        if (!support || support->elevation != 0.f) continue;
        bool valid = true;
        for (const auto point : points) {
          const auto floor = WorldGeometry::FindSupport(world, point, 0.f, support->id, GameplayConfig::AgentRadius);
          if (!floor || floor->elevation != 0.f || WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius, 0.f, CombatConfig::StandingHeight, floor->id)) valid = false;
        }
        if (!valid || WorldGeometry::RayOccluded(world, {points[0].x, points[0].y, 40.f}, {points[1].x, points[1].y, 40.f}) ||
            WorldGeometry::RayOccluded(world, {points[2].x, points[2].y, 40.f}, {points[1].x, points[1].y, 6.f}) ||
            WorldGeometry::RayOccluded(world, {points[3].x, points[3].y, 40.f}, {points[1].x, points[1].y, 6.f})) continue;
        shooter->position = points[0]; shooter->elevation = 0.f; shooter->supportId = support->id; shooter->facing = {1.f, 0.f};
        host->position = points[3]; host->elevation = 0.f; host->supportId = WorldGeometry::FindSupport(world, points[3], 0.f, support->id, GameplayConfig::AgentRadius)->id; host->facing = (points[1] - points[3]).normalized();
        for (std::size_t index = 0; index < 2; ++index) {
          auto &guard = world.guards[index]; const auto point = points[index + 1];
          guard.position = point; guard.elevation = 0.f; guard.supportId = WorldGeometry::FindSupport(world, point, 0.f, support->id, GameplayConfig::AgentRadius)->id;
          guard.facing = {1.f, 0.f}; guard.patrol = {point}; guard.patrolElevations = {0.f}; guard.waypoint = 0; guard.navigation.clear(); guard.patrolPauseRemaining = SecurityConfig::PatrolPauseMaximum;
        }
        const auto setup = Json{{"victim", world.guards[0].id}, {"observer", world.guards[1].id}, {"shooter", shooter->id}, {"position", {x, y}}, {"note", "Colocación para rodaje sobre geometría real; guardias vivos sin daño ni incapacitación impuesta"}};
        Case("enemy_nonlethal_shot_setup", setup); Mark("enemy-placed.json", setup); return;
      }
      throw std::runtime_error("No hay un sector libre para el disparo no letal en primera persona");
    }

    bool DriveEnemyShowcase() {
      if (options_.scenario != "fps-enemy" || !stage_.starts_with("enemy_")) return false;
      if (stage_ == "enemy_done") {
        if (Marker(Host() ? "client-enemy-finished.json" : "host-enemy-finished.json").is_object()) complete_ = true;
        return true;
      }
      Require(SessionState().IsConnected() && Local(), "El rodaje FPS perdió la sala LAN");
      Require(StageSeconds() < 25.f, "La toma FPS no progresa: " + stage_);
      const auto setup = Marker("enemy-placed.json");
      if (!setup.is_object()) return true;
      const auto targetId = setup.at("victim").get<std::uint32_t>();
      const auto target = std::ranges::find(World().guards, targetId, &Guard::id);
      const auto pulse = [&](sf::Keyboard::Scan key) { Access::Key(game_, context_, key, true); Access::Key(game_, context_, key, false); };
      if (stage_ == "enemy_setup_wait") {
        if (StageSeconds() < .4f) return true;
        if (Host()) Stage("enemy_host_watch");
        else {
          pulse(sf::Keyboard::Scan::Num2); Access::PerspectiveChord(game_, context_); Stage("enemy_fps_ready");
        }
      } else if (stage_ == "enemy_fps_ready" || stage_ == "enemy_fps_shoot") {
        Require(Access::FirstPerson(game_), "El disparo al enemigo perdió la vista FPS");
        if (target == World().guards.end()) return true;
        Access::LookAt(game_, *Local(), {target->position.x, target->position.y, target->elevation + CombatGeometry::HeadCenterHeight(Stance::Standing)});
        if (target->state == GuardState::Incapacitated) {
          Require(target->health > 0.f && Local()->selectedWeapon == WeaponKind::NonLethal, "El blanco no quedó dormido por el arma no letal");
          Case("fps_nonlethal_enemy_incapacitated", {{"guard", targetId}, {"health", target->health}, {"stun", target->stun}, {"firstPerson", true}});
          Capture("enemy-fps-nonlethal-ko"); Mark("client-enemy-ko.json"); pulse(sf::Keyboard::Scan::Z); Stage("enemy_client_bodywatch");
        } else if (Local()->selectedWeapon == WeaponKind::NonLethal && Local()->actionCooldown <= 0.f && StageSeconds() > 1.f) {
          if (Local()->nonLethalMagazine == 0) pulse(sf::Keyboard::Scan::R);
          else Access::Fire(context_);
          Stage("enemy_fps_shoot");
        }
      } else if (stage_ == "enemy_host_watch") {
        if (target != World().guards.end() && target->state == GuardState::Incapacitated && Marker("client-enemy-body-held.json").is_object()) {
          Require(std::ranges::any_of(World().events, [&](const MissionEvent &event) { return event.kind == EventKind::GuardIncapacitated && event.target == targetId && event.actor == setup.at("shooter").get<PlayerId>(); }), "La autoridad no registró la baja no letal del cliente");
          auto &world = const_cast<WorldState &>(World());
          auto shooter = std::ranges::find(world.agents, setup.at("shooter").get<PlayerId>(), &Agent::id);
          Require(shooter != world.agents.end(), "Falta el tirador para el corte de patrulla");
          const auto safe = WorldGeometry::FindSupport(world, {122.f, 758.f}, 0.f, 5, GameplayConfig::AgentRadius);
          Require(safe.has_value(), "El punto de espera del rodaje carece de suelo");
          shooter->position = {122.f, 758.f}; shooter->elevation = safe->elevation; shooter->supportId = safe->id;
          auto observer = std::ranges::find(world.guards, setup.at("observer").get<std::uint32_t>(), &Guard::id);
          Require(observer != world.guards.end(), "Falta la patrulla observadora");
          observer->facing = (target->position - observer->position).normalized();
          observer->patrolPauseRemaining = 0.f;
          Case("enemy_patrol_shot_setup", {{"note", "Corte de rodaje: el tirador vuelve al punto de espera y la patrulla gira al sector; descubrir el cuerpo depende de la IA"}, {"guard", observer->id}});
          Stage("enemy_host_bodywatch");
        }
      } else if (stage_ == "enemy_host_bodywatch") {
        if (std::ranges::any_of(World().events, [&](const MissionEvent &event) { return event.kind == EventKind::BodyFound && event.target == targetId; })) {
          Case("enemy_ai_found_unconscious_guard", {{"guard", targetId}, {"alarmCount", World().alarmCount}});
          Capture("enemy-body-found"); Mark("enemy-body-found.json"); Stage("enemy_found_hold");
        }
      } else if (stage_ == "enemy_client_bodywatch") {
        if (target != World().guards.end()) Access::LookAt(game_, *Local(), {target->position.x, target->position.y, target->elevation + 6.f});
        if (StageSeconds() > 2.f && !Marker("client-enemy-body-held.json").is_object()) { Capture("enemy-fps-body-held"); Mark("client-enemy-body-held.json"); }
        if (Marker("enemy-body-found.json").is_object()) { Capture("enemy-client-body-found"); Stage("enemy_found_hold"); }
      } else if (stage_ == "enemy_found_hold") {
        if (StageSeconds() > 3.f) { Mark(options_.role + "-enemy-finished.json"); Stage("enemy_done"); }
      }
      return true;
    }

    bool DriveShowcase() {
      if (!Showcase() || !stage_.starts_with("show_")) return false;
      if (stage_ == "show_done") {
        if (Marker(Host() ? "client-show-finished.json" : "host-show-finished.json").is_object()) complete_ = true;
        return true;
      }
      Require(SessionState().IsConnected() && Local(), "La grabación perdió la sala LAN");
      Require(StageSeconds() < 20.f, "El cierre no progresa en " + stage_);
      if (stage_ == "show_archive_setup") {
        if (Host()) {
          Require(World().alarmCount == 0, "La demostración activó una alarma antes del cierre de traición");
          PlaceShowcaseArchive(); Mark("show-archive-placed.json");
        }
        if (Marker("show-archive-placed.json").is_object()) Stage("show_archive_collect");
      } else if (stage_ == "show_archive_collect") {
        if (Local()->privateOrder == PrivateOrder::Traitor) {
          const auto camera = std::ranges::find(World().objects, 13u, &Interactable::id);
          if (camera != World().objects.end() && camera->active) { ActionCommand sabotage; sabotage.tool = true; SessionState().Submit(sabotage); }
        }
        if (StageSeconds() > 1.f && Local()->privateOrder != PrivateOrder::Traitor) { ActionCommand use; use.interact = true; SessionState().Submit(use); }
        if (World().objectives >= GetContract(World().contract).requiredObjectives) {
          Case("showcase_objective_collected_by_interaction", {{"objectives", World().objectives}, {"alarmCount", World().alarmCount}});
          Capture("show-objective-collected"); Mark(options_.role + "-show-objective.json"); Stage("show_archive_hold");
        }
      } else if (stage_ == "show_archive_hold") {
        if (StageSeconds() < 2.f || !Marker("host-show-objective.json").is_object() || !Marker("client-show-objective.json").is_object()) return true;
        if (Host()) { PlaceShowcaseBetrayal(); Mark("show-betrayal-placed.json"); }
        Stage("show_betrayal_wait");
      } else if (stage_ == "show_betrayal_wait") {
        if (Marker("show-betrayal-placed.json").is_object() && StageSeconds() > .5f) Stage("show_betrayal_push");
      } else if (stage_ == "show_betrayal_push") {
        if (Local()->privateOrder == PrivateOrder::Traitor && World().phase == GamePhase::Mission) { ActionCommand push; push.push = true; SessionState().Submit(push); }
        if (World().phase == GamePhase::Results) {
          const auto found = World().results.find(SessionState().GetLocalPlayerId());
          if (found == World().results.end()) return true;
          const bool traitor = Local()->privateOrder == PrivateOrder::Traitor;
          Require(found->second.traitorVictory == traitor && found->second.success == traitor && World().objectives >= 1 && World().alarmCount == 0, "El informe no confirma victoria real del traidor después del archivo");
          personalResult_ = {{"success", found->second.success}, {"traitorVictory", found->second.traitorVictory}, {"reason", found->second.reason}};
          Case("showcase_traitor_victory_after_objective", personalResult_); Capture("show-traitor-result");
          Mark(options_.role + "-show-results.json"); Stage("show_results_hold");
        }
      } else if (stage_ == "show_results_hold") {
        if (StageSeconds() > 5.f && Marker("host-show-results.json").is_object() && Marker("client-show-results.json").is_object()) {
          Mark(options_.role + "-show-finished.json"); Stage("show_done");
        }
      }
      return true;
    }

    bool DriveFirstPerson() {
      if (!stage_.starts_with("fps_")) return false;
      Require(StageSeconds() < 12.f, "No progresa el control FPS real en " + stage_);
      if (stage_ == "fps_finish") {
        if (Marker("client-fps-ack.json").is_object()) {
          if (Showcase()) { Mark("host-mechanics-base.json"); Stage("mechanics_base_barrier"); }
          else complete_ = true;
        }
        return true;
      }
      Require(SessionState().IsConnected() && IsTraining(World()) && Local(), "El caso FPS perdió su sala de entrenamiento real");
      const auto &actor = *Local();
      const auto shots = [&](EventKind kind) { return static_cast<std::size_t>(std::ranges::count(World().events, kind, &MissionEvent::kind)); };
      const auto pulse = [&](sf::Keyboard::Scan key) { Access::Key(game_, context_, key, true); Access::Key(game_, context_, key, false); };
      if (stage_ == "fps_base_barrier") {
        if (!Marker("host-fps-base.json").is_object() || !Marker("client-fps-base.json").is_object()) return true;
        Require(!Access::FirstPerson(game_), "La perspectiva inicial de una sala nueva no es táctica");
        baselinePosition_ = actor.position; fpsMagazine_ = actor.lethalMagazine; fpsAmmo_ = actor.loadout.lethalAmmo;
        if (Host()) { Capture("03-host-tactical"); Stage("fps_host_observe"); }
        else {
          fpsCamera_ = Access::Camera(game_); const auto equipment = actor.loadout;
          Access::PerspectiveChord(game_, context_);
          Require(Access::FirstPerson(game_) && actor.position == baselinePosition_ && actor.loadout.lethalAmmo == equipment.lethalAmmo && actor.lethalMagazine == fpsMagazine_,
            "Shift+F no abre la cámara FPS o modifica posición, munición o cargador");
          Case("client_shift_f_enters_without_world_mutation", {{"position", {actor.position.x, actor.position.y}}, {"magazine", actor.lethalMagazine}});
          Capture("03-client-first-person"); Mark("client-fps-entered.json"); Stage("fps_look");
        }
      } else if (stage_ == "fps_host_observe") {
        Require(!Access::FirstPerson(game_) && (actor.position - baselinePosition_).length() < .01f && actor.lethalMagazine == fpsMagazine_ && actor.loadout.lethalAmmo == fpsAmmo_,
          "La perspectiva del cliente cambia la cámara, posición o cargador del anfitrión");
        if (Marker("client-fps-entered.json").is_object() && !Marker("host-fps-independent.json").is_object()) {
          Case("host_keeps_independent_tactical_camera"); Capture("04-host-client-in-fps"); Mark("host-fps-independent.json");
        }
        if (const auto done = Marker("client-fps-done.json"); done.is_object()) {
          const auto *remote = AgentById(done.at("player").get<PlayerId>());
          Require(remote && remote->lethalMagazine == done.at("lethalMagazine").get<int>() && remote->nonLethalMagazine == done.at("nonLethalMagazine").get<int>() &&
            shots(EventKind::WeaponLethal) == 1 && shots(EventKind::WeaponNonLethal) == 1,
            "Los controles FPS no producen los dos disparos y cargadores únicos en el mundo del anfitrión");
          Case("host_authority_observes_real_fps_shots", {{"lethalShots", 1}, {"nonLethalShots", 1}, {"remotePosition", {remote->position.x, remote->position.y}}});
          Capture("09-host-authoritative-inventory"); Mark("host-fps-done.json"); Stage("fps_finish");
        }
      } else if (stage_ == "fps_look") {
        if (StageSeconds() < .3f || !Marker("host-fps-independent.json").is_object()) return true;
        fpsCamera_ = Access::Camera(game_); Access::Look(context_, {60.f, -20.f}); Stage("fps_look_check");
      } else if (stage_ == "fps_look_check") {
        if (StageSeconds() < .3f) return true;
        const auto camera = Access::Camera(game_); const sf::Vector2f forward{std::cos(camera.yaw), std::sin(camera.yaw)};
        Require(std::abs(camera.yaw - fpsCamera_.yaw) > .05f && std::abs(camera.pitch - fpsCamera_.pitch) > .01f &&
          actor.facing.dot(forward) > .99f && (actor.position - baselinePosition_).length() < .01f && actor.lethalMagazine == fpsMagazine_,
          "El ratón FPS no rota cámara y cuerpo autoritativo sin desplazar ni gastar equipo");
        Case("mouse_look_is_local_with_authoritative_body", {{"yaw", camera.yaw}, {"pitch", camera.pitch}, {"facing", {actor.facing.x, actor.facing.y}}});
        Access::Key(game_, context_, sf::Keyboard::Scan::W, true); Stage("fps_move");
      } else if (stage_ == "fps_move") {
        if (StageSeconds() < .5f) return true;
        Access::Key(game_, context_, sf::Keyboard::Scan::W, false);
        Require((actor.position - baselinePosition_).length() > 4.f, "W no mueve al cliente FPS mediante la simulación compartida");
        Case("fps_w_moves_the_existing_agent", {{"distance", (actor.position - baselinePosition_).length()}, {"elevation", actor.elevation}, {"support", actor.supportId}});
        Capture("05-client-fps-after-movement"); pulse(sf::Keyboard::Scan::Num1); Stage("fps_pistol_ready");
      } else if (stage_ == "fps_pistol_ready") {
        if (actor.selectedWeapon != WeaponKind::Pistol || actor.actionCooldown > 0.f || actor.reloadRemaining > 0.f || StageSeconds() < .3f) return true;
        fpsMagazine_ = actor.lethalMagazine; fpsWear_ = actor.pistolSuppressor; fpsShots_ = shots(EventKind::WeaponLethal);
        Require(fpsMagazine_ > 0, "El arma preparada no contiene munición física"); Access::Fire(context_); Stage("fps_pistol_shot");
      } else if (stage_ == "fps_pistol_shot") {
        if (actor.lethalMagazine != fpsMagazine_ - 1 || shots(EventKind::WeaponLethal) != fpsShots_ + 1) return true;
        Require(actor.pistolSuppressor == fpsWear_ - WeaponConfig::PistolSuppressorWear, "El disparo FPS ignora desgaste real del silenciador");
        Case("fps_mouse_fires_one_authoritative_pistol_round", {{"magazineBefore", fpsMagazine_}, {"magazineAfter", actor.lethalMagazine}, {"wearAfter", actor.pistolSuppressor}});
        Capture("06-client-fps-real-shot"); Stage("fps_pistol_no_repeat");
      } else if (stage_ == "fps_pistol_no_repeat") {
        if (StageSeconds() < .3f) return true;
        Require(actor.lethalMagazine == fpsMagazine_ - 1 && shots(EventKind::WeaponLethal) == fpsShots_ + 1, "Un clic FPS produce disparos repetidos");
        pulse(sf::Keyboard::Scan::R); Stage("fps_reload_begin");
      } else if (stage_ == "fps_reload_begin") {
        if (actor.reloadRemaining <= 0.f) return true;
        Case("fps_reload_uses_existing_weapon_timer", {{"remaining", actor.reloadRemaining}}); Stage("fps_reload_end");
      } else if (stage_ == "fps_reload_end") {
        if (actor.reloadRemaining > 0.f) return true;
        Require(actor.lethalMagazine == WeaponConfig::PistolCapacity, "R no rellena el cargador físico de la pistola FPS");
        Capture("07-client-fps-reloaded"); pulse(sf::Keyboard::Scan::Num2); Stage("fps_nonlethal_ready");
      } else if (stage_ == "fps_nonlethal_ready") {
        if (actor.selectedWeapon != WeaponKind::NonLethal || actor.actionCooldown > 0.f) return true;
        fpsMagazine_ = actor.nonLethalMagazine; fpsWear_ = actor.nonLethalSuppressor; fpsShots_ = shots(EventKind::WeaponNonLethal);
        Access::Fire(context_); Stage("fps_nonlethal_shot");
      } else if (stage_ == "fps_nonlethal_shot") {
        if (actor.nonLethalMagazine != fpsMagazine_ - 1 || shots(EventKind::WeaponNonLethal) != fpsShots_ + 1) return true;
        Require(actor.nonLethalSuppressor == fpsWear_ - WeaponConfig::NonLethalSuppressorWear, "El arma no letal FPS usa un desgaste distinto");
        Case("fps_selected_nonlethal_uses_existing_inventory", {{"magazine", actor.nonLethalMagazine}, {"wear", actor.nonLethalSuppressor}});
        Capture("08-client-fps-nonlethal"); baselinePosition_ = actor.position; Stage(Showcase() ? "fps_suppressor_remove" : "fps_exit");
      } else if (stage_ == "fps_suppressor_remove") {
        if (StageSeconds() < .5f) return true;
        pulse(sf::Keyboard::Scan::H); Stage("fps_suppressor_check");
      } else if (stage_ == "fps_suppressor_check") {
        if (actor.nonLethalSuppressorEquipped || StageSeconds() < 1.f) return true;
        Case("showcase_real_suppressor_removal"); Stage("fps_exit");
      } else if (stage_ == "fps_exit") {
        if (StageSeconds() < .3f) return true;
        baselinePosition_ = actor.position; const int magazine = actor.nonLethalMagazine;
        Access::PerspectiveChord(game_, context_);
        Require(!Access::FirstPerson(game_) && actor.position == baselinePosition_ && actor.nonLethalMagazine == magazine,
          "Volver de FPS a táctico teletransporta o cambia el cargador");
        Case("client_shift_f_returns_without_world_mutation"); Capture("09-client-tactical-restored");
        Stage("fps_client_wait");
      } else if (stage_ == "fps_client_wait") {
        Require(actor.cqcAttackTime <= 0.f && !actor.cqcPressPending && !actor.cqcHoldingGuard && !actor.cqcHoldingAgent && !actor.cqcInputDown,
          "El pulso Shift+F de salida con liberación en el mismo frame se filtra como CQC");
        if (StageSeconds() < .35f) return true;
        if (!Marker("client-fps-done.json").is_object()) Mark("client-fps-done.json", {{"player", actor.id}, {"lethalMagazine", actor.lethalMagazine}, {"nonLethalMagazine", actor.nonLethalMagazine}});
        if (Marker("host-fps-done.json").is_object()) {
          Mark("client-fps-ack.json");
          if (Showcase()) { Mark("client-mechanics-base.json"); Stage("mechanics_base_barrier"); }
          else complete_ = true;
        }
      } else throw std::runtime_error("Etapa FPS desconocida: " + stage_);
      return true;
    }

    void MechanicsEvidence(const std::string &phase) {
      if (Host()) {
        const auto current = phase + "/" + mechanics_.Stage();
        if (current == mechanicsPublishedStage_) return;
        mechanicsPublishedStage_ = current;
        auto file = "mechanics-" + current; std::replace(file.begin(), file.end(), '/', '-');
        Capture(file);
        mechanicsObservations_.push_back({{"phase", phase}, {"stage", mechanics_.Stage()}, {"tick", World().tick}, {"source", "authoritative"}});
        Mark("host-mechanics-stage.json", {{"phase", phase}, {"stage", mechanics_.Stage()}, {"tick", World().tick}});
      } else {
        const auto current = Marker("host-mechanics-stage.json");
        if (!current.is_object() || current.value("phase", "") != phase || World().tick <= current.value("tick", std::uint64_t{0})) return;
        const auto observed = phase + "/" + current.value("stage", "");
        if (observed == mechanicsObservedStage_) return;
        mechanicsObservedStage_ = observed;
        auto file = "mechanics-client-" + observed; std::replace(file.begin(), file.end(), '/', '-');
        Capture(file);
        mechanicsObservations_.push_back({{"phase", phase}, {"stage", current.value("stage", "")}, {"authoritativeTick", current.at("tick")}, {"localTick", World().tick}, {"state", State()}});
      }
    }

    bool DriveMechanics() {
      if (!stage_.starts_with("mechanics_")) return false;
      Require(SessionState().IsConnected() && Local(), "La prueba mecánica perdió su sesión LAN");
      if (stage_ == "mechanics_base_barrier") {
        if (!Marker("host-mechanics-base.json").is_object() || !Marker("client-mechanics-base.json").is_object()) return true;
        if (Host()) {
          if (options_.scenario == "pipe") mechanics_.BeginPipe(const_cast<WorldState &>(World()));
          else mechanics_.Begin(const_cast<WorldState &>(World()));
        }
        Stage("mechanics_training");
      } else if (stage_ == "mechanics_training" || stage_ == "mechanics_mission") {
        const std::string phase = stage_ == "mechanics_training" ? "training" : "mission";
        if (Host()) {
          SessionState().Submit(mechanics_.Update(const_cast<WorldState &>(World()), SessionState().GetLocalPlayerId(), context_.time.GetDeltaTime()));
          Require(!mechanics_.Failed(), mechanics_.Failure());
          MechanicsEvidence(phase);
          if (mechanics_.Done()) {
            const auto coverage = mechanics_.Coverage();
            if (options_.scenario == "table-tour") Require(std::ranges::any_of(coverage.at("cases"), [](const Json &entry) {
              return entry.value("case", std::string{}) == "mission_mesa_inspeccion" && entry.value("status", std::string{}) == "PASS";
            }), "La inspección de mesa quedó sin verificar: " + coverage.dump());
            if (options_.scenario == "pipe") Require(std::ranges::any_of(coverage.at("cases"), [](const Json &entry) {
              return entry.value("case", std::string{}) == "training_tubo_primera_entrada" && entry.value("status", std::string{}) == "PASS";
            }), "El primer agarre del tubo quedó sin verificar: " + coverage.dump());
            Case("authoritative_" + phase + "_mechanics_finished", coverage);
            Mark("host-mechanics-" + phase + "-done.json", {{"coverage", coverage}});
            Stage("mechanics_" + phase + "_finish");
          }
        } else {
          SessionState().Submit(mechanics_.ClientUpdate(World(), SessionState().GetLocalPlayerId())); MechanicsEvidence(phase);
          if (const auto completed = Marker("host-mechanics-" + phase + "-done.json"); completed.is_object()) {
            mechanicsRemoteCoverage_ = completed.at("coverage");
            Capture("mechanics-client-" + phase + "-finished");
            Case("client_received_" + phase + "_mechanics_completion", {{"localTick", World().tick}, {"source", "host_report_with_local_observations"}});
            Mark("client-mechanics-" + phase + "-ack.json"); Stage("mechanics_" + phase + "_finish");
          }
        }
      } else if (stage_ == "mechanics_training_finish") {
        if (!Marker("host-mechanics-training-done.json").is_object() || !Marker("client-mechanics-training-ack.json").is_object()) return true;
        if (options_.scenario == "training" || options_.scenario == "pipe") { Mark(options_.role + "-done.json"); Stage("done_barrier"); }
        else { Access::Lobby(game_, context_, window_); Stage("prepare"); }
      } else if (stage_ == "mechanics_mission_finish") {
        if (!Marker("host-mechanics-mission-done.json").is_object() || !Marker("client-mechanics-mission-ack.json").is_object()) return true;
        if (Showcase()) Stage("show_archive_setup");
        else { pilot_.EnableTour(); Stage("role_pilot"); }
      } else throw std::runtime_error("Etapa mecánica desconocida: " + stage_);
      return true;
    }

    void Drive() {
      if (DriveEnemyShowcase()) return;
      if (DriveShowcase()) return;
      if (DriveFirstPerson()) return;
      if (DriveMechanics()) return;
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
          if (options_.scenario == "betrayal" || options_.scenario == "fps-enemy") Stage("prepare");
          else if (options_.scenario == "fps" || Showcase()) {
            Click("RECORRER LA BASE"); Capture("02-base-tactical"); Mark(options_.role + "-fps-base.json"); Stage("fps_base_barrier");
          } else if (TrainingMechanicsScenario()) {
            Click("RECORRER LA BASE"); Capture("02-base-mechanics"); Mark(options_.role + "-mechanics-base.json"); Stage("mechanics_base_barrier");
          } else if (options_.scenario == "mission" || options_.scenario == "tour") {
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
          if (options_.scenario == "fps-enemy") {
            if (Host()) PlaceEnemyShowcase();
            Stage("enemy_setup_wait"); return;
          }
          if (MechanicsScenario()) {
            if (Host()) {
              if (Showcase()) mechanics_.BeginShowcaseMission(const_cast<WorldState &>(World()));
              else if (options_.scenario == "table-tour") mechanics_.BeginInspection(const_cast<WorldState &>(World()));
              else mechanics_.BeginMission(const_cast<WorldState &>(World()));
            }
            Stage("mechanics_mission");
          } else if (options_.scenario != "mission" && options_.scenario != "tour") {
            const bool dropTarget = (options_.scenario == "host-drop") == Host();
            if (dropTarget && !Host()) { baselinePosition_ = Local()->position; Stage("client_drop_command"); }
            else if (dropTarget) { Mark(options_.role + "-drop-ready.json", {{"cases", cases_}, {"evidence", evidence_}}); Stage("await_forced_drop"); }
            else { baselineTick_ = World().tick; Stage(Host() ? "client_loss_wait" : "host_loss_wait"); }
          } else if (Host()) Stage("client_pause_wait");
          else {
            const auto *host = AgentById(GameplayConfig::HostPlayerId);
            if (!host) { ActionCommand look; look.moveY = -1.f; SessionState().Submit(look); return; }
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
          Stage("role_pilot");
        }
      } else if (stage_ == "client_paused") {
        if (StageSeconds() >= 4.f) {
          const auto *host = AgentById(GameplayConfig::HostPlayerId);
          Require(host && World().tick > baselineTick_ && (host->position - baselinePosition_).length() > 5.f, "La pausa local detuvo el reloj remoto o al anfitrión");
          Require(Local() && (Local()->position - pausedLocalPosition_).length() < 0.1f, "La pausa local conserva movimiento del cliente");
          Case("local_pause_remote_world_continues", {{"ticksAdvanced", World().tick - baselineTick_}, {"hostDistance", (host->position - baselinePosition_).length()}, {"realSeconds", StageSeconds()}});
          Capture("08-client-pause-world-advanced"); Click("REANUDAR OPERACIÓN"); Mark("client-pause-finished.json"); Stage("role_pilot");
        }
      } else if (stage_ == "role_pilot") {
        if (World().phase == GamePhase::Results) { Stage("results"); return; }
        RequireMissionHealthy();
        const bool traitor = Local()->privateOrder == PrivateOrder::Traitor;
        SubmitPilot(!traitor, traitor || Marker("extraction-denied.json").is_object());
        CheckPilotProgress();
        if (World().objectives >= GetContract(World().contract).requiredObjectives && !objectiveSeen_) {
          objectiveSeen_ = true; Case("objective_collected_by_normal_interaction"); Capture("09-objective-collected"); Mark("objective-collected.json");
        }
        if (traitor && objectiveSeen_ && pilot_.Stage() == "interactuando_extraccion") {
          Require(!Local()->extracted, "El traidor logró extraerse mediante E");
          if (extractionDeniedAt_ < 0.f) extractionDeniedAt_ = Elapsed();
          if (Elapsed() - extractionDeniedAt_ >= 1.f && !Marker("extraction-denied.json").is_object()) {
            Case("local_traitor_extraction_rejected", {{"seconds", Elapsed() - extractionDeniedAt_}}); Capture("10-extraction-denied"); Mark("extraction-denied.json");
          }
        }
      } else if (stage_ == "results") {
        if (options_.scenario == "table-tour") {
          Require(pilot_.TourDone() && pilot_.CompletedCheckpoints().size() == 17, "La misión terminó antes de recorrer los 17 apoyos del caso mesa y nivel");
          Case("physical_tour_17_supports_finished", {{"checkpoints", pilot_.CompletedCheckpoints()}, {"visitedSupports", pilot_.VisitedSupports()}});
        }
        const auto found = World().results.find(SessionState().GetLocalPlayerId());
        Require(Local() && found != World().results.end(), "Falta el informe personal de la operación");
        const bool traitor = Local()->privateOrder == PrivateOrder::Traitor;
        Require(found->second.success == !traitor && Local()->extracted == !traitor && !found->second.traitorVictory &&
          (Host() || World().results.size() == 1), "El informe personal no respeta la extracción cooperativa, el rol local o la privacidad");
        personalResult_ = {{"success", found->second.success}, {"traitorVictory", found->second.traitorVictory}, {"extracted", Local()->extracted}, {"score", found->second.score}, {"rank", static_cast<int>(found->second.rank)}, {"reason", found->second.reason}};
        Case(traitor ? "local_traitor_private_loss_report" : "mission_success_personal_report", {{"score", found->second.score}, {"reason", found->second.reason}, {"seed", missionSeed_}});
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
          Require(SessionState().IsConnected() && Local() && World().tick > baselineTick_, "El anfitrión perdió su autoridad al cortarse el cliente");
          clientLossEndedMission_ = Local()->privateOrder == PrivateOrder::Traitor;
          if (clientLossEndedMission_) {
            const auto result = World().results.find(SessionState().GetLocalPlayerId());
            Require(World().phase == GamePhase::Results && result != World().results.end() && !result->second.success && !result->second.traitorVictory,
              "Quedar sólo el traidor tras una desconexión concede victoria o no produce su fracaso privado");
          } else Require(World().phase == GamePhase::Mission, "Un normal operativo deja de continuar tras el corte del cliente");
          const auto disconnected = std::ranges::find_if(World().agents, [](const Agent &agent) { return agent.id != GameplayConfig::HostPlayerId; });
          Require(disconnected != World().agents.end() && !disconnected->connected, "El cliente cortado permanece conectado en la simulación");
          baselinePosition_ = disconnected->position; baselineTick_ = World().tick;
          Capture("07-host-client-disconnected"); Stage("client_loss_stability");
        }
      } else if (stage_ == "client_loss_stability") {
        const auto disconnected = std::ranges::find_if(World().agents, [](const Agent &agent) { return agent.id != GameplayConfig::HostPlayerId; });
        Require(disconnected != World().agents.end() && !disconnected->connected && (disconnected->position - baselinePosition_).length() < 0.1f, "El mando remoto continúa desplazando al agente tras el corte");
        if (StageSeconds() >= 0.5f) {
          Require(SessionState().IsConnected() && (clientLossEndedMission_ ? World().phase == GamePhase::Results && World().tick == baselineTick_ :
            World().phase == GamePhase::Mission && World().tick > baselineTick_), "El corte remoto pierde la autoridad, altera el resultado o detiene al normal operativo");
          Require(!disconnected->moving && !disconnected->running, "La presentación remota conserva movimiento tras el corte");
          Case(clientLossEndedMission_ ? "abrupt_client_loss_local_traitor_private_failure" : "abrupt_client_loss_host_continues",
            {{"phase", static_cast<int>(World().phase)}, {"ticksAdvanced", World().tick - baselineTick_}, {"neutralizedActiveCommand", true}, {"stableSeconds", StageSeconds()}});
          Stage("rejoin_rejection_wait");
        }
      } else if (stage_ == "rejoin_rejection_wait") {
        if (Marker("rejoin-rejected.json").is_object()) {
          Require(World().phase == (clientLossEndedMission_ ? GamePhase::Results : GamePhase::Mission), "El rechazo de reingreso alteró la operación");
          Case(clientLossEndedMission_ ? "results_rejoin_rejected" : "mission_rejoin_rejected");
          if (clientLossEndedMission_) Click("REGRESAR A LA BASE");
          else { Access::Lobby(game_, context_, window_); Click("VOLVER A LA BASE"); }
          Stage("host_recovery_base");
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
    const auto isolatedSave = options.output / "runtime" / "Content" / "Save.json";
#ifdef _WIN32
    if (_wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", isolatedSave.c_str()) != 0) throw std::runtime_error("No se pudo aislar el guardado QA");
#else
    if (setenv("AWESOME_ENGINE_SAVE_FILE", isolatedSave.string().c_str(), 1) != 0) throw std::runtime_error("No se pudo aislar el guardado QA");
#endif
    return Runner(std::move(options)).Run();
  } catch (const std::exception &error) {
    std::cerr << "FAIL QA bootstrap: " << error.what() << '\n';
    return 1;
  }
}
