#include "NoTraceSimulation.h"
#include "Config/NoTraceGameplayConfig.h"
#include <cmath>
#include <iostream>
#include <queue>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }
  void Load(Simulation &simulation, const nlohmann::json &snapshot) {
    std::string error;
    if (!simulation.LoadSnapshot(snapshot, error)) throw std::runtime_error("Fixture: " + error);
  }
  nlohmann::json EmptyMission(Simulation &simulation, sf::Vector2f position = {100.f, 600.f}) {
    simulation.ResetBase(); simulation.Deploy(0);
    auto snapshot = simulation.SnapshotFor(1);
    snapshot["obstacles"] = nlohmann::json::array();
    snapshot["guards"] = nlohmann::json::array();
    snapshot["objects"] = nlohmann::json::array();
    snapshot["agents"][0]["position"] = {position.x, position.y};
    return snapshot;
  }
  void Advance(Simulation &simulation, ActionCommand command, int frames, float dt = 0.1f) {
    for (int index = 0; index < frames; ++index) simulation.Step(dt, {{1, command}});
  }
  nlohmann::json GuardJson(Simulation &simulation, sf::Vector2f position, sf::Vector2f facing) {
    simulation.ResetBase(); simulation.Deploy(0);
    auto value = simulation.SnapshotFor(1)["guards"][0];
    value["position"] = {position.x, position.y}; value["facing"] = {facing.x, facing.y};
    value["patrol"] = nlohmann::json::array(); value["waypoint"] = 0;
    return value;
  }

  void MovementAndBase() {
    Simulation simulation;
    simulation.ResetBase();
    Require(simulation.GetState().objects.size() == 4, "La base necesita cuatro estaciones");
    Require(simulation.AddPlayer(2, "Segundo"), "Debe admitir otro agente en base");
    Require(!simulation.AddPlayer(2, "Duplicado"), "Debe rechazar identificadores duplicados");
    simulation.DisconnectPlayer(2);
    Require(simulation.AddPlayer(2, "Reconectado"), "Debe admitir reconexión en base");
    simulation.ResetBase();
    const auto start = simulation.GetState().agents[0].position;
    ActionCommand move; move.moveX = 1.f;
    Advance(simulation, move, 10);
    Require(std::abs(simulation.GetState().agents[0].position.x - start.x - GameplayConfig::WalkSpeed) < 0.1f, "Caminar debe usar segundos");
    simulation.ResetBase();
    move.moveY = 1.f;
    Advance(simulation, move, 10);
    Require(std::abs((simulation.GetState().agents[0].position - start).length() - GameplayConfig::WalkSpeed) < 0.1f, "La diagonal no debe acelerar");
    simulation.ResetBase();
    ActionCommand prone; prone.prone = true;
    simulation.Step(0.1f, {{1, prone}});
    move.moveY = 0.f;
    Advance(simulation, move, 10);
    Require(simulation.GetState().agents[0].position == start, "Tumbarse no equivale a arrastrarse");
    move.crawl = true;
    Advance(simulation, move, 10);
    Require(std::abs(simulation.GetState().agents[0].position.x - start.x - GameplayConfig::CrawlSpeed) < 0.1f, "Arrastrarse requiere la acción mantenida");
    simulation.ResetBase();
    ActionCommand crouch; crouch.crouch = true;
    simulation.Step(0.1f, {{1, crouch}});
    move.crawl = false;
    Advance(simulation, move, 10);
    Require(std::abs(simulation.GetState().agents[0].position.x - start.x - GameplayConfig::CrouchSpeed) < 0.1f, "Agacharse debe cambiar la velocidad");
    simulation.ResetBase();
    move.run = true;
    Advance(simulation, move, 10);
    Require(std::abs(simulation.GetState().agents[0].position.x - start.x - GameplayConfig::RunSpeed) < 0.1f, "Correr debe cambiar la velocidad");

    auto snapshot = EmptyMission(simulation, {100.f, 100.f});
    snapshot["obstacles"].push_back({{"position", {150.f, 50.f}}, {"size", {20.f, 200.f}}, {"cover", 2}, {"solid", true}, {"name", "Pared"}});
    Load(simulation, snapshot);
    Advance(simulation, move, 40);
    Require(simulation.GetState().agents[0].position.x <= 138.01f, "El agente no debe atravesar una pared al correr");
    Require(simulation.GetState().agents[0].position.x > 130.f, "La pared no debe bloquear a distancia artificial");
    auto invalid = snapshot;
    invalid["agents"][0]["position"] = {"inválido", 100.f};
    const auto position = simulation.GetState().agents[0].position;
    std::string error;
    Require(!simulation.LoadSnapshot(invalid, error) && !error.empty(), "El parser debe rechazar coordenadas malformadas");
    Require(simulation.GetState().agents[0].position == position, "Un paquete inválido no debe alterar el mundo");
  }

  void PerceptionAndNoise() {
    Simulation simulation, donor;
    auto snapshot = EmptyMission(simulation, {150.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {300.f, 500.f}, {1.f, 0.f}));
    Load(simulation, snapshot);
    ActionCommand decoy; decoy.throwDecoy = true; decoy.aimX = 1.f;
    simulation.Step(0.1f, {{1, decoy}});
    Require(simulation.GetState().guards[0].state == GuardState::Investigate, "El ruido debe provocar investigación");
    Require((simulation.GetState().guards[0].lastKnown - sf::Vector2f{320.f, 500.f}).length() < 0.01f, "El guardia debe investigar el señuelo, no conocer al agente");
    Require(!simulation.GetState().alarm, "Un ruido sin visión no activa alarma de agente");
    Require(std::ranges::none_of(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection; }), "Investigación no equivale a detección");

    snapshot = EmptyMission(simulation, {220.f, 100.f});
    Load(simulation, snapshot);
    Agent target = simulation.GetState().agents[0];
    Require(simulation.CanSee({100.f, 100.f}, {1.f, 0.f}, target), "Debe ver un agente expuesto dentro del cono");
    Require(!simulation.CanSee({100.f, 100.f}, {-1.f, 0.f}, target), "No debe ver detrás de su orientación");
    snapshot["obstacles"].push_back({{"position", {175.f, 80.f}}, {"size", {20.f, 40.f}}, {"cover", 2}, {"solid", true}, {"name", "Muro"}});
    Load(simulation, snapshot);
    Require(!simulation.CanSee({100.f, 100.f}, {1.f, 0.f}, target), "La cobertura alta bloquea visión de pie");
    snapshot["obstacles"][0]["cover"] = 1;
    Load(simulation, snapshot);
    Require(simulation.CanSee({100.f, 100.f}, {1.f, 0.f}, target), "La cobertura baja no oculta a un agente de pie");
    target.stance = Stance::Crouched;
    Require(!simulation.CanSee({100.f, 100.f}, {1.f, 0.f}, target), "La cobertura baja protege al agente agachado");
    target.hidden = true;
    Require(!simulation.CanSee({210.f, 100.f}, {1.f, 0.f}, target), "Un escondite autorizado oculta al agente");

    snapshot = EmptyMission(simulation, {400.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {300.f, 500.f}, {1.f, 0.f}));
    Load(simulation, snapshot);
    Advance(simulation, {}, 9);
    Require(simulation.GetState().alarm, "La visión sostenida debe activar alarma");
    Require(std::ranges::any_of(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection; }), "La detección confirmada debe quedar registrada");
    auto escaped = simulation.SnapshotFor(1);
    escaped["agents"][0]["position"] = {800.f, 700.f};
    Load(simulation, escaped);
    simulation.Step(0.1f, {});
    Require(simulation.GetState().guards[0].state == GuardState::Search, "Al romper visión debe buscar la última posición conocida");
    Require((simulation.GetState().guards[0].lastKnown - sf::Vector2f{400.f, 500.f}).length() < 0.01f, "La búsqueda no debe seguir la posición oculta actual");

    snapshot = EmptyMission(simulation, {200.f, 200.f});
    Load(simulation, snapshot);
    ActionCommand knock; knock.knock = true;
    simulation.Step(0.1f, {{1, knock}});
    Require(simulation.GetState().noises.empty(), "No debe golpear una pared inexistente");
    snapshot["obstacles"].push_back({{"position", {220.f, 150.f}}, {"size", {20.f, 100.f}}, {"cover", 2}, {"solid", true}, {"name", "Pared"}});
    Load(simulation, snapshot);
    simulation.Step(0.1f, {{1, knock}});
    Require(!simulation.GetState().noises.empty(), "Golpear una pared cercana debe producir un ruido real");

    snapshot = EmptyMission(simulation, {400.f, 500.f});
    snapshot["objects"].push_back({{"id", 90}, {"kind", static_cast<int>(ObjectKind::Camera)}, {"position", {300.f, 500.f}}, {"name", "Cámara"}, {"active", true}, {"completed", false}, {"open", false}, {"progress", 0.f}, {"facing", {1.f, 0.f}}});
    Load(simulation, snapshot);
    Advance(simulation, {}, 250);
    Require(simulation.GetState().alarm, "La visión continua debe mantener la alarma más de veinte segundos");
    snapshot = simulation.SnapshotFor(1); snapshot["agents"][0]["hidden"] = true;
    Load(simulation, snapshot);
    Advance(simulation, {}, 210);
    Require(!simulation.GetState().alarm, "Al perder la visión la alarma debe terminar después de la búsqueda");

    snapshot = EmptyMission(simulation, {900.f, 700.f});
    snapshot["guards"].push_back(GuardJson(donor, {150.f, 200.f}, {1.f, 0.f}));
    snapshot["obstacles"].push_back({{"position", {190.f, 180.f}}, {"size", {20.f, 60.f}}, {"cover", 2}, {"solid", true}, {"name", "Muro entre guardia y ruido"}});
    snapshot["noises"].push_back({{"position", {250.f, 200.f}}, {"radius", 300.f}, {"remaining", 0.6f}, {"actor", 1}});
    Load(simulation, snapshot);
    for (int frame = 0; frame < 40; ++frame) {
      simulation.Step(0.1f, {});
      Require(!sf::FloatRect{{178.f, 168.f}, {44.f, 84.f}}.contains(simulation.GetState().guards[0].position), "La navegación no debe atravesar el muro");
    }
    Require((simulation.GetState().guards[0].position - sf::Vector2f{250.f, 200.f}).length() < 20.f, "El guardia debe rodear el muro para investigar el ruido");
  }

  void CombatAndTools() {
    Simulation simulation, donor;
    auto snapshot = EmptyMission(simulation, {300.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {330.f, 500.f}, {1.f, 0.f}));
    Load(simulation, snapshot);
    ActionCommand cqc; cqc.cqc = true;
    const std::array<CqcState, 3> stages{CqcState::Grabbed, CqcState::KnockedDown, CqcState::Restrained};
    for (auto stage : stages) {
      simulation.Step(0.1f, {{1, cqc}});
      Require(simulation.GetState().guards[0].cqcState == stage, "CQC debe distinguir agarre, derribo e inmovilización");
      Advance(simulation, {}, 5);
    }
    simulation.Step(0.1f, {{1, cqc}});
    Require(simulation.GetState().guards[0].state == GuardState::Incapacitated, "CQC debe incapacitar sin matar");
    ActionCommand drag; drag.dragBody = true;
    simulation.Step(0.1f, {{1, drag}});
    Require(simulation.GetState().agents[0].draggingGuard == 1, "Debe permitir arrastrar un cuerpo cercano");
    auto hide = simulation.SnapshotFor(1);
    hide["objects"].push_back({{"id", 90}, {"kind", static_cast<int>(ObjectKind::Hideout)}, {"position", {300.f, 500.f}}, {"name", "Escondite"}, {"active", true}, {"completed", false}, {"open", false}, {"progress", 0.f}, {"facing", {1.f, 0.f}}});
    Load(simulation, hide);
    ActionCommand interact; interact.interact = true;
    simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().guards[0].bodyHidden, "Debe poder ocultar un cuerpo en escondite válido");

    snapshot = EmptyMission(simulation, {300.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {400.f, 500.f}, {1.f, 0.f}));
    Load(simulation, snapshot);
    ActionCommand shoot; shoot.fireNonLethal = true; shoot.aimX = 1.f;
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().guards[0].state == GuardState::Incapacitated, "El arma no letal debe incapacitar");
    Load(simulation, snapshot);
    shoot.fireNonLethal = false; shoot.fireLethal = true;
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().guards[0].state == GuardState::Dead, "El arma letal debe eliminar");
    Require(simulation.GetState().agents[0].loadout.lethalAmmo == 3, "Disparar debe consumir munición");
  }

  void ScoringAndObjectives() {
    WorldState world;
    Agent agent; agent.id = 1; agent.extracted = true;
    world.agents.push_back(agent); world.objectives = 1; world.secondary = 1;
    const auto &contract = GetContract(ContractId::GhostArchive);
    world.events.push_back({1, 1, 1, EventKind::Noise, 0});
    auto result = EvaluateMission(contract, world, 1, true);
    Require(result.rank == Rank::Maestro && result.maestroEligible, "Una distracción limpia permite Maestro");
    for (const auto kind : {EventKind::WeaponLethal, EventKind::WeaponNonLethal, EventKind::Cqc, EventKind::ConfirmedDetection, EventKind::AgentAlarm}) {
      world.events.push_back({2, 2, 1, kind, 0});
      result = EvaluateMission(contract, world, 1, true);
      Require(!result.maestroEligible && result.rank != Rank::Maestro, "Armas, CQC o detección invalidan Maestro de forma irreversible");
      world.events.pop_back();
    }
    world.objectives = 0;
    Require(!EvaluateMission(contract, world, 1, true).maestroEligible, "Sin objetivos obligatorios no se obtiene Maestro");
    world.objectives = 1; world.agents[0].extracted = false;
    Require(!EvaluateMission(contract, world, 1, true).maestroEligible, "Sin extracción no se obtiene Maestro");
    world.agents[0].extracted = true; world.elapsed = 500.f;
    Require(EvaluateMission(contract, world, 1, true).rank == Rank::Maestro, "El tiempo no debe sustituir la disciplina operacional");
    for (const auto [detections, secondary, expected] : std::array<std::tuple<int, int, Rank>, 6>{{
      {5, 0, Rank::D}, {3, 0, Rank::C}, {1, 0, Rank::B}, {0, 0, Rank::A}, {0, 1, Rank::S}, {0, 2, Rank::SPlus}}}) {
      world.secondary = secondary;
      world.events.clear();
      world.events.push_back({1, 1, 1, EventKind::Cqc, 0});
      for (int index = 0; index < detections; ++index) world.events.push_back({static_cast<std::uint64_t>(index + 2), 1, 1, EventKind::ConfirmedDetection, 0});
      Require(EvaluateMission(contract, world, 1, true).rank == expected, "Los umbrales deben distinguir D, C, B, A, S y S+");
    }

    Simulation simulation;
    simulation.ResetBase(); simulation.Deploy(0);
    auto snapshot = simulation.SnapshotFor(1);
    snapshot["guards"] = nlohmann::json::array();
    for (auto &object : snapshot["objects"]) if (object["kind"] == static_cast<int>(ObjectKind::Camera)) object["active"] = false;
    snapshot["agents"][0]["position"] = {105.f, 665.f};
    Load(simulation, snapshot);
    ActionCommand interact; interact.interact = true;
    simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().phase == GamePhase::Mission && !simulation.GetState().agents[0].extracted, "No debe extraer antes del objetivo principal");
    snapshot = simulation.SnapshotFor(1); snapshot["agents"][0]["position"] = {570.f, 275.f};
    Load(simulation, snapshot); simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().objectives == 1, "El terminal debe completar un objetivo real");
    simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().objectives == 1, "Un objetivo no debe puntuar dos veces");
    snapshot = simulation.SnapshotFor(1); snapshot["agents"][0]["position"] = {105.f, 665.f};
    Load(simulation, snapshot); simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().phase == GamePhase::Results && simulation.GetState().results.at(1).success, "El bucle objetivo y extracción debe producir resultados");
    simulation.ReturnToBase();
    Require(simulation.GetState().phase == GamePhase::Base && simulation.GetState().objects.size() == 4, "Debe poder regresar a preparación");

    simulation.SetContract(ContractId::SilentRelay); simulation.Deploy(2);
    snapshot = simulation.SnapshotFor(1); snapshot["guards"] = nlohmann::json::array();
    for (auto &object : snapshot["objects"]) if (object["kind"] == static_cast<int>(ObjectKind::Camera)) object["active"] = false;
    snapshot["agents"][0]["position"] = {840.f, 360.f};
    Load(simulation, snapshot); simulation.Step(0.1f, {{1, interact}}); Advance(simulation, {}, 30);
    Require(simulation.GetState().objectives == 1, "El repetidor cooperativo debe poder resolverse en individual");
    Require(std::ranges::any_of(simulation.GetState().objects, [](const Interactable &object) { return object.kind == ObjectKind::CoopSwitch && object.completed; }), "El sabotaje debe modificar el objeto");
  }

  void PrivacyAndDisconnect() {
    Simulation simulation;
    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}, {3, "Tres"}});
    simulation.SetContract(ContractId::DoubleCross); simulation.Deploy(1);
    const auto one = simulation.SnapshotFor(1);
    const auto two = simulation.SnapshotFor(2);
    Require(one.dump().size() < GameplayConfig::MaximumSnapshotBytes, "El estado debe caber en el transporte");
    for (const auto &agent : one.at("agents")) Require(agent.contains("privateOrder") == (agent.at("id") == 1), "El jugador uno no debe recibir órdenes ajenas");
    for (const auto &agent : two.at("agents")) Require(agent.contains("privateOrder") == (agent.at("id") == 2), "El jugador dos no debe recibir órdenes ajenas");
    Simulation client;
    Load(client, two);
    Require(client.GetState().agents[0].privateOrder == PrivateOrder::None, "El cliente no debe conservar el rol secreto ajeno");
    auto malicious = two; malicious["agents"][0]["privateOrder"] = 3; malicious["agents"][0]["privateComplete"] = false;
    std::string error;
    Require(!client.LoadSnapshot(malicious, error), "Debe rechazar estados que filtren una orden ajena");
    simulation.DisconnectPlayer(2);
    Require(!simulation.GetState().agents[1].connected, "La desconexión debe retirar al jugador activo");
    simulation.ReturnToBase();
    Require(simulation.AddPlayer(2, "Regreso"), "El identificador debe poder reutilizarse al volver a base");

    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.SetContract(ContractId::SilentRelay); simulation.Deploy(0);
    auto snapshot = simulation.SnapshotFor(1); snapshot["guards"] = nlohmann::json::array();
    for (auto &object : snapshot["objects"]) if (object["kind"] == static_cast<int>(ObjectKind::Camera)) object["active"] = false;
    snapshot["agents"][0]["position"] = {840.f, 360.f};
    Load(simulation, snapshot);
    ActionCommand interact; interact.interact = true;
    simulation.Step(0.1f, {{1, interact}});
    simulation.DisconnectPlayer(2);
    Advance(simulation, {}, 30);
    Require(simulation.GetState().objectives == 1, "La desconexión no debe bloquear el objeto cooperativo");

    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.SetContract(ContractId::DoubleCross); simulation.Deploy(0);
    ActionCommand lethal; lethal.fireLethal = true;
    auto towardOther = (simulation.GetState().agents[1].position - simulation.GetState().agents[0].position).normalized();
    lethal.aimX = towardOther.x; lethal.aimY = towardOther.y;
    simulation.Step(0.1f, {{1, lethal}});
    Require(simulation.GetState().phase == GamePhase::Results && simulation.GetState().results.at(1).traitorVictory, "El traidor debe ganar solo después de eliminar al equipo sin alarma");
    Require(!simulation.GetState().agents[1].alive, "La victoria del traidor exige eliminación real");
    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.SetContract(ContractId::DoubleCross); simulation.Deploy(0);
    lethal.fireLethal = false; lethal.fireNonLethal = true;
    simulation.Step(0.1f, {{1, lethal}});
    Require(simulation.GetState().agents[1].alive && simulation.GetState().agents[1].incapacitated, "La fuerza no letal no debe contar como eliminación de un agente");
    Require(!simulation.GetState().results.at(1).traitorVictory, "Incapacitar al equipo no cumple la orden de eliminarlo");
  }

  void SeedReachability() {
    for (std::uint32_t seed = 0; seed < 36; ++seed) {
      Simulation simulation;
      simulation.ResetBase({{1, "Uno"}, {2, "Dos"}, {3, "Tres"}, {4, "Cuatro"}}); simulation.Deploy(seed);
      const auto &world = simulation.GetState();
      constexpr int columns = 50, rows = 38;
      const auto blocked = [&world](int x, int y) {
        const sf::Vector2f point{10.f + x * 20.f, 10.f + y * 20.f};
        if (point.x < 12.f || point.y < 12.f || point.x > world.size.x - 12.f || point.y > world.size.y - 12.f) return true;
        for (const auto &obstacle : world.obstacles) if (obstacle.solid && sf::FloatRect{obstacle.bounds.position - sf::Vector2f{12.f, 12.f}, obstacle.bounds.size + sf::Vector2f{24.f, 24.f}}.contains(point)) return true;
        for (const auto &object : world.objects) if (object.kind == ObjectKind::Door && !object.open && sf::FloatRect{{object.position.x - 24.f, object.position.y - 42.f}, {48.f, 84.f}}.contains(point)) return true;
        return false;
      };
      std::array<bool, columns * rows> visited{};
      std::queue<std::pair<int, int>> queue;
      const auto insertion = world.agents.front().position;
      const int insertionX = static_cast<int>(insertion.x / 20.f), insertionY = static_cast<int>(insertion.y / 20.f);
      queue.push({insertionX, insertionY}); visited[insertionY * columns + insertionX] = true;
      while (!queue.empty()) {
        const auto [x, y] = queue.front(); queue.pop();
        for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
          const int nx = x + dx, ny = y + dy;
          if (nx < 0 || ny < 0 || nx >= columns || ny >= rows || visited[ny * columns + nx] || blocked(nx, ny)) continue;
          visited[ny * columns + nx] = true; queue.push({nx, ny});
        }
      }
      for (const auto &object : world.objects) if (object.kind == ObjectKind::Terminal || object.kind == ObjectKind::Secondary || object.kind == ObjectKind::CoopSwitch || object.kind == ObjectKind::Extraction) {
        bool reachable = false;
        for (int y = 0; y < rows; ++y) for (int x = 0; x < columns; ++x) if (visited[y * columns + x] && (sf::Vector2f{10.f + x * 20.f, 10.f + y * 20.f} - object.position).length() < 45.f) reachable = true;
        Require(reachable, "Toda semilla necesita acceso a objetivos y extracción");
      }
      for (const auto &agent : world.agents) Require(visited[static_cast<int>(agent.position.y / 20.f) * columns + static_cast<int>(agent.position.x / 20.f)], "Los cuatro puntos de inserción deben compartir un corredor conectado");
    }
  }

  void SafeInsertion() {
    for (auto contract : {ContractId::GhostArchive, ContractId::SilentRelay, ContractId::DoubleCross}) {
      for (std::uint32_t seed : {0u, 1u, 2u, 3u, 4u, 5u, 545993526u}) {
        Simulation simulation;
        simulation.ResetBase({{1, "Uno"}, {2, "Dos"}, {3, "Tres"}, {4, "Cuatro"}});
        const auto basePosition = simulation.GetState().agents.front().position;
        Require(basePosition == sf::Vector2f{130.f, 150.f}, "La corrección de inserción no debe cambiar la base");
        simulation.SetContract(contract); simulation.Deploy(seed);
        const auto &world = simulation.GetState();
        for (const auto &agent : world.agents) {
          const float radius = GameplayConfig::AgentRadius;
          Require(agent.position.x >= radius && agent.position.y >= radius && agent.position.x <= world.size.x - radius && agent.position.y <= world.size.y - radius, "Los cuatro agentes deben aparecer dentro del mapa");
          for (const auto &obstacle : world.obstacles) if (obstacle.solid) Require(!sf::FloatRect{obstacle.bounds.position - sf::Vector2f{radius, radius}, obstacle.bounds.size + sf::Vector2f{radius * 2.f, radius * 2.f}}.contains(agent.position), "La inserción no debe intersectar obstáculos");
          for (const auto &object : world.objects) if (object.kind == ObjectKind::Door && !object.open) Require(!sf::FloatRect{{object.position.x - 12.f - radius, object.position.y - 30.f - radius}, {24.f + radius * 2.f, 60.f + radius * 2.f}}.contains(agent.position), "La inserción no debe intersectar una puerta cerrada");
          for (const auto &other : world.agents) if (other.id != agent.id) Require((agent.position - other.position).length() >= radius * 2.f, "Los agentes no deben aparecer superpuestos");
          for (const auto &guard : world.guards) for (std::size_t index = 0; index < guard.patrol.size(); ++index) {
            const auto start = guard.patrol[index], end = guard.patrol[(index + 1) % guard.patrol.size()];
            const auto segment = end - start;
            const float t = std::clamp((agent.position - start).dot(segment) / segment.lengthSquared(), 0.f, 1.f);
            Require((agent.position - (start + segment * t)).length() > GameplayConfig::VisionDistance, "Ningún tramo de patrulla debe observar la inserción de pie");
          }
        }
        for (int frame = 0; frame < 720; ++frame) simulation.Step(1.f / 60.f, {});
        Require(simulation.GetState().phase == GamePhase::Mission, "Una misión no debe terminar mientras el equipo se orienta en la inserción");
        Require(simulation.GetState().alarmCount == 0, "La inserción inmóvil no debe activar alarmas");
        Require(std::ranges::all_of(simulation.GetState().agents, [](const Agent &agent) { return agent.alive && !agent.incapacitated && agent.stance == Stance::Standing; }), "Los cuatro agentes de pie deben seguir operativos tras doce segundos");
        Require(std::ranges::none_of(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection || event.kind == EventKind::AgentKilled; }), "La espera inicial no debe registrar detecciones ni bajas");
      }
    }
  }

  std::vector<sf::Vector2f> PlanRoute(const Simulation &simulation, sf::Vector2f goal, bool stealth) {
    const auto &world = simulation.GetState();
    constexpr int columns = 50, rows = 38;
    const auto blocked = [&world, &simulation, stealth](sf::Vector2f point) {
      if (point.x < 12.f || point.y < 12.f || point.x > world.size.x - 12.f || point.y > world.size.y - 12.f) return true;
      for (const auto &obstacle : world.obstacles) if (obstacle.solid && sf::FloatRect{obstacle.bounds.position - sf::Vector2f{12.f, 12.f}, obstacle.bounds.size + sf::Vector2f{24.f, 24.f}}.contains(point)) return true;
      for (const auto &object : world.objects) {
        if (object.kind == ObjectKind::Door && !object.open && sf::FloatRect{{object.position.x - 24.f, object.position.y - 42.f}, {48.f, 84.f}}.contains(point)) return true;
        if (object.kind == ObjectKind::Camera && object.active) {
          Agent candidate; candidate.position = point; candidate.stance = stealth ? Stance::Prone : Stance::Crouched;
          if (simulation.CanSee(object.position, object.facing, candidate, GameplayConfig::CameraDistance + 25.f)) return true;
        }
      }
      if (stealth) for (const auto &guard : world.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated) {
        const float currentDistance = (guard.position - world.agents.front().position).length();
        if ((guard.position - point).length() < std::min(140.f, currentDistance - 1.f)) return true;
      }
      return false;
    };
    const auto position = world.agents.front().position;
    const int sx = std::clamp(static_cast<int>(position.x / 20.f), 0, columns - 1), sy = std::clamp(static_cast<int>(position.y / 20.f), 0, rows - 1);
    const int start = sy * columns + sx;
    std::array<int, columns * rows> previous;
    previous.fill(-1); previous[start] = start;
    std::queue<int> queue; queue.push(start);
    int finish = -1;
    while (!queue.empty()) {
      const int current = queue.front(); queue.pop();
      const int x = current % columns, y = current / columns;
      if ((sf::Vector2f{10.f + x * 20.f, 10.f + y * 20.f} - goal).length() < 35.f) { finish = current; break; }
      for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        const int nx = x + dx, ny = y + dy, next = ny * columns + nx;
        if (nx < 0 || ny < 0 || nx >= columns || ny >= rows || previous[next] != -1 || blocked({10.f + nx * 20.f, 10.f + ny * 20.f})) continue;
        previous[next] = current; queue.push(next);
      }
    }
    std::vector<sf::Vector2f> route;
    if (finish < 0) return route;
    for (int index = finish; index != start; index = previous[index]) route.push_back({10.f + index % columns * 20.f, 10.f + index / columns * 20.f});
    std::ranges::reverse(route);
    return route;
  }

  void ReachAndInteract(Simulation &simulation, ObjectKind kind, bool stealth) {
    const auto target = std::ranges::find_if(simulation.GetState().objects, [kind](const Interactable &object) { return object.kind == kind; });
    Require(target != simulation.GetState().objects.end(), "El escenario debe contener el objeto solicitado");
    const auto goal = target->position;
    std::vector<sf::Vector2f> route;
    constexpr float dt = 1.f / 30.f;
    for (int frame = 0; frame < 9000; ++frame) {
      const auto &state = simulation.GetState();
      if (state.phase != GamePhase::Mission) throw std::runtime_error("Operación terminada hacia " + std::to_string(static_cast<int>(kind)) + " en " + std::to_string(state.agents.front().position.x) + "," + std::to_string(state.agents.front().position.y) + " t=" + std::to_string(state.elapsed) + ": " + state.message);
      const auto &agent = state.agents.front();
      ActionCommand command;
      if (stealth && agent.stance != Stance::Prone) command.prone = true;
      if (!stealth && agent.stance != Stance::Crouched) command.crouch = true;
      command.crawl = stealth;
      if ((agent.position - goal).length() < 40.f) {
        command.interact = true;
        simulation.Step(dt, {{1, command}});
        if (kind == ObjectKind::CoopSwitch) Advance(simulation, {}, 40);
        return;
      }
      if (frame % 6 == 0 || route.empty()) route = PlanRoute(simulation, goal, stealth);
      while (!route.empty() && (route.front() - agent.position).length() < 4.f) route.erase(route.begin());
      if (!route.empty()) {
        const auto direction = (route.front() - agent.position).normalized();
        command.moveX = direction.x; command.moveY = direction.y;
      }
      if (!stealth) {
        for (const auto &guard : state.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated) {
          Agent candidate; candidate.position = guard.position;
          const auto offset = guard.position - agent.position;
          if (offset.length() > 0.1f && simulation.CanSee(agent.position, offset.normalized(), candidate, GameplayConfig::WeaponRange)) {
            command.fireNonLethal = true; command.aimX = offset.x; command.aimY = offset.y;
            break;
          }
        }
      } else if (agent.loadout.decoys > 0 && agent.actionCooldown <= 0.f) {
        for (const auto &guard : state.guards) if ((guard.state == GuardState::Patrol || guard.state == GuardState::Suspicion) && (guard.position - agent.position).length() < 235.f) {
          const auto towardGuard = guard.position - agent.position;
          command.throwDecoy = true;
          command.aimX = towardGuard.y; command.aimY = -towardGuard.x;
          break;
        }
      }
      simulation.Step(dt, {{1, command}});
    }
    throw std::runtime_error("No se pudo llegar mediante controles al objeto " + std::to_string(static_cast<int>(kind)) + "; " + simulation.GetState().message);
  }

  void CompleteOperationWithoutStateShortcuts() {
    for (std::uint32_t seed : {0u, 1u, 2u, 5u}) {
      for (auto contract : {ContractId::GhostArchive, ContractId::SilentRelay, ContractId::DoubleCross}) {
        Simulation simulation;
        simulation.ResetBase();
        simulation.SetContract(contract);
        simulation.SetLoadout(1, {0, 12, 6, 6});
        simulation.Deploy(seed);
        ReachAndInteract(simulation, ObjectKind::Terminal, false);
        ReachAndInteract(simulation, ObjectKind::Secondary, false);
        ReachAndInteract(simulation, ObjectKind::CoopSwitch, false);
        ReachAndInteract(simulation, ObjectKind::Extraction, false);
        Require(simulation.GetState().phase == GamePhase::Results && simulation.GetState().results.at(1).success, "El contrato completo debe poder terminarse con controles reales");
        Require(simulation.GetState().secondary >= 1, "La ruta debe completar también el objetivo secundario");
        Require(!simulation.GetState().results.at(1).maestroEligible, "Una operación que usa armas no puede lograr Maestro");
        simulation.ReturnToBase();
        Require(simulation.GetState().phase == GamePhase::Base, "El ciclo completo debe volver a base");
      }
    }
    Simulation clean;
    clean.ResetBase(); clean.SetLoadout(1, {0, 0, 6, 6}); clean.Deploy(0);
    ActionCommand approach; approach.moveX = 1.f; approach.moveY = 0.25f; approach.run = true;
    Advance(clean, approach, 7, 1.f / 30.f);
    ReachAndInteract(clean, ObjectKind::Terminal, true);
    ReachAndInteract(clean, ObjectKind::Extraction, true);
    Require(clean.GetState().results.at(1).rank == Rank::Maestro, "Una infiltración real limpia debe lograr Maestro");
    Require(clean.GetState().events.size() >= 2, "La misión limpia debe conservar registro real de objetivos y extracción");
  }
}

int main() {
  try {
    MovementAndBase();
    PerceptionAndNoise();
    CombatAndTools();
    ScoringAndObjectives();
    PrivacyAndDisconnect();
    SeedReachability();
    SafeInsertion();
    CompleteOperationWithoutStateShortcuts();
    std::cout << "NO TRACE: " << checks << " comprobaciones de simulación correctas.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NO TRACE: fallo tras " << checks << " comprobaciones: " << error.what() << '\n';
    return 1;
  }
}
