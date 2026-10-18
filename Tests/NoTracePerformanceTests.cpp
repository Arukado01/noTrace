#include <windows.h>
#include <psapi.h>
#undef near
#undef far
#include "NoTraceGame.h"
#include "NoTracePerformance.h"
#include "Core/EngineConfig.h"
#include <SFML/GpuPreference.hpp>
#include <SFML/OpenGL.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>

SFML_DEFINE_DISCRETE_GPU_PREFERENCE

namespace EngineTests {
  struct GuiAccess {
    static void Initialize(EngineContext &context) {
      context.input.hardwarePolling_ = false;
      context.window.RefreshPresentation();
      context.gui.SyncPresentation(context.window.GetLogicalSize(), context.window.GetViewport());
    }
    static void Time(EngineContext &context, bool realTime) {
      context.time.Update();
      if (!realTime) context.time.deltaTime_ = sf::seconds(1.f / 60.f);
    }
    static void Deploy(NoTrace::Game &game) {
      game.session_.StartSolo("Perfil"); game.session_.SetReady(true); game.session_.Deploy();
      NoTrace::Simulation fixture; fixture.ResetBase({{1, "Perfil"}}); fixture.Deploy(119003);
      const_cast<NoTrace::WorldState &>(game.session_.GetWorld()) = fixture.GetState();
      game.previousPhase_ = NoTrace::GamePhase::Mission; game.ClosePanel(); game.RefreshHud();
    }
    static NoTrace::WorldState &World(NoTrace::Game &game) { return const_cast<NoTrace::WorldState &>(game.session_.GetWorld()); }
    static void Perspective(NoTrace::Game &game, bool firstPerson) {
      if (firstPerson) {
        if (!game.ToggleFirstPerson()) throw std::runtime_error("No se pudo entrar en primera persona");
        game.firstPersonCamera_.yaw = .7f;
      } else game.LeaveFirstPerson();
    }
    static void Move(EngineContext &context, NoTrace::Game &game, unsigned int frame) {
      constexpr std::array keys{sf::Keyboard::Scan::D, sf::Keyboard::Scan::S, sf::Keyboard::Scan::A, sf::Keyboard::Scan::W};
      if (frame % 60 == 0) {
        for (const auto scan : keys) { sf::Event::KeyReleased event; event.scancode = scan; context.input.CaptureEvent(event); }
        sf::Event::KeyPressed event; event.scancode = keys[(frame / 60) % keys.size()]; context.input.CaptureEvent(event);
      }
      if (game.firstPerson_) game.firstPersonCamera_.yaw += .0025f;
    }
    static void Scenario(NoTrace::Game &game, const std::string &scenario) {
      auto &world = World(game); auto &agent = world.agents.front();
      if (scenario == "warehouse") { agent.position = {400.f, 810.f}; agent.elevation = 0.f; agent.supportId = 5; }
      else if (scenario == "multilevel") { agent.position = {380.f, 360.f}; agent.elevation = 0.f; agent.supportId = 1; }
      else if (scenario == "camera10") { agent.position = {610.f, 240.f}; agent.elevation = 96.f; agent.supportId = 10; }
      else if (scenario == "top23") { agent.position = {850.f, 340.f}; agent.elevation = 192.f; agent.supportId = 23; }
      else if (scenario != "spawn") throw std::runtime_error("Escenario de perfil desconocido");
      const auto desired = agent.position;
      bool found = false;
      for (const float radius : {0.f, 24.f, 48.f}) {
        for (const float y : {0.f, -1.f, 1.f}) for (const float x : {0.f, -1.f, 1.f}) {
          const auto candidate = desired + sf::Vector2f{x, y} * radius;
          if (!NoTrace::WorldGeometry::FindSupport(world, candidate, agent.elevation, agent.supportId, NoTrace::GameplayConfig::AgentRadius) ||
              NoTrace::WorldGeometry::PositionBlocked(world, candidate, NoTrace::GameplayConfig::AgentRadius, agent.elevation, NoTrace::CombatConfig::StandingHeight, agent.supportId)) continue;
          agent.position = candidate; found = true; break;
        }
        if (found) break;
      }
      if (!found) throw std::runtime_error("No hay una posición física libre para el escenario de perfil");
      if (!NoTrace::WorldGeometry::FindSupport(world, agent.position, agent.elevation, agent.supportId, NoTrace::GameplayConfig::AgentRadius) ||
          NoTrace::WorldGeometry::PositionBlocked(world, agent.position, NoTrace::GameplayConfig::AgentRadius, agent.elevation, NoTrace::CombatConfig::StandingHeight, agent.supportId)) throw std::runtime_error("La posición inicial del perfil no tiene apoyo libre válido");
    }
    static void Stimulus(NoTrace::Game &game, const std::string &scenario) {
      if (scenario != "warehouse" && scenario != "multilevel") return;
      auto &agent = World(game).agents.front(); agent.pistolSuppressorEquipped = false;
      NoTrace::ActionCommand shot; shot.fireLethal = true; shot.aimX = 0.f; shot.aimY = 1.f;
      game.session_.Submit(shot);
    }
    static void Begin(EngineContext &context) { context.renderer.BeginDrawing(); }
    static const sf::Texture &Finish(EngineContext &context) { return context.renderer.FinishDrawing(); }
    static void Gui(EngineContext &context) { context.gui.Render(); }
  };
}

namespace {
  using Clock = std::chrono::steady_clock;
  using Json = nlohmann::json;
  using Access = EngineTests::GuiAccess;
  double Milliseconds(Clock::time_point from) { return std::chrono::duration<double, std::milli>(Clock::now() - from).count(); }
  Json Distribution(std::vector<double> values) {
    if (values.empty()) return Json::object();
    std::sort(values.begin(), values.end());
    const double mean = std::accumulate(values.begin(), values.end(), 0.) / values.size();
    const auto quantile = [&](double fraction) { return values[std::min(values.size() - 1, static_cast<std::size_t>(fraction * (values.size() - 1)))]; };
    return {{"meanMs", mean}, {"minMs", values.front()}, {"p50Ms", quantile(.5)}, {"p95Ms", quantile(.95)}, {"p99Ms", quantile(.99)}, {"maxMs", values.back()}, {"averageFps", 1000. / mean}, {"minimumFps", 1000. / values.back()}};
  }
  Json Memory() {
    PROCESS_MEMORY_COUNTERS_EX value{}; value.cb = sizeof(value);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&value), sizeof(value))) throw std::runtime_error("No se pudo medir memoria del proceso");
    return {{"workingSetBytes", value.WorkingSetSize}, {"peakWorkingSetBytes", value.PeakWorkingSetSize}, {"privateBytes", value.PrivateUsage}};
  }
  double CpuMilliseconds() {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) throw std::runtime_error("No se pudo medir CPU del proceso");
    const auto ticks = [](FILETIME time) { ULARGE_INTEGER value{}; value.LowPart = time.dwLowDateTime; value.HighPart = time.dwHighDateTime; return value.QuadPart; };
    return static_cast<double>(ticks(kernel) + ticks(user)) / 10000.;
  }
  void Save(const std::filesystem::path &path, const Json &value) { std::ofstream(path, std::ios::binary) << value.dump(2) << '\n'; }
  void Sandbox(const std::filesystem::path &root, const std::filesystem::path &output) {
    const auto runtime = output / "runtime";
    if (std::filesystem::exists(runtime)) throw std::runtime_error("La medición exige un directorio de salida nuevo");
    std::filesystem::create_directories(runtime / "Content");
    for (auto it = std::filesystem::recursive_directory_iterator(root / "Content"); it != std::filesystem::recursive_directory_iterator(); ++it) {
      const auto relative = std::filesystem::relative(it->path(), root / "Content");
      const auto first = relative.begin()->string();
      if (first == "Sources" || first == "Screenshots") { if (it->is_directory()) it.disable_recursion_pending(); continue; }
      if (!it->is_regular_file() || relative == "Save.json") continue;
      const auto target = runtime / "Content" / relative;
      std::filesystem::create_directories(target.parent_path()); std::filesystem::copy_file(it->path(), target);
    }
    std::filesystem::current_path(runtime);
  }
  void Stress(NoTrace::WorldState &world) {
    const auto original = world.guards;
    std::uint32_t nextId = 1;
    for (const auto &guard : original) nextId = std::max(nextId, guard.id + 1);
    for (const auto &source : original) {
      auto guard = source; guard.id = nextId++;
      for (const auto offset : {sf::Vector2f{24.f, 0.f}, {-24.f, 0.f}, {0.f, 24.f}, {0.f, -24.f}}) {
        const auto candidate = source.position + offset;
        if (NoTrace::WorldGeometry::PositionBlocked(world, candidate, NoTrace::GameplayConfig::AgentRadius, source.elevation, NoTrace::CombatConfig::StandingHeight, source.supportId, 0)) continue;
        if (std::ranges::any_of(world.guards, [&](const auto &other) { return (other.position - candidate).length() < NoTrace::GameplayConfig::AgentRadius * 2.f && std::abs(other.elevation - source.elevation) < 1.f; })) continue;
        guard.position = candidate; world.guards.push_back(guard); break;
      }
    }
    const auto objects = world.objects.size();
    std::uint32_t objectId = 1;
    for (const auto &object : world.objects) objectId = std::max(objectId, object.id + 1);
    for (std::size_t index = 0; index < objects; ++index) {
      NoTrace::Interactable object; object.id = objectId++; object.kind = NoTrace::ObjectKind::Hideout; object.propStyle = NoTrace::PropStyle::Logistics;
      object.position = {80.f + static_cast<float>(index % 12) * 112.f, 1010.f + static_cast<float>(index / 12) * 25.f};
      object.footprint = {18.f, 18.f}; object.interactive = false; object.decorative = true;
      object.solid = NoTrace::SpatialOverride::Disabled; object.blocksVision = NoTrace::SpatialOverride::Disabled;
      world.objects.push_back(object);
    }
  }
}

int main(int argc, char **argv) {
  try {
    std::filesystem::path output;
    unsigned int frames = 180, warmup = 24, width = 1280, height = 720;
    bool firstPerson = false, stress = false, profile = false, realTime = false, gpuFence = false, moving = false;
    std::string scenario = "spawn";
    for (int index = 1; index < argc; ++index) {
      const std::string argument = argv[index];
      if (argument == "--output" && index + 1 < argc) output = std::filesystem::absolute(argv[++index]);
      else if (argument == "--frames" && index + 1 < argc) frames = std::stoul(argv[++index]);
      else if (argument == "--warmup" && index + 1 < argc) warmup = std::stoul(argv[++index]);
      else if (argument == "--width" && index + 1 < argc) width = std::stoul(argv[++index]);
      else if (argument == "--height" && index + 1 < argc) height = std::stoul(argv[++index]);
      else if (argument == "--fps") firstPerson = true;
      else if (argument == "--stress") stress = true;
      else if (argument == "--profile") profile = true;
      else if (argument == "--real-time") realTime = true;
      else if (argument == "--gpu-fence") gpuFence = true;
      else if (argument == "--moving") moving = true;
      else if (argument == "--scenario" && index + 1 < argc) scenario = argv[++index];
      else throw std::runtime_error("Argumento de perfil no válido: " + argument);
    }
    if (output.empty() || !frames || frames > 3600 || width < 640 || height < 360) throw std::runtime_error("Parámetros de perfil fuera de límites");
    const auto repository = std::filesystem::current_path();
    std::filesystem::create_directories(output); Sandbox(repository, output);
    const auto isolatedSave = output / "runtime/Content/Save.json";
    if (_wputenv_s(L"AWESOME_ENGINE_SAVE_FILE", isolatedSave.c_str()) != 0) throw std::runtime_error("No se pudo aislar el guardado del perfil");
    sf::RenderWindow window(sf::VideoMode({width, height}), "NO TRACE / medición de rendimiento");
    EngineContext context(window); Access::Initialize(context);
    NoTrace::Game game(context); game.Start(); Access::Deploy(game);
    Access::Scenario(game, scenario);
    auto &world = Access::World(game);
    if (stress) Stress(world);
    Access::Perspective(game, firstPerson);
    window.setVerticalSyncEnabled(false); window.setFramerateLimit(0);
    const auto glText = [](unsigned int field) { const auto *value = glGetString(field); return value ? std::string(reinterpret_cast<const char *>(value)) : std::string("desconocido"); };
#if defined(NDEBUG)
    constexpr auto configuration = "Release";
#else
    constexpr auto configuration = "Debug";
#endif
    Json result{{"status", "RUNNING"}, {"configuration", configuration}, {"seed", world.seed}, {"firstPerson", firstPerson}, {"stress", stress}, {"moving", moving}, {"instrumentation", profile}, {"fixedSimulationDelta", !realTime}, {"gpuFence", gpuFence}, {"frames", frames}, {"warmup", warmup}, {"nativeSize", {width, height}}, {"logicalSize", {gConfig.windowSize.x, gConfig.windowSize.y}}, {"guards", world.guards.size()}, {"objects", world.objects.size()}, {"obstacles", world.obstacles.size()}, {"surfaces", world.walkSurfaces.size()}, {"glVendor", glText(GL_VENDOR)}, {"glRenderer", glText(GL_RENDERER)}, {"glVersion", glText(GL_VERSION)}, {"memoryLoaded", Memory()}};
    result["scenario"] = scenario;
    result["initialPosition"] = {world.agents.front().position.x, world.agents.front().position.y, world.agents.front().elevation};
    Save(output / "progress.json", result);
    std::vector<double> frameTimes, updateTimes, renderTimes, presentTimes, deltaTimes;
    double cpuStart = 0.; Clock::time_point measuredStart;
    for (unsigned int index = 0; index < warmup + frames; ++index) {
      if (index == warmup) { NoTrace::Profiling::Reset(); NoTrace::Profiling::enabled = profile; cpuStart = CpuMilliseconds(); measuredStart = Clock::now(); }
      const auto started = Clock::now();
      while (const auto event = window.pollEvent()) if (event->is<sf::Event::Closed>()) throw std::runtime_error("Medición cancelada desde la ventana");
      context.input.BeginFrame(); Access::Time(context, realTime);
      if (moving) Access::Move(context, game, index);
      const auto updateStarted = Clock::now();
      game.Update();
      if (index == warmup) Access::Stimulus(game, scenario);
      game.UpdateBackground();
      const double updateTime = Milliseconds(updateStarted);
      if (world.phase != NoTrace::GamePhase::Mission || !world.agents.front().alive || world.agents.front().incapacitated) throw std::runtime_error("La misión dejó de ser activa durante el perfil");
      const auto renderStarted = Clock::now();
      window.setView(context.window.GetPresentationView()); window.clear(); Access::Begin(context); game.Render();
      window.draw(sf::Sprite(Access::Finish(context))); Access::Gui(context);
      if (gpuFence) glFinish();
      const double renderTime = Milliseconds(renderStarted);
      const auto presentStarted = Clock::now(); window.display(); const double presentTime = Milliseconds(presentStarted);
      if (index >= warmup) {
        frameTimes.push_back(Milliseconds(started)); updateTimes.push_back(updateTime); renderTimes.push_back(renderTime); presentTimes.push_back(presentTime); deltaTimes.push_back(context.time.GetDeltaTime() * 1000.);
      }
      if (index % 30 == 0) { result["completedFrames"] = index; Save(output / "progress.json", result); }
    }
    NoTrace::Profiling::enabled = false;
    result["wallSeconds"] = Milliseconds(measuredStart) / 1000.; result["cpuMilliseconds"] = CpuMilliseconds() - cpuStart;
    result["cpuCoreEquivalent"] = result["cpuMilliseconds"].get<double>() / (result["wallSeconds"].get<double>() * 1000.);
    result["frame"] = Distribution(frameTimes); result["update"] = Distribution(updateTimes); result["renderSubmission"] = Distribution(renderTimes); result["present"] = Distribution(presentTimes); result["simulationDelta"] = Distribution(deltaTimes);
    result["memoryFinal"] = Memory(); result["worldTick"] = world.tick; result["elapsedSimulation"] = world.elapsed;
    result["alarm"] = world.alarm; result["health"] = world.agents.front().health; result["magazine"] = world.agents.front().lethalMagazine;
    for (const auto &guard : world.guards) result["guardStates"].push_back({{"id", guard.id}, {"state", static_cast<int>(guard.state)}, {"position", {guard.position.x, guard.position.y}}, {"elevation", guard.elevation}, {"support", guard.supportId}, {"lastKnownElevation", guard.lastKnownElevation}});
    for (const auto &event : world.events) result["events"].push_back({{"kind", static_cast<int>(event.kind)}, {"actor", event.actor}, {"target", event.target}});
    result["profileScope"] = "Tiempos inclusivos de CPU; no sumar padre e hijos. FPS completos medidos con instrumentación desactivada; gpuFence es diagnóstico separado. Sin porcentaje ni VRAM GPU medidos.";
    for (std::size_t index = 0; index < NoTrace::Profiling::StageCount; ++index) {
      const auto stage = static_cast<NoTrace::Profiling::Stage>(index); const auto &counter = NoTrace::Profiling::Get(stage);
      result["stages"][std::string(NoTrace::Profiling::Name(stage))] = {{"milliseconds", counter.milliseconds}, {"calls", counter.calls}, {"perFrameMs", counter.milliseconds / frames}};
    }
    sf::Texture capture(window.getSize()); capture.update(window);
    if (!capture.copyToImage().saveToFile(output / "frame.png")) throw std::runtime_error("No se pudo guardar la captura del perfil");
    result["status"] = "PASS"; Save(output / "result.json", result);
    game.OnCleanup(); window.close(); std::filesystem::current_path(repository);
    std::cout << result.dump(2) << '\n'; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL PERF: " << error.what() << '\n'; return 1; }
}
