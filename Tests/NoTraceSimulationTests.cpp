#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceRecoveryFixtures.h"
#include "NoTraceNavigation.h"
#include "Config/NoTraceGameplayConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceDistractionGeometry.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceVisibility.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <queue>
#include <unordered_set>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  const char *currentCase = "argumentos";
  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }
  void Load(Simulation &simulation, const nlohmann::json &snapshot) {
    std::string error;
    auto fixture = snapshot;
    if (fixture.at("surfaces").empty()) for (auto &object : fixture["objects"]) {
      if (!object.contains("elevation")) object["elevation"] = 0.f;
      if (!object.contains("supportId")) object["supportId"] = 0;
      if (!object.contains("elevatorId")) object["elevatorId"] = 0;
    }
    if (!simulation.LoadSnapshot(fixture, error)) throw std::runtime_error("Fixture: " + error);
  }
  nlohmann::json EmptyMission(Simulation &simulation, sf::Vector2f position = {100.f, 600.f}) {
    simulation.ResetBase(); simulation.Deploy(0);
    auto snapshot = simulation.SnapshotFor(1); TestFixture::LegacyPlane(snapshot);
    snapshot["obstacles"] = nlohmann::json::array();
    snapshot["guards"] = nlohmann::json::array();
    snapshot["objects"] = nlohmann::json::array();
    snapshot["securityEntries"] = nlohmann::json::array();
    snapshot["agents"][0]["position"] = {position.x, position.y};
    return snapshot;
  }
  void Advance(Simulation &simulation, ActionCommand command, int frames, float dt = 0.1f) {
    for (int index = 0; index < frames; ++index) simulation.Step(dt, {{1, command}});
  }
  void AimHead(ActionCommand &command, const Agent &actor, sf::Vector2f position, sf::Vector2f facing, Stance stance = Stance::Standing, float elevation = 0.f) {
    const auto head = CombatGeometry::ActorVolumes(position, facing, stance).front().center;
    const auto offset = head - actor.position;
    command.aimX = offset.x; command.aimY = offset.y;
    const float height = elevation + CombatGeometry::HeadCenterHeight(stance) + (stance == Stance::Prone ? 0.f : CombatConfig::HeadHeight * 0.25f);
    command.aimSlope = (height - actor.elevation - CombatGeometry::WeaponHeight(actor.stance)) / std::max(0.001f, offset.length());
  }
  nlohmann::json GuardJson(Simulation &simulation, sf::Vector2f position, sf::Vector2f facing) {
    simulation.ResetBase(); simulation.Deploy(0);
    auto donorSnapshot = simulation.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot); auto value = donorSnapshot["guards"][0];
    value["position"] = {position.x, position.y}; value["facing"] = {facing.x, facing.y};
    value["patrol"] = nlohmann::json::array(); value["waypoint"] = 0;
    return value;
  }

  void MovementOwnsBodyFacing() {
    const sf::Vector2f look = sf::Vector2f{-0.8f, 0.6f};
    for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) for (const bool running : {false, true}) {
      for (const auto movement : {sf::Vector2f{1.f,0.f}, {1.f,1.f}, {0.f,1.f}, {-1.f,1.f}, {-1.f,0.f}, {-1.f,-1.f}, {0.f,-1.f}, {1.f,-1.f}}) {
        Simulation simulation; Load(simulation, EmptyMission(simulation, {500.f,500.f}));
        auto &agent = const_cast<WorldState &>(simulation.GetState()).agents.front();
        agent.stance = stance;
        const auto initial = agent.position;
        ActionCommand command; command.moveX=movement.x; command.moveY=movement.y; command.aimX=look.x; command.aimY=look.y; command.run=running;
        Advance(simulation,command,5,1.f/60.f);
        const auto displacement=agent.position-initial;
        Require(displacement.length()>0.01f && displacement.normalized().dot(movement.normalized())>0.999f,"El movimiento axonométrico cambia al mirar hacia otra dirección");
        Require(agent.facing.dot(movement.normalized())>0.999f,"La orientación corporal no corresponde al movimiento WASD");
        command.aimX=command.aimY=0.f; Advance(simulation,command,2,1.f/60.f);
        Require(agent.facing.dot(movement.normalized())>0.999f,"Una orden sin ratón pierde la orientación WASD");
        command.aimX=0.6f; command.aimY=0.8f; command.aiming=true;
        simulation.Step(1.f/60.f,{{1,command}});
        Require(agent.aiming && WeaponFacing(agent).dot({0.6f,0.8f})>0.999f && agent.facing.dot(movement.normalized())>0.999f,"Apuntar no separa el arma del cuerpo durante el desplazamiento");
        command.aiming=false; command.moveX=command.moveY=0.f; command.aimX=-1.f; command.aimY=0.f;
        simulation.Step(1.f/60.f,{{1,command}});
        Require(!agent.aiming && agent.facing.dot(movement.normalized())>0.999f,"Mover el ratón en reposo gira el cuerpo o pierde la última orientación");
      }
    }
    Simulation simulation; Load(simulation,EmptyMission(simulation,{500.f,500.f}));
    auto &agent=const_cast<WorldState &>(simulation.GetState()).agents.front();
    agent.facing={1.f,0.f}; agent.staggerRemaining=0.2f;
    ActionCommand command; command.aimX=-1.f; command.moveY=1.f;
    simulation.Step(1.f/60.f,{{1,command}});
    Require(agent.facing==sf::Vector2f{1.f,0.f} && !agent.moving,"La mirada interrumpe la orientación de una reacción de daño");
    Advance(simulation,command,20,1.f/60.f);
    Require(agent.facing.y>.999f && agent.moving,"Al finalizar la reacción no se recupera la orientación WASD");
  }

  void MovementAndBase() {
    Simulation simulation;
    simulation.ResetBase();
    for (const auto kind : {ObjectKind::Operations, ObjectKind::Armory, ObjectKind::Intelligence, ObjectKind::Deployment})
      Require(std::ranges::count_if(simulation.GetState().objects, [kind](const Interactable &object) { return object.kind == kind; }) == 1, "La sala de entrenamiento debe conservar cada estación CIO");
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
    move.moveX = -1.f; move.moveY = -1.f;
    Advance(simulation, move, 10);
    Require(std::abs((simulation.GetState().agents[0].position - start).length() - GameplayConfig::WalkSpeed) < 0.1f, "La diagonal no debe acelerar");
    simulation.ResetBase();
    ActionCommand prone; prone.prone = true;
    simulation.Step(0.1f, {{1, prone}});
    move.moveX = 1.f; move.moveY = 0.f;
    Advance(simulation, move, 10);
    Require(std::abs(simulation.GetState().agents[0].position.x - start.x - GameplayConfig::CrawlSpeed) < 0.1f, "Moverse tumbado debe arrastrar automáticamente sin otra tecla");
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
    Require(simulation.GetState().decoys.size() == 1 && simulation.GetState().noises.empty() && simulation.GetState().guards[0].state != GuardState::Investigate, "El señuelo en vuelo permanece silencioso hasta el impacto");
    for (int frame = 0; frame < 30 && !simulation.GetState().decoys.empty(); ++frame) simulation.Step(0.1f, {});
    Require(simulation.GetState().decoys.empty(), "El señuelo debe aterrizar dentro de su duración máxima de vuelo");
    Require(simulation.GetState().guards[0].state == GuardState::Investigate, "El ruido debe provocar investigación");
    Require((simulation.GetState().guards[0].lastKnown - sf::Vector2f{150.f + DistractionConfig::MaximumDecoyDistance, 500.f}).length() < 0.01f, "El guardia debe investigar el señuelo, no conocer al agente");
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
    const int confirmationFrames = static_cast<int>(std::ceil(GameplayConfig::ConfirmationTime / (SecurityConfig::StationaryDetectionFactor * (SecurityConfig::DetectionNearFactor - SecurityConfig::DetectionDistanceReduction)) / 0.1f)) + 1;
    Advance(simulation, {}, confirmationFrames);
    Require(simulation.GetState().alarm, "La visión sostenida debe activar alarma tras el umbral configurado y su modulación");
    Require(std::ranges::any_of(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection; }), "La detección confirmada debe quedar registrada");
    auto escaped = simulation.SnapshotFor(1);
    escaped["agents"][0]["position"] = {800.f, 700.f};
    Load(simulation, escaped);
    simulation.Step(0.1f, {});
    Require(simulation.GetState().guards[0].state == GuardState::Search, "Al romper visión debe buscar la última posición conocida");
    Require((simulation.GetState().guards[0].lastKnown - sf::Vector2f{400.f, 500.f}).length() < 0.01f, "La búsqueda no debe seguir la posición oculta actual");

    snapshot = EmptyMission(simulation, {200.f, 200.f});
    Load(simulation, snapshot);
    ActionCommand knock; knock.cqc = true; knock.cqcHeld = true;
    simulation.Step(0.1f, {{1, knock}});
    Require(simulation.GetState().noises.empty(), "No debe golpear una pared inexistente");
    snapshot["obstacles"].push_back({{"position", {220.f, 150.f}}, {"size", {20.f, 100.f}}, {"cover", 2}, {"solid", true}, {"name", "Pared"}});
    Load(simulation, snapshot);
    ActionCommand cover; cover.coverToggle = true;
    simulation.Step(0.1f, {{1, cover}});
    Require(simulation.GetState().agents[0].cover.active, "El golpe contextual requiere adherirse primero a una pared real");
    simulation.Step(0.1f, {{1, knock}});
    Require(!simulation.GetState().noises.empty(), "Golpear una pared cercana debe producir un ruido real");

    snapshot = EmptyMission(simulation, {400.f, 500.f});
    snapshot["objects"].push_back({{"id", 90}, {"kind", static_cast<int>(ObjectKind::Camera)}, {"position", {300.f, 500.f}}, {"name", "Cámara"}, {"active", true}, {"completed", false}, {"open", false}, {"progress", 0.f}, {"facing", {1.f, 0.f}}});
    Load(simulation, snapshot);
    Advance(simulation, {}, 350);
    Require(simulation.GetState().alarm, "La visión continua debe mantener la alarma más de treinta segundos");
    snapshot = simulation.SnapshotFor(1); snapshot["agents"][0]["hidden"] = true;
    Load(simulation, snapshot);
    Advance(simulation, {}, 310);
    Require(!simulation.GetState().alarm, "Al perder la visión la alarma debe terminar después de la búsqueda");

    snapshot = EmptyMission(simulation, {900.f, 700.f});
    snapshot["guards"].push_back(GuardJson(donor, {150.f, 200.f}, {1.f, 0.f}));
    snapshot["obstacles"].push_back({{"position", {190.f, 180.f}}, {"size", {20.f, 60.f}}, {"cover", 2}, {"solid", true}, {"name", "Muro entre guardia y ruido"}});
    snapshot["noises"].push_back({{"position", {250.f, 200.f}}, {"radius", 300.f}, {"remaining", 0.6f}, {"actor", 1}, {"visualDirection", {0.f, 0.f}}, {"elevation", 0.f}});
    Load(simulation, snapshot);
    const auto wallBounds = simulation.GetState().obstacles[0].bounds;
    const sf::Vector2f clearCorner{180.f, 170.f};
    Require(WorldGeometry::Expanded(wallBounds, GameplayConfig::AgentRadius).contains(clearCorner) &&
        !WorldGeometry::CircleOverlaps(clearCorner, GameplayConfig::AgentRadius, wallBounds), "La esquina libre no distingue la huella circular de su AABB conservadora");
    for (int frame = 0; frame < 40; ++frame) {
      const auto previous = simulation.GetState().guards[0].position;
      simulation.Step(0.1f, {});
      const auto &world = simulation.GetState(); const auto position = world.guards[0].position;
      Require(!WorldGeometry::PositionBlocked(world, position, GameplayConfig::AgentRadius), "La navegación no debe penetrar el muro con su huella circular");
      Require(WorldGeometry::ClearMovementSegment(world, previous, position, GameplayConfig::AgentRadius), "El desplazamiento de navegación atraviesa el muro entre dos posiciones libres");
      Require((position - previous).length() <= GameplayConfig::GuardAlertSpeed * 0.1f + SpatialConfig::CollisionSkin, "La navegación teletransporta al guardia para evitar la colisión del muro");
    }
    Require((simulation.GetState().guards[0].position - sf::Vector2f{250.f, 200.f}).length() < 20.f, "El guardia debe rodear el muro para investigar el ruido");
  }

  void CombatAndTools() {
    Simulation simulation, donor;
    auto snapshot = EmptyMission(simulation, {300.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {330.f, 500.f}, {1.f, 0.f}));
    Load(simulation, snapshot);
    ActionCommand cqc; cqc.cqc = true; cqc.cqcHeld = true; cqc.aimX = 1.f;
    Advance(simulation, cqc, 2);
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
    hide["objects"].push_back({{"id", 90}, {"kind", static_cast<int>(ObjectKind::Hideout)}, {"position", {368.f, 500.f}}, {"name", "Escondite"}, {"active", true}, {"completed", false}, {"open", false}, {"progress", 0.f}, {"facing", {1.f, 0.f}}});
    Load(simulation, hide);
    ActionCommand interact; interact.interact = true; interact.dragBody = true;
    simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().guards[0].bodyHidden, "Debe poder ocultar un cuerpo en escondite válido");

    snapshot = EmptyMission(simulation, {300.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {400.f, 500.f}, {1.f, 0.f}));
    Load(simulation, snapshot);
    ActionCommand shoot; shoot.fireNonLethal = true; shoot.aimX = 1.f;
    AimHead(shoot, simulation.GetState().agents[0], simulation.GetState().guards[0].position, simulation.GetState().guards[0].facing);
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().guards[0].state == GuardState::Incapacitated, "El arma no letal debe incapacitar");
    Load(simulation, snapshot);
    shoot.fireNonLethal = false; shoot.fireLethal = true;
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().guards[0].state == GuardState::Dead, "El arma letal debe eliminar");
    Require(simulation.GetState().agents[0].loadout.lethalAmmo == GameplayConfig::StartingPistolAmmo - 1, "Disparar debe consumir munición");
  }

  void WeaponTrajectoriesAndVitals() {
    Simulation simulation, donor, replica;
    auto snapshot = EmptyMission(simulation, {300.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {450.f, 500.f}, {1.f, 0.f}));
    snapshot["guards"].push_back(GuardJson(donor, {400.f, 525.f}, {1.f, 0.f}));
    snapshot["guards"][1]["id"] = 2;
    Load(simulation, snapshot);
    ActionCommand shoot; shoot.fireLethal = true; shoot.aimX = 1.f;
    AimHead(shoot, simulation.GetState().agents[0], simulation.GetState().guards[0].position, simulation.GetState().guards[0].facing);
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().guards[0].state == GuardState::Dead && simulation.GetState().guards[1].state != GuardState::Dead, "La bala sigue el rayo de puntería, no selecciona un guardia cercano fuera de trayectoria");
    const float headEntry = 450.f + CombatConfig::HeadForwardOffset - CombatConfig::HeadHalfLength;
    Require(simulation.GetState().shots.size() == 1 && simulation.GetState().shots[0].hit && simulation.GetState().shots[0].zone == HitZone::Head && std::abs(simulation.GetState().shots[0].to.x - headEntry) < 0.01f, "El trazo termina en la primera intersección real con el volumen orientado de cabeza");
    const auto fired = simulation.SnapshotFor(1);
    Load(replica, fired);
    Require(replica.GetState().shots[0].hit && replica.GetState().shots[0].to == simulation.GetState().shots[0].to, "El snapshot propaga impacto y trayectoria autoritativos");
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().agents[0].loadout.lethalAmmo == GameplayConfig::StartingPistolAmmo - 1, "El cooldown bloquea un segundo disparo inmediato");
    Advance(simulation, {}, 7);
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().agents[0].loadout.lethalAmmo == GameplayConfig::StartingPistolAmmo - 2, "Al vencer la cadencia puede dispararse de nuevo");

    snapshot["obstacles"].push_back({{"position", {350.f, 470.f}}, {"size", {20.f, 60.f}}, {"cover", 2}, {"solid", true}, {"name", "Muro balístico"}});
    Load(simulation, snapshot);
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().guards[0].state != GuardState::Dead && simulation.GetState().shots[0].blocked && std::abs(simulation.GetState().shots[0].to.x - 350.f) < 0.01f, "Un muro sólido detiene la bala antes del objetivo");
    Require(simulation.GetState().agents[0].loadout.lethalAmmo == GameplayConfig::StartingPistolAmmo - 1, "Un disparo bloqueado también gasta munición");
    snapshot["obstacles"] = nlohmann::json::array();
    snapshot["objects"].push_back({{"id", 90}, {"kind", static_cast<int>(ObjectKind::Door)}, {"position", {370.f, 500.f}}, {"name", "Puerta"}, {"active", true}, {"completed", false}, {"open", false}, {"progress", 0.f}, {"facing", {1.f, 0.f}}});
    Load(simulation, snapshot);
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().shots[0].blocked && simulation.GetState().guards[0].state != GuardState::Dead, "La puerta cerrada bloquea disparos");
    snapshot["objects"][0]["open"] = true;
    Load(simulation, snapshot);
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().guards[0].state == GuardState::Dead, "La puerta abierta permite un disparo alineado");
    snapshot["agents"][0]["loadout"][0] = 0;
    Load(simulation, snapshot);
    simulation.Step(0.1f, {{1, shoot}});
    Require(simulation.GetState().shots.empty() && simulation.GetState().agents[0].loadout.lethalAmmo == 0, "Sin munición no se crean tiros ni cantidades negativas");

    snapshot = EmptyMission(simulation, {100.f, 600.f});
    Load(simulation, snapshot);
    ActionCommand run; run.moveX = 1.f; run.run = true;
    Advance(simulation, run, 35);
    const float depleted = simulation.GetState().agents[0].stamina;
    Require(depleted < 30.f && depleted >= 0.f && simulation.GetState().agents[0].health == GameplayConfig::MaxHealth, "Correr consume resistencia sin dañar salud");
    Advance(simulation, {}, 20);
    Require(simulation.GetState().agents[0].stamina > depleted, "Descansar recupera resistencia");
    auto invalid = simulation.SnapshotFor(1);
    invalid["agents"][0]["health"] = GameplayConfig::MaxHealth + 1.f;
    std::string error;
    Require(!replica.LoadSnapshot(invalid, error), "El snapshot rechaza salud fuera de límites");
  }

  void BallisticHeightRoundTrip() {
    for (const float elevation : {0.f, 192.f}) {
      Simulation simulation, replica;
      if (elevation == 0.f) Load(simulation, EmptyMission(simulation, {300.f, 500.f}));
      else {
        simulation.ResetBase(); simulation.Deploy(0);
        auto &world = const_cast<WorldState &>(simulation.GetState()); world.guards.clear();
        auto &agent = world.agents.front(); agent.position = {1100.f, 110.f}; agent.elevation = elevation; agent.supportId = 20;
      }
      ActionCommand fire; fire.fireLethal = true; fire.aimX = 1.f; fire.aimSlope = CombatConfig::MaximumAimSlope;
      simulation.Step(1.f / 60.f, {{1, fire}});
      const auto &world = simulation.GetState();
      Require(world.shots.size() == 1 && !world.shots.front().hit && !world.shots.front().blocked, "El disparo ascendente legal no alcanza un cuerpo ni un sólido");
      const auto &shot = world.shots.front(); const float origin = elevation + CombatGeometry::WeaponHeight(world.agents.front().stance);
      Require(shot.fromHeight == origin && std::abs(shot.toHeight - origin - GameplayConfig::WeaponRange * CombatConfig::MaximumAimSlope) < 0.01f,
          "La trayectoria ascendente conserva la altura física del arma y el alcance completo");
      const auto snapshot = simulation.SnapshotFor(1); std::string error;
      Require(replica.LoadSnapshot(snapshot, error) && replica.GetState().shots.size() == 1 && replica.GetState().shots.front().fromHeight == shot.fromHeight && replica.GetState().shots.front().toHeight == shot.toHeight,
          "El snapshot debe aceptar y conservar un disparo real al cielo desde suelo y techo");
      const auto filtered = simulation.SnapshotFor(1, true);
      Require(replica.LoadSnapshot(filtered, error) && !replica.GetState().shots.empty(), "El recorte privado de una trayectoria ascendente legal debe cargar en el cliente");
      auto malformed = snapshot; malformed["shots"][0]["toHeight"] = GameplayConfig::MaximumShotEndpointHeight + 1.f;
      Require(!replica.LoadSnapshot(malformed, error), "El límite balístico no acepta alturas de impacto superiores al máximo configurado");
      malformed = snapshot; malformed["shots"][0]["fromHeight"] = LevelConfig::MaximumElevation + CombatConfig::StandingHeight + 1.f;
      Require(!replica.LoadSnapshot(malformed, error), "Ampliar la cota del extremo no permite un origen de disparo fuera de la altura física");
    }
  }

  void GuardAttacksAndRecovery() {
    Simulation simulation, donor, replica;
    auto snapshot = EmptyMission(simulation, {300.f, 500.f});
    auto guard = GuardJson(donor, {480.f, 500.f}, {-1.f, 0.f});
    guard["state"] = static_cast<int>(GuardState::Alert); guard["suspicion"] = 1.f;
    snapshot["guards"].push_back(guard);
    Load(simulation, snapshot);
    simulation.Step(0.1f, {});
    const auto firstHealth = simulation.GetState().agents[0].health;
    Require(firstHealth == GameplayConfig::MaxHealth - GameplayConfig::GuardShotDamage && simulation.GetState().agents[0].alive, "Un guardia alerta dispara a distancia y aplica daño discreto");
    Require(simulation.GetState().guards[0].ammo == GameplayConfig::GuardAmmo - 1, "El disparo del guardia consume una bala");
    Require(simulation.GetState().shots.size() == 1 && simulation.GetState().shots[0].guardId == 1 && simulation.GetState().shots[0].hit, "El tiro enemigo tiene origen, impacto e identidad autoritativos");
    Load(replica, simulation.SnapshotFor(1));
    Require(replica.GetState().agents[0].health == firstHealth && replica.GetState().guards[0].attackTime > 0.f && replica.GetState().shots[0].guardId == 1, "Salud y pose de ataque llegan al cliente");
    Advance(simulation, {}, 8);
    Require(simulation.GetState().agents[0].health == firstHealth, "El guardia respeta su intervalo entre ataques");
    Require(simulation.GetState().guards[0].ammo == GameplayConfig::GuardAmmo - 1, "El cooldown del guardia impide gastar otra bala");
    Advance(simulation, {}, 14);
    Require(simulation.GetState().agents[0].health < firstHealth, "El guardia vuelve a atacar tras el cooldown");
    snapshot["obstacles"].push_back({{"position", {380.f, 470.f}}, {"size", {20.f, 60.f}}, {"cover", 2}, {"solid", true}, {"name", "Muro"}});
    Load(simulation, snapshot);
    Advance(simulation, {}, 5);
    Require(simulation.GetState().agents[0].health == GameplayConfig::MaxHealth && simulation.GetState().shots.empty(), "Un guardia no dispara sin línea de visión");
    snapshot["obstacles"] = nlohmann::json::array();
    snapshot["guards"][0]["position"] = {330.f, 500.f};
    Load(simulation, snapshot);
    simulation.Step(0.1f, {});
    Require(simulation.GetState().agents[0].health == GameplayConfig::MaxHealth - GameplayConfig::GuardMeleeDamage && simulation.GetState().guards[0].meleeAttack, "El ataque cuerpo a cuerpo usa su daño y pose propios");
    Require(simulation.GetState().agents[0].stamina == GameplayConfig::MaxStamina - GameplayConfig::GuardMeleeStaminaDamage && simulation.GetState().shots.empty(), "El golpe consume resistencia sin inventar una bala");
    Require(simulation.GetState().guards[0].ammo == GameplayConfig::GuardAmmo, "El golpe cuerpo a cuerpo no consume munición");
    Advance(simulation, {}, 43);
    Require(simulation.GetState().agents[0].alive && simulation.GetState().agents[0].incapacitated && simulation.GetState().phase == GamePhase::Results, "Agotar la resistencia del último agente operativo termina la operación con derrota");
    snapshot = simulation.SnapshotFor(1);
    Load(simulation, snapshot);
    Advance(simulation, {}, 82);
    Require(simulation.GetState().agents[0].incapacitated && simulation.GetState().phase == GamePhase::Results, "La derrota permanece reconocible sin reanimación ni reinicio automático");
    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.Deploy(0);
    snapshot = simulation.SnapshotFor(1);
    TestFixture::LegacyPlane(snapshot); snapshot["agents"][0]["privateOrder"] = static_cast<int>(PrivateOrder::CopyArchive);
    snapshot["guards"] = nlohmann::json::array(); snapshot["obstacles"] = nlohmann::json::array(); snapshot["objects"] = nlohmann::json::array();
    snapshot["agents"][0]["incapacitated"] = true; snapshot["agents"][0]["lifeState"] = static_cast<int>(LifeState::Incapacitated); snapshot["agents"][0]["incapacityRemaining"] = 0.15f; snapshot["agents"][0]["stamina"] = 0.f;
    snapshot["agents"][1]["position"] = {900.f, 700.f};
    Load(simulation, snapshot);
    Advance(simulation, {}, 2);
    Require(simulation.GetState().phase == GamePhase::Mission && simulation.GetState().agents[0].lifeState == LifeState::Dead && simulation.GetState().agents[0].rescueExpired, "Vencer el plazo causa una baja individual sin recuperar al agente ni terminar la operación del superviviente");

    snapshot = EmptyMission(simulation, {300.f, 500.f});
    snapshot["guards"].push_back(GuardJson(donor, {400.f, 500.f}, {1.f, 0.f}));
    Load(simulation, snapshot);
    ActionCommand stun; stun.fireNonLethal = true; stun.aimX = 1.f;
    AimHead(stun, simulation.GetState().agents[0], simulation.GetState().guards[0].position, simulation.GetState().guards[0].facing);
    simulation.Step(0.1f, {{1, stun}});
    snapshot = simulation.SnapshotFor(1);
    snapshot["guards"][0]["incapacityRemaining"] = 0.15f;
    snapshot["agents"][0]["hidden"] = true;
    Load(simulation, snapshot);
    Advance(simulation, {}, 2);
    Require(simulation.GetState().guards[0].state != GuardState::Incapacitated, "Un guardia no letal recupera consciencia al vencer su tiempo");

    snapshot = EmptyMission(simulation, {300.f, 500.f});
    guard["ammo"] = 1;
    snapshot["guards"].push_back(guard);
    Load(simulation, snapshot);
    simulation.Step(0.1f, {});
    Require(simulation.GetState().guards[0].ammo == 0 && simulation.GetState().agents[0].health == firstHealth, "El último disparo del guardia agota su munición finita");
    Load(replica, simulation.SnapshotFor(1));
    Require(replica.GetState().guards[0].ammo == 0, "El snapshot propaga la munición restante del guardia");
    Advance(simulation, {}, 8);
    Require(simulation.GetState().shots.empty() && simulation.GetState().agents[0].health == firstHealth, "Sin munición el guardia persigue sin inventar proyectiles");
    Advance(simulation, {}, 14);
    const auto &events = simulation.GetState().events;
    Require(std::ranges::count_if(events, [](const MissionEvent &event) { return event.kind == EventKind::GuardShot; }) == 1 && std::ranges::any_of(events, [](const MissionEvent &event) { return event.kind == EventKind::GuardMelee; }), "Tras agotar su arma el guardia debe alcanzar al agente y continuar con melee");
    Require(simulation.GetState().guards[0].ammo == 0 && simulation.GetState().agents[0].health < firstHealth, "La munición no puede quedar negativa ni impedir el ataque cuerpo a cuerpo");
    auto invalid = simulation.SnapshotFor(1);
    std::string error;
    invalid["guards"][0]["ammo"] = -1;
    Require(!replica.LoadSnapshot(invalid, error), "El snapshot rechaza munición negativa del guardia");
    invalid["guards"][0]["ammo"] = GameplayConfig::GuardAmmo + 1;
    Require(!replica.LoadSnapshot(invalid, error), "El snapshot rechaza munición de guardia superior al máximo");
    invalid["guards"][0]["ammo"] = 0.5f;
    Require(!replica.LoadSnapshot(invalid, error), "El snapshot rechaza munición fraccionaria de guardia");
  }

  void AtomicLifeAndExplicitRescue() {
    const auto prepare = [](Simulation &simulation, bool partner) -> WorldState & {
      simulation.ResetBase(partner ? std::vector<PlayerProfile>{{1, "Uno"}, {2, "Dos"}} : std::vector<PlayerProfile>{{1, "Uno"}});
      simulation.Deploy(7);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      TestFixture::LegacyPlane(world);
      world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.events.clear(); world.noises.clear();
      for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::CopyArchive;
      world.agents[0].position = {400.f, 400.f};
      if (partner) world.agents[1].position = {900.f, 700.f};
      return world;
    };
    const auto attack = [](WorldState &world) {
      Guard guard; guard.id = 44; guard.position = {550.f, 400.f}; guard.facing = {-1.f, 0.f}; guard.state = GuardState::Alert; guard.suspicion = 1.f;
      world.guards.push_back(guard);
      world.agents[0].health = GameplayConfig::GuardShotDamage; world.agents[0].stamina = 0.f;
    };
    Simulation solo;
    auto &soloWorld = prepare(solo, false); attack(soloWorld); solo.Step(0.1f, {});
    Require(soloWorld.phase == GamePhase::Results && !soloWorld.agents[0].alive && soloWorld.agents[0].lifeState == LifeState::Dead && soloWorld.agents[0].health == 0.f && soloWorld.agents[0].stamina == 0.f,
      "Vida y stamina agotadas simultáneamente confirman muerte y derrota individual en el mismo paso");
    Advance(solo, {}, 600);
    Require(soloWorld.agents[0].lifeState == LifeState::Dead && soloWorld.agents[0].health == 0.f && !soloWorld.results.at(1).success,
      "La derrota y el estado muerto no permiten regeneración ni resurrección posterior");
    Simulation cooperative, replica;
    auto &world = prepare(cooperative, true); attack(world); cooperative.Step(0.1f, {}); world.guards.clear();
    Require(world.phase == GamePhase::Mission && world.agents[0].lifeState == LifeState::Incapacitated && world.agents[0].health == 0.f && world.agents[0].stamina == 0.f && world.agents[0].incapacityRemaining == 55.f,
      "El daño fatal cooperativo inicia exactamente 55 segundos autoritativos sin prioridad de stamina");
    Advance(cooperative, {}, 100);
    Require(world.agents[0].incapacitated && world.agents[0].health == 0.f && world.agents[0].stamina == 0.f && world.agents[0].incapacityRemaining < 45.1f && world.agents[0].incapacityRemaining > 44.9f,
      "Diez segundos de espera no recuperan vida ni stamina de un incapacitado");
    std::string error;
    Require(replica.LoadSnapshot(cooperative.SnapshotFor(2), error) && replica.GetState().agents[0].incapacityRemaining == world.agents[0].incapacityRemaining && replica.GetState().agents[0].lifeState == LifeState::Incapacitated,
      "El cliente recibe el plazo y estado vital exactos del anfitrión");
    ActionCommand rescue; rescue.interact = true;
    cooperative.Step(0.1f, {{2, rescue}});
    Require(world.agents[0].incapacitated, "Una orden de rescate fuera de alcance no recupera al compañero");
    world.agents[1].position = {360.f, 400.f}; world.obstacles.push_back({{{379.f, 390.f}, {4.f, 20.f}}, CoverKind::High, true, "Muro de rescate"});
    cooperative.Step(0.1f, {{2, rescue}});
    Require(world.agents[0].incapacitated, "Un rescate no atraviesa una pared aunque esté dentro del radio de interacción");
    world.obstacles.clear();
    Guard nearbyBody; nearbyBody.id = 90; nearbyBody.position = {365.f, 400.f}; nearbyBody.state = GuardState::Dead; nearbyBody.health = 0.f; world.guards.push_back(nearbyBody);
    rescue.dragBody = true; cooperative.Step(0.1f, {{2, rescue}}); rescue.dragBody = false;
    Require(world.agents[1].draggingGuard < 0, "E prioriza el rescate válido frente al cuerpo neutralizado que está junto al compañero");
    Require(world.agents[0].lifeState == LifeState::Alive && !world.agents[0].incapacitated && world.agents[0].health == GameplayConfig::RevivedHealth && world.agents[0].incapacityRemaining == 0.f,
      "E valida un rescate próximo, sin oclusión y antes del plazo con la salud configurada");
    Require(std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::AgentRevived; }) == 1,
      "Sólo el rescate explícito registra una recuperación y no la espera ni la resistencia");
    auto &fullDeadline = prepare(cooperative, true); attack(fullDeadline); cooperative.Step(0.1f, {}); fullDeadline.guards.clear();
    Advance(cooperative, {}, 540);
    Require(fullDeadline.phase == GamePhase::Mission && fullDeadline.agents[0].incapacitated && fullDeadline.agents[0].incapacityRemaining > 0.9f && fullDeadline.agents[0].incapacityRemaining < 1.1f,
      "Un compañero activo conserva la operación durante 54 segundos completos del plazo de rescate");
    Advance(cooperative, {}, 11);
    Require(fullDeadline.phase == GamePhase::Mission && fullDeadline.agents[0].rescueExpired && !fullDeadline.agents[0].alive && fullDeadline.agents[1].alive,
      "El plazo real de55segundos elimina al incapacitado y permite continuar al superviviente");
    auto &expired = prepare(cooperative, true);
    expired.agents[1].lifeState = LifeState::Incapacitated; expired.agents[1].incapacitated = true; expired.agents[1].health = expired.agents[1].stamina = 0.f;
    expired.agents[1].incapacityRemaining = 0.05f; expired.agents[1].position = {430.f, 400.f};
    cooperative.Step(0.1f, {{1, rescue}});
    Require(expired.phase == GamePhase::Mission && expired.agents[1].rescueExpired && expired.agents[1].lifeState == LifeState::Dead && expired.agents[1].health == 0.f,
      "El plazo vence antes de las órdenes incluso si el rescatador se actualiza primero");
    auto &allDown = prepare(cooperative, true);
    for (auto &agent : allDown.agents) { agent.lifeState = LifeState::Incapacitated; agent.incapacitated = true; agent.health = agent.stamina = 0.f; agent.incapacityRemaining = 55.f; }
    cooperative.Step(0.1f, {});
    Require(allDown.phase == GamePhase::Results && !allDown.results.at(1).success, "Todos incapacitados provocan derrota sin una recuperación automática del equipo");
  }

  void ScoringAndObjectives() {
    WorldState world;
    Agent agent; agent.id = 1; agent.extracted = true;
    world.agents.push_back(agent); world.objectives = 1; world.secondary = 1;
    Interactable secondary{12, ObjectKind::Secondary, {}, "Objetivo secundario"}; secondary.completed = true; world.objects.push_back(secondary);
    const auto &contract = GetContract(ContractId::GhostArchive);
    world.events.push_back({1, 1, 1, EventKind::Noise, 0});
    auto result = EvaluateMission(contract, world, 1, true);
    Require(result.rank == Rank::Maestro && result.maestroEligible && result.maestroDeniedReason.empty(), "Una distracción limpia y todas las secundarias permiten Maestro");
    world.events.push_back({2, 2, 0, EventKind::BodyFound, 2});
    world.alarm = false;
    result = EvaluateMission(contract, world, 1, true);
    Require(!result.maestroEligible && result.rank != Rank::Maestro && result.maestroDeniedReason.find("cuerpos") != std::string::npos,
      "Descubrir un cuerpo impide Maestro aunque se haya calmado la alarma y no existan eventos de armas, CQC o detección");
    world.events.pop_back();
    world.objects[0].completed = false; world.secondary = 8;
    Interactable optional{999, ObjectKind::Secondary, {}, "Recogida opcional"}; optional.completed = true; world.objects.push_back(optional);
    result = EvaluateMission(contract, world, 1, true);
    Require(!result.maestroEligible && result.rank != Rank::Maestro && result.maestroDeniedReason.find("secundarias") != std::string::npos, "Un contador alto y una recogida ajena no sustituyen una misión secundaria pendiente");
    world.objects[0].completed = true; world.secondary = 1;
    Contract expanded = contract; expanded.secondaryObjectives = 2; expanded.secondaryTargets = {12, 999};
    Require(EvaluateMission(expanded, world, 1, true).maestroEligible, "Un contrato puede declarar explícitamente una recogida como segunda misión secundaria");
    world.objects[1].completed = false;
    Require(!EvaluateMission(expanded, world, 1, true).maestroEligible, "Todas las secundarias declaradas son necesarias aunque la principal esté completa");
    world.objects.pop_back();
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
    snapshot["agents"][0]["position"] = {105.f, 665.f}; snapshot["agents"][0]["elevation"] = 0.f; snapshot["agents"][0]["supportId"] = 5;
    Load(simulation, snapshot);
    ActionCommand interact; interact.interact = true;
    simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().phase == GamePhase::Mission && !simulation.GetState().agents[0].extracted, "No debe extraer antes del objetivo principal");
    snapshot = simulation.SnapshotFor(1); snapshot["agents"][0]["position"] = {570.f, 275.f}; snapshot["agents"][0]["elevation"] = 96.f; snapshot["agents"][0]["supportId"] = 10;
    Load(simulation, snapshot); simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().objectives == 1, "El terminal debe completar un objetivo real");
    simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().objectives == 1, "Un objetivo no debe puntuar dos veces");
    snapshot = simulation.SnapshotFor(1); snapshot["agents"][0]["position"] = {105.f, 665.f}; snapshot["agents"][0]["elevation"] = 0.f; snapshot["agents"][0]["supportId"] = 5;
    Load(simulation, snapshot); simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().phase == GamePhase::Results && simulation.GetState().results.at(1).success, "El bucle objetivo y extracción debe producir resultados");
    simulation.ReturnToBase();
    Require(IsTraining(simulation.GetState()), "Debe poder regresar a preparación en la sala segura");
    for (const auto kind : {ObjectKind::Operations, ObjectKind::Armory, ObjectKind::Intelligence, ObjectKind::Deployment})
      Require(std::ranges::count_if(simulation.GetState().objects, [kind](const Interactable &object) { return object.kind == kind; }) == 1,
        "Regresar a preparación debe conservar cada estación CIO junto al campo de entrenamiento");

    simulation.SetContract(ContractId::SilentRelay); simulation.Deploy(2);
    snapshot = simulation.SnapshotFor(1); snapshot["guards"] = nlohmann::json::array();
    for (auto &object : snapshot["objects"]) if (object["kind"] == static_cast<int>(ObjectKind::Camera)) object["active"] = false;
    snapshot["agents"][0]["position"] = {1150.f, 460.f}; snapshot["agents"][0]["elevation"] = 96.f; snapshot["agents"][0]["supportId"] = 11;
    Load(simulation, snapshot); simulation.Step(0.1f, {{1, interact}});
    Require(simulation.GetState().agents[0].puzzle.family == PuzzleFamily::Coordination, "El repetidor debe abrir controles de coordinación autoritativos");
    ActionCommand hold; hold.puzzleTarget = simulation.GetState().agents[0].puzzle.target; hold.puzzleHeld = true;
    Advance(simulation, hold, 21);
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
    snapshot["agents"][0]["position"] = {1150.f, 460.f}; snapshot["agents"][0]["elevation"] = 96.f; snapshot["agents"][0]["supportId"] = 11;
    snapshot["agents"][0]["privateOrder"] = static_cast<int>(PrivateOrder::CopyArchive);
    Load(simulation, snapshot);
    ActionCommand interact; interact.interact = true;
    simulation.Step(0.1f, {{1, interact}});
    simulation.DisconnectPlayer(2);
    ActionCommand hold; hold.puzzleTarget = simulation.GetState().agents[0].puzzle.target; hold.puzzleHeld = true;
    Advance(simulation, hold, 21);
    Require(simulation.GetState().objectives == 1, "La desconexión no debe bloquear el objeto cooperativo");

    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.SetContract(ContractId::DoubleCross); simulation.Deploy(0);
    auto &roles = const_cast<WorldState &>(simulation.GetState()).agents; roles[0].privateOrder = PrivateOrder::Traitor; roles[1].privateOrder = PrivateOrder::CopyArchive;
    ActionCommand lethal; lethal.fireLethal = true;
    auto towardOther = (simulation.GetState().agents[1].position - simulation.GetState().agents[0].position).normalized();
    lethal.aimX = towardOther.x; lethal.aimY = towardOther.y;
    AimHead(lethal, simulation.GetState().agents[0], simulation.GetState().agents[1].position, simulation.GetState().agents[1].facing);
    simulation.Step(0.1f, {{1, lethal}});
    Require(simulation.GetState().agents[1].incapacitated && simulation.GetState().phase == GamePhase::Mission, "El disparo fatal conserva la ventana de rescate individual antes de una eliminación real");
    Advance(simulation, {}, 551);
    Require(simulation.GetState().phase == GamePhase::Results && simulation.GetState().results.at(1).traitorVictory, "El traidor debe ganar solo después de eliminar al equipo sin alarma");
    Require(!simulation.GetState().agents[1].alive, "La victoria del traidor exige eliminación real");
    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.SetContract(ContractId::DoubleCross); simulation.Deploy(0);
    auto &nonLethalRoles = const_cast<WorldState &>(simulation.GetState()).agents; nonLethalRoles[0].privateOrder = PrivateOrder::Traitor; nonLethalRoles[1].privateOrder = PrivateOrder::CopyArchive;
    lethal.fireLethal = false; lethal.fireNonLethal = true;
    simulation.Step(0.1f, {{1, lethal}});
    Require(simulation.GetState().agents[1].alive && simulation.GetState().agents[1].incapacitated, "La fuerza no letal no debe contar como eliminación de un agente");
    Require(simulation.GetState().phase == GamePhase::Mission && simulation.GetState().results.empty(), "La incapacitación no cumple la orden de eliminación mientras la víctima siga viva");
  }

  void DisconnectNeutralizesMovement() {
    Simulation simulation;
    simulation.ResetBase({{1, "Anfitrión"}, {2, "Cliente"}});
    simulation.Deploy(0);
    auto &roles = const_cast<WorldState &>(simulation.GetState()).agents;
    roles[0].privateOrder = PrivateOrder::CopyArchive; roles[1].privateOrder = PrivateOrder::Traitor;
    const auto initial = simulation.GetState().agents[1].position;
    ActionCommand moving;
    moving.moveX = 1.f; moving.run = true; moving.aiming = true; moving.aimX = 1.f;
    simulation.Step(0.1f, {{2, moving}});
    const auto &active = simulation.GetState().agents[1];
    Require(active.moving && active.running && active.aiming && (active.position - initial).length() > 1.f,
        "El cliente debe mantener movimiento, carrera y apuntado reales antes del corte");
    const auto stopped = active.position;
    const auto tick = simulation.GetState().tick;
    simulation.DisconnectPlayer(2);
    const auto &disconnected = simulation.GetState().agents[1];
    Require(!disconnected.connected && !disconnected.moving && !disconnected.running && !disconnected.aiming,
        "Desconectar debe neutralizar los indicadores de movimiento, carrera y apuntado");
    for (int frame = 0; frame < 30; ++frame) simulation.Step(1.f / 60.f, {{2, moving}});
    const auto &world = simulation.GetState();
    Require(world.tick > tick && world.phase == GamePhase::Mission && world.agents.front().connected,
        "El anfitrión debe continuar simulando la misión tras desconectar al cliente activo");
    Require(world.agents[1].position == stopped && !world.agents[1].moving && !world.agents[1].running && !world.agents[1].aiming,
        "Los mandos residuales no deben mover ni reactivar al cliente desconectado");
    simulation.DisconnectPlayer(2);
    Require(std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::Disconnected && event.actor == 2; }) == 1,
        "Un corte repetido no debe duplicar el evento de desconexión");
  }

  void SeedReachability() {
    struct Node { int x; int y; float elevation; std::uint32_t support; };
    std::string previousLayout; std::unordered_set<std::uint64_t> visited; std::vector<Node> reached;
    for (std::uint32_t seed = 0; seed < 36; ++seed) {
      Simulation simulation;
      simulation.ResetBase({{1, "Uno"}, {2, "Dos"}, {3, "Tres"}, {4, "Cuatro"}}); simulation.Deploy(seed);
      const auto &world = simulation.GetState();
      const int columns = static_cast<int>(std::ceil(world.size.x / 20.f)), rows = static_cast<int>(std::ceil(world.size.y / 20.f));
      const auto key = [columns, rows](int x, int y, std::uint32_t support) { return static_cast<std::uint64_t>(support) * columns * rows + y * columns + x; };
      const auto point = [](int x, int y) { return sf::Vector2f{10.f + x * 20.f, 10.f + y * 20.f}; };
      const auto &insertion = world.agents.front();
      const int sx = static_cast<int>(insertion.position.x / 20.f), sy = static_cast<int>(insertion.position.y / 20.f);
      auto furniture = nlohmann::json::array();
      for (const auto &object : world.objects) if (WorldGeometry::BlocksMovement(object)) {
        const auto bounds = WorldGeometry::ObjectFootprint(object);
        furniture.push_back({object.id, bounds.position.x, bounds.position.y, bounds.size.x, bounds.size.y, object.elevation, WorldGeometry::ObjectHeight(object)});
      }
      const auto layout = Level::GeometryHash(world) + furniture.dump() + std::to_string(insertion.position.x) + std::to_string(insertion.position.y) + std::to_string(insertion.elevation) + std::to_string(insertion.supportId);
      // Las semillas comparten búsqueda sólo cuando toda su geometría física y su inserción son idénticas.
      if (layout != previousLayout) {
        previousLayout = layout; visited.clear(); reached.clear(); std::queue<Node> pending;
        pending.push({sx, sy, insertion.elevation, insertion.supportId}); visited.insert(key(sx, sy, insertion.supportId));
        while (!pending.empty()) {
          const auto current = pending.front(); pending.pop(); reached.push_back(current);
          for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
            const int nx = current.x + dx, ny = current.y + dy;
            if (nx < 0 || ny < 0 || nx >= columns || ny >= rows) continue;
            auto position = point(current.x, current.y); auto elevation = current.elevation; auto support = current.support;
            const auto target = point(nx, ny);
            WorldGeometry::MoveOnSupports(world, position, elevation, support, target - position, GameplayConfig::AgentRadius, CombatConfig::StandingHeight);
            if ((position - target).length() > 0.01f || !visited.insert(key(nx, ny, support)).second) continue;
            pending.push({nx, ny, elevation, support});
          }
        }
      }
      for (const auto &object : world.objects) if (object.kind == ObjectKind::Terminal || object.kind == ObjectKind::Secondary || object.kind == ObjectKind::CoopSwitch || object.kind == ObjectKind::Extraction) {
        Require(std::ranges::any_of(reached, [&](const Node &node) { return std::abs(node.elevation - object.elevation) <= LevelConfig::MaximumStepHeight &&
          WorldGeometry::DistanceToBounds(point(node.x, node.y), WorldGeometry::ObjectFootprint(object)) < GameplayConfig::InteractionDistance; }), "Toda semilla necesita acceso físico3D a objetivos y extracción");
      }
      for (const auto &agent : world.agents) Require(visited.contains(key(static_cast<int>(agent.position.x / 20.f), static_cast<int>(agent.position.y / 20.f), agent.supportId)), "Los cuatro puntos de inserción deben compartir un corredor conectado");
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
          Require(!WorldGeometry::PositionBlocked(world, agent.position, radius, agent.elevation, WorldGeometry::ActorHeight(agent), agent.supportId), "La inserción no debe intersectar muros, muebles ni plantas sólidas");
          for (const auto &other : world.agents) if (other.id != agent.id) Require(WorldGeometry::SpatialDistance(WorldGeometry::Feet(agent), WorldGeometry::Feet(other)) >= radius * 2.f, "Los agentes no deben aparecer superpuestos");
          for (const auto &guard : world.guards) for (std::size_t index = 0; index < guard.patrol.size(); ++index) {
            const auto start = guard.patrol[index], end = guard.patrol[(index + 1) % guard.patrol.size()];
            const auto segment = end - start;
            const float t = std::clamp((agent.position - start).dot(segment) / segment.lengthSquared(), 0.f, 1.f);
            const auto observer = start + segment * t, toward = agent.position - observer;
            const float startZ = guard.patrolElevations.empty() ? guard.elevation : guard.patrolElevations[index];
            const float endZ = guard.patrolElevations.empty() ? guard.elevation : guard.patrolElevations[(index + 1) % guard.patrol.size()];
            Require(toward.length() > GameplayConfig::VisionDistance || !simulation.CanSee(observer, toward.length() > 0.f ? toward.normalized() : sf::Vector2f{1.f, 0.f}, agent, GameplayConfig::VisionDistance, startZ + (endZ - startZ) * t), "Ningún tramo de patrulla debe observar la inserción de pie a través de su planta");
          }
        }
        for (const auto &guard : world.guards) for (std::size_t index = 0; index < guard.patrol.size(); ++index) {
          const auto position = guard.patrol[index];
          const float z = guard.patrolElevations.empty() ? guard.elevation : guard.patrolElevations[index];
          Require(!WorldGeometry::PositionBlocked(world, position, GameplayConfig::AgentRadius, z, CombatConfig::StandingHeight, guard.supportId), "Una patrulla militar aparece dentro de un sólido de su planta");
        }
        for (int frame = 0; frame < 720; ++frame) simulation.Step(1.f / 60.f, {});
        Require(simulation.GetState().phase == GamePhase::Mission, "Una misión no debe terminar mientras el equipo se orienta en la inserción");
        Require(simulation.GetState().alarmCount == 0, "La inserción inmóvil no debe activar alarmas");
        Require(std::ranges::all_of(simulation.GetState().agents, [](const Agent &agent) { return agent.alive && !agent.incapacitated && agent.stance == Stance::Standing; }), "Los cuatro agentes de pie deben seguir operativos tras doce segundos");
        Require(std::ranges::none_of(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection || event.kind == EventKind::AgentKilled; }), "La espera inicial no debe registrar detecciones ni bajas");
      }
    }
  }

  std::vector<sf::Vector2f> PlanRoute(const Simulation &simulation, const Interactable &target, bool stealth) {
    const auto &world = simulation.GetState();
    const int columns = static_cast<int>(std::ceil(world.size.x / 20.f)), rows = static_cast<int>(std::ceil(world.size.y / 20.f));
    const auto &actor = world.agents.front();
    const auto *currentSupport = WorldGeometry::FindSurface(world, actor.supportId);
    const WalkSurface *nextSupport = nullptr;
    bool intermediate = false; sf::Vector2f intermediatePoint; float intermediateReach = 20.f;
    if (currentSupport && actor.supportId != target.supportId) {
      auto navigation = world;
      // La infiltración limpia accede al archivo por la escalera este y el puente del ala de control.
      if (stealth) for (auto &surface : navigation.walkSurfaces) if (surface.id == 2) surface.enabled = false;
      const auto route = Navigation::Route(navigation, actor.supportId, target.supportId);
      if (route.empty()) return {};
      const auto &step = route.front(); nextSupport = WorldGeometry::FindSurface(world, step.toSupport); intermediate = true;
      if (stealth && actor.supportId == 10)
        std::cout << "Enlace sigiloso " << step.fromSupport << "->" << step.toSupport << " destino=" << target.supportId << " entrada=" << step.entry.x << ',' << step.entry.y << " salida=" << step.exit.x << ',' << step.exit.y << '\n';
      auto entry = WorldGeometry::Ground(step.entry), exit = WorldGeometry::Ground(step.exit);
      if (step.linkId == 0 && nextSupport && currentSupport->slope.lengthSquared() == 0.f && nextSupport->slope.lengthSquared() == 0.f) {
        const auto firstEnd = currentSupport->bounds.position + currentSupport->bounds.size, nextEnd = nextSupport->bounds.position + nextSupport->bounds.size;
        const sf::Vector2f minimum{std::max(currentSupport->bounds.position.x, nextSupport->bounds.position.x), std::max(currentSupport->bounds.position.y, nextSupport->bounds.position.y)};
        const sf::Vector2f maximum{std::min(firstEnd.x, nextEnd.x), std::min(firstEnd.y, nextEnd.y)};
        const auto crossing = WorldGeometry::ClosestPoint(actor.position, {minimum, maximum - minimum});
        const auto inward = nextSupport->bounds.position + nextSupport->bounds.size * 0.5f - crossing;
        const auto beyond = crossing + (inward.lengthSquared() > 0.f ? inward.normalized() * std::min(GameplayConfig::AgentRadius, inward.length()) : sf::Vector2f{});
        if (WorldGeometry::ClearBodySegment(world, {crossing.x, crossing.y, actor.elevation}, {beyond.x, beyond.y, actor.elevation}, GameplayConfig::AgentRadius, stealth ? CombatConfig::ProneHeight : CombatConfig::CrouchedHeight, actor.supportId)) { entry = crossing; exit = beyond; }
      }
      if (const auto link = std::ranges::find(world.verticalLinks, step.linkId, &VerticalLink::id); link != world.verticalLinks.end())
        intermediateReach = std::max(20.f, link->width * 0.5f - GameplayConfig::AgentRadius);
      intermediatePoint = (actor.position - entry).length() <= intermediateReach ? exit : entry;
    }
    const auto supportAt = [&](sf::Vector2f point) -> const WalkSurface * {
      if (nextSupport && nextSupport->kind == WalkSurfaceKind::Stair && WorldGeometry::ContainsSupport(*nextSupport, point) &&
          (!currentSupport || WorldGeometry::SurfaceHeight(*nextSupport, point) > WorldGeometry::SurfaceHeight(*currentSupport, point) + LevelConfig::SupportTolerance)) return nextSupport;
      if (currentSupport && WorldGeometry::ContainsSupport(*currentSupport, point)) return currentSupport;
      if (nextSupport && WorldGeometry::ContainsSupport(*nextSupport, point)) return nextSupport;
      return nullptr;
    };
    const auto blocked = [&world, &simulation, stealth, &actor, &supportAt, currentSupport](sf::Vector2f point) {
      if (point.x < 12.f || point.y < 12.f || point.x > world.size.x - 12.f || point.y > world.size.y - 12.f) return true;
      const auto *support = supportAt(point);
      if (currentSupport && !support) return true;
      const float elevation = support ? WorldGeometry::SurfaceHeight(*support, point) : 0.f;
      if (WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius + 0.5f, elevation, stealth ? CombatConfig::ProneHeight : CombatConfig::CrouchedHeight, support ? support->id : 0)) return true;
      for (const auto &object : world.objects) {
        if (std::abs(object.elevation - elevation) > GameplayConfig::InteractionDistance) continue;
        if (object.kind == ObjectKind::Explosive && object.active && object.trap.state != TrapState::Disarmed && object.trap.state != TrapState::Detonated &&
            (point - object.position).length() <= object.trap.triggerRadius + GameplayConfig::AgentRadius && Visibility::HasLineOfSight(world, object.position, point, Stance::Standing, object.id)) return true;
        if (object.kind == ObjectKind::Camera && object.active && object.camera.state == CameraState::Operating) {
          const auto delta = point - object.position;
          const float distance = delta.length();
          const auto stance = stealth ? Stance::Prone : Stance::Crouched;
          const float posture = stealth ? GameplayConfig::ProneVisionFactor : GameplayConfig::CrouchedVisionFactor;
          if (distance > object.camera.range * posture + 25.f || !Visibility::HasLineOfSight(world, object.position, point, stance, object.id)) continue;
          if (distance <= GameplayConfig::CloseVisionDistance) return true;
          if (object.camera.sweeping) {
            const float angle = std::remainder(std::atan2(delta.y, delta.x) - object.camera.baseAngle, 6.2831853f);
            const float nearest = std::clamp(angle, object.camera.minAngle, object.camera.maxAngle);
            if (std::cos(angle - nearest) >= object.camera.cosHalfAngle) return true;
          } else if (object.facing.dot(delta) >= object.camera.cosHalfAngle * distance * object.facing.length()) return true;
        }
      }
      if (stealth) for (const auto &guard : world.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated) {
        if (std::abs(guard.elevation - elevation) > CombatConfig::StandingHeight) continue;
        Agent candidate; candidate.position = point; candidate.elevation = elevation; candidate.stance = Stance::Prone;
        const float currentDistance = WorldGeometry::SpatialDistance(WorldGeometry::Feet(guard), WorldGeometry::Feet(actor));
        const float plannedDistance = WorldGeometry::SpatialDistance(WorldGeometry::Feet(guard), WorldGeometry::Feet(candidate));
        const auto toward = candidate.position - guard.position;
        if (plannedDistance < std::min(GameplayConfig::VisionDistance * GameplayConfig::ProneVisionFactor + GameplayConfig::AgentRadius, currentDistance - 1.f) &&
            simulation.CanSee(guard.position, toward, candidate, GameplayConfig::VisionDistance + GameplayConfig::AgentRadius * 2.f, guard.elevation)) return true;
      }
      return false;
    };
    const auto position = world.agents.front().position;
    const int sx = std::clamp(static_cast<int>(position.x / 20.f), 0, columns - 1), sy = std::clamp(static_cast<int>(position.y / 20.f), 0, rows - 1);
    const int start = sy * columns + sx;
    std::vector<int> previous(static_cast<std::size_t>(columns * rows), -1); previous[start] = start;
    std::queue<int> queue; queue.push(start);
    int finish = -1;
    while (!queue.empty()) {
      const int current = queue.front(); queue.pop();
      const int x = current % columns, y = current / columns;
      const sf::Vector2f point{10.f + x * 20.f, 10.f + y * 20.f};
      const auto closest = WorldGeometry::ClosestPoint(point, WorldGeometry::ObjectFootprint(target));
      // El archivo se opera desde su cara este sin cruzar la ronda del vigilante.
      const bool approachSide = !stealth || target.kind != ObjectKind::Terminal || (point.x >= target.position.x + 40.f && point.y <= target.position.y - 30.f);
      const bool interaction = approachSide && (point - closest).length() < GameplayConfig::InteractionDistance - GameplayConfig::AgentRadius &&
          (target.kind != ObjectKind::CoopSwitch || (point - target.position).length() < GameplayConfig::InteractionDistance - 1.f);
      const auto *pointSupport = supportAt(point); const float pointZ = pointSupport ? WorldGeometry::SurfaceHeight(*pointSupport, point) : 0.f;
      if (!blocked(point) && (intermediate ? (point - intermediatePoint).length() <= intermediateReach :
          interaction && std::abs(pointZ - target.elevation) <= LevelConfig::MaximumStepHeight && !WorldGeometry::RayOccluded(world, {point.x, point.y, pointZ + CombatConfig::StandingWeaponHeight}, {closest.x, closest.y, target.elevation + CombatConfig::StandingWeaponHeight}, target.id))) { finish = current; break; }
      for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        const int nx = x + dx, ny = y + dy, next = ny * columns + nx;
        const sf::Vector2f nextPoint{10.f + nx * 20.f, 10.f + ny * 20.f};
        if (nx < 0 || ny < 0 || nx >= columns || ny >= rows || previous[next] != -1 || blocked(nextPoint)) continue;
        if (currentSupport) {
          auto moved = point; auto z = pointZ; auto support = pointSupport ? pointSupport->id : 0;
          WorldGeometry::MoveOnSupports(world, moved, z, support, nextPoint - point, GameplayConfig::AgentRadius, stealth ? CombatConfig::ProneHeight : CombatConfig::CrouchedHeight);
          const auto *expected = supportAt(nextPoint);
          if ((moved - nextPoint).length() > 0.01f || !expected || std::abs(z - WorldGeometry::SurfaceHeight(*expected, nextPoint)) > LevelConfig::SupportTolerance) continue;
        }
        previous[next] = current; queue.push(next);
      }
    }
    std::vector<sf::Vector2f> route;
    if (finish < 0) {
      if (!stealth) {
        const auto bounds = WorldGeometry::ObjectFootprint(target);
        std::cout << "Sin ruta huella=" << bounds.position.x << ',' << bounds.position.y << '/' << bounds.size.x << ',' << bounds.size.y << '\n';
        const int tx = static_cast<int>(target.position.x / 20.f), ty = static_cast<int>(target.position.y / 20.f);
        for (int y = std::max(0, ty - 3); y <= std::min(rows - 1, ty + 3); ++y) for (int x = std::max(0, tx - 3); x <= std::min(columns - 1, tx + 3); ++x) {
          const sf::Vector2f point{10.f + x * 20.f, 10.f + y * 20.f}; const auto closest = WorldGeometry::ClosestPoint(point, bounds);
          if ((point - closest).length() < GameplayConfig::InteractionDistance - GameplayConfig::AgentRadius)
            std::cout << "Candidato " << point.x << ',' << point.y << " bloqueado=" << blocked(point) << " alcanzado=" << (previous[y * columns + x] >= 0) << " los=" << !WorldGeometry::RayOccluded(world, {point.x, point.y, target.elevation + CombatConfig::StandingWeaponHeight}, {closest.x, closest.y, target.elevation + CombatConfig::StandingWeaponHeight}, target.id) << '\n';
        }
      }
      if (!stealth) throw std::runtime_error("Buscador sin ruta: objeto=" + std::to_string(target.id) + " apoyo=" + std::to_string(actor.supportId) + " destino=" + std::to_string(target.supportId) +
        " posición=" + std::to_string(actor.position.x) + "," + std::to_string(actor.position.y) + "," + std::to_string(actor.elevation) + " nodos=" + std::to_string(std::ranges::count_if(previous, [](int value) { return value >= 0; })));
      return route;
    }
    for (int index = finish; index != start; index = previous[index]) route.push_back({10.f + index % columns * 20.f, 10.f + index / columns * 20.f});
    if (route.empty()) route.push_back({10.f + finish % columns * 20.f, 10.f + finish / columns * 20.f});
    std::ranges::reverse(route);
    if (intermediate) route.push_back(intermediatePoint);
    return route;
  }

  void ReachAndInteract(Simulation &simulation, ObjectKind kind, bool stealth) {
    const auto target = std::ranges::find_if(simulation.GetState().objects, [kind](const Interactable &object) { return object.kind == kind; });
    Require(target != simulation.GetState().objects.end(), "El escenario debe contener el objeto solicitado");
    std::vector<sf::Vector2f> route;
    // La retirada del terminal conserva su lado este hasta cruzar el acceso del puente.
    if (stealth && kind != ObjectKind::Terminal && simulation.GetState().agents.front().supportId == 10)
      route = {{650.f, 210.f}, {650.f, 250.f}, {690.f, 250.f}};
    unsigned int routeSearches = 0;
    constexpr float dt = 1.f / 30.f;
    for (int frame = 0; frame < 9000; ++frame) {
      const auto &state = simulation.GetState();
      if (state.phase != GamePhase::Mission) throw std::runtime_error("Operación terminada hacia " + std::to_string(static_cast<int>(kind)) + " seed=" + std::to_string(state.seed) + " contrato=" + std::to_string(static_cast<int>(state.contract)) + " en " + std::to_string(state.agents.front().position.x) + "," + std::to_string(state.agents.front().position.y) + " t=" + std::to_string(state.elapsed) + ": " + state.message);
      const auto &agent = state.agents.front();
      if (frame % 900 == 0 || (stealth && kind == ObjectKind::Secondary && frame % 60 == 0 && agent.supportId == 10)) {
        const auto next = route.empty() ? agent.position : route.front();
        std::cout << "RUTA objeto=" << target->id << " frame=" << frame << " pos=" << agent.position.x << ',' << agent.position.y << ',' << agent.elevation << " apoyo=" << agent.supportId << " puntos=" << route.size() << " siguiente=" << next.x << ',' << next.y << " salud=" << agent.health << " vida=" << static_cast<int>(agent.lifeState) << " incapacidad=" << agent.incapacityRemaining << " t=" << state.elapsed << std::endl;
        if (stealth && kind == ObjectKind::Secondary && agent.supportId == 10) for (const auto &guard : state.guards)
          std::cout << "  Guardia=" << guard.id << " pos=" << guard.position.x << ',' << guard.position.y << ',' << guard.elevation << " estado=" << static_cast<int>(guard.state) << " frente=" << guard.facing.x << ',' << guard.facing.y << " sospecha=" << guard.suspicion << '\n';
      }
      ActionCommand command;
      if (stealth && agent.stance != Stance::Prone) command.prone = true;
      if (!stealth && agent.stance != Stance::Crouched) command.crouch = true;
      if (!stealth) {
        command.weaponChoice = 1;
        command.reloadWeapon = agent.nonLethalMagazine == 0 && agent.loadout.nonLethalAmmo > 0 && agent.reloadRemaining <= 0.f;
      }
      command.crawl = stealth;
      const auto closest = WorldGeometry::ClosestPoint(agent.position, WorldGeometry::ObjectFootprint(*target));
      const auto toward = closest - agent.position;
      const bool approachSide = !stealth || kind != ObjectKind::Terminal || (agent.position.x >= target->position.x + 40.f && agent.position.y <= target->position.y - 30.f);
      if (approachSide && std::abs(agent.elevation - target->elevation) <= LevelConfig::MaximumStepHeight && toward.length() < GameplayConfig::InteractionDistance - GameplayConfig::AgentRadius &&
          (kind != ObjectKind::CoopSwitch || (agent.position - target->position).length() < GameplayConfig::InteractionDistance - 1.f) &&
          !WorldGeometry::RayOccluded(state, {agent.position.x, agent.position.y, agent.elevation + CombatConfig::StandingWeaponHeight}, {closest.x, closest.y, target->elevation + CombatConfig::StandingWeaponHeight}, target->id)) {
        if (agent.puzzle.target == target->id && agent.puzzle.family == PuzzleFamily::Coordination) { command.puzzleTarget = target->id; command.puzzleHeld = true; }
        else if (kind == ObjectKind::AlarmPanel || kind == ObjectKind::Camera) command.tool = true;
        else command.interact = true;
        simulation.Step(dt, {{1, command}});
        if (target->completed || ((kind == ObjectKind::AlarmPanel || kind == ObjectKind::Camera) && !target->active) || simulation.GetState().agents.front().extracted || simulation.GetState().phase == GamePhase::Results) {
          if (stealth) std::cout << "Planificación sigilosa objeto=" << target->id << " búsquedas=" << routeSearches << std::endl;
          return;
        }
        continue;
      }
      const bool routeThreatened = stealth && std::ranges::any_of(state.guards, [&](const Guard &guard) {
        if (guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || std::abs(guard.elevation - agent.elevation) > CombatConfig::StandingHeight) return false;
        return std::any_of(route.begin(), route.begin() + std::min<std::size_t>(route.size(), 3), [&](sf::Vector2f point) {
          auto candidate = agent; candidate.position = point;
          if (const auto support = WorldGeometry::FindSupport(state, point, agent.elevation, agent.supportId, GameplayConfig::AgentRadius)) candidate.elevation = support->elevation;
          const float currentDistance = WorldGeometry::SpatialDistance(WorldGeometry::Feet(guard), WorldGeometry::Feet(agent));
          const float plannedDistance = WorldGeometry::SpatialDistance(WorldGeometry::Feet(guard), WorldGeometry::Feet(candidate));
          return plannedDistance < std::min(GameplayConfig::VisionDistance * GameplayConfig::ProneVisionFactor + GameplayConfig::AgentRadius, currentDistance - 1.f) &&
            simulation.CanSee(guard.position, candidate.position - guard.position, candidate, GameplayConfig::VisionDistance + GameplayConfig::AgentRadius * 2.f, guard.elevation);
        });
      });
      if ((!stealth && route.empty()) || (stealth && frame % 6 == 0 && (route.empty() || routeThreatened))) {
        route = PlanRoute(simulation, *target, stealth); ++routeSearches;
      }
      while (!route.empty() && (route.front() - agent.position).length() < 0.25f) route.erase(route.begin());
      if (!route.empty()) {
        const auto offset = route.front() - agent.position;
        const float stepDistance = (stealth ? GameplayConfig::CrawlSpeed : GameplayConfig::CrouchSpeed) * dt;
        const auto direction = offset / std::max(offset.length(), stepDistance);
        command.moveX = direction.x; command.moveY = direction.y;
      }
      if (!stealth) {
        const Guard *nearest = nullptr;
        float distance = GameplayConfig::WeaponRange;
        Agent aimingAgent = agent;
        if (command.crouch) aimingAgent.stance = Stance::Crouched;
        for (const auto &guard : state.guards) if (guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated) {
          Agent candidate; candidate.position = guard.position; candidate.elevation = guard.elevation;
          const auto offset = guard.position - agent.position;
          if (offset.length() < 0.1f || offset.length() >= distance || !simulation.CanSee(agent.position, offset.normalized(), candidate, GameplayConfig::WeaponRange, agent.elevation)) continue;
          ActionCommand aim; AimHead(aim, aimingAgent, guard.position, guard.facing, Stance::Standing, guard.elevation);
          const sf::Vector2f direction = sf::Vector2f{aim.aimX, aim.aimY}.normalized();
          const float height = aimingAgent.elevation + CombatGeometry::WeaponHeight(aimingAgent.stance);
          const auto hit = CombatGeometry::IntersectActor(agent.position, direction, height, aim.aimSlope, GameplayConfig::WeaponRange, guard.position, guard.facing, Stance::Standing, false, guard.elevation);
          if (!hit || hit->zone != HitZone::Head) continue;
          const bool blocked = WorldGeometry::RayOccluded(state, {agent.position.x, agent.position.y, height}, {guard.position.x, guard.position.y, hit->height});
          if (!blocked) { nearest = &guard; distance = offset.length(); }
        }
        if (nearest) {
          command.fireNonLethal = agent.nonLethalMagazine > 0 && agent.reloadRemaining <= 0.f && !command.reloadWeapon;
          AimHead(command, aimingAgent, nearest->position, nearest->facing, Stance::Standing, nearest->elevation);
        }
      } else if (route.empty() && agent.loadout.decoys > 0 && agent.actionCooldown <= 0.f) {
        for (const auto &guard : state.guards) if ((guard.state == GuardState::Patrol || guard.state == GuardState::Suspicion) && (guard.position - agent.position).length() < 210.f) {
          const auto towardGuard = guard.position - agent.position;
          const auto forward = towardGuard.normalized();
          const sf::Vector2f lateral{-forward.y, forward.x};
          float bestScore = 0.f;
          sf::Vector2f best;
          for (float side : {-1.f, -0.5f, 0.f, 0.5f, 1.f}) {
            const auto desired = guard.position + forward * 110.f + lateral * (side * 150.f);
            const auto offset = desired - agent.position;
            const auto path = DistractionGeometry::TraceDecoy(state, agent.position, offset, offset.length(), agent.elevation);
            const auto diversion = path.to - guard.position;
            if (!path.valid || diversion.length() > DistractionConfig::DecoyHearingDistance || diversion.dot(forward) < 55.f) continue;
            float separation = (path.to - agent.position).length();
            for (std::size_t point = 0; point < std::min<std::size_t>(route.size(), 12); ++point) separation = std::min(separation, (path.to - route[point]).length());
            if (separation > bestScore) { bestScore = separation; best = path.to; }
          }
          if (bestScore > 150.f) {
            const auto offset = best - agent.position;
            command.throwDecoy = true; command.decoyDistance = offset.length();
            command.aimX = offset.x; command.aimY = offset.y;
            break;
          }
        }
      }
      simulation.Step(dt, {{1, command}});
    }
    const auto &stuck = simulation.GetState();
    throw std::runtime_error("No se pudo llegar mediante controles al objeto " + std::to_string(static_cast<int>(kind)) + " seed=" + std::to_string(stuck.seed) + " contrato=" + std::to_string(static_cast<int>(stuck.contract)) + " posicion=" + std::to_string(stuck.agents[0].position.x) + "," + std::to_string(stuck.agents[0].position.y) + " t=" + std::to_string(stuck.elapsed) + " ruta=" + std::to_string(route.size()) + "; " + stuck.message);
  }

  void CompleteOperationWithoutStateShortcuts() {
    for (std::uint32_t seed : {0u, 1u, 2u, 5u}) {
      for (auto contract : {ContractId::GhostArchive, ContractId::SilentRelay, ContractId::DoubleCross}) {
        const auto started = std::chrono::steady_clock::now();
        std::cout << "RUN operación: semilla=" << seed << " contrato=" << static_cast<int>(contract) << std::endl;
        Simulation simulation;
        simulation.ResetBase();
        simulation.SetContract(contract);
        simulation.SetLoadout(1, {0, 12, 6, 6});
        simulation.Deploy(seed);
        ReachAndInteract(simulation, ObjectKind::AlarmPanel, false);
        ReachAndInteract(simulation, ObjectKind::Terminal, false);
        ReachAndInteract(simulation, ObjectKind::Secondary, false);
        ReachAndInteract(simulation, ObjectKind::CoopSwitch, false);
        ReachAndInteract(simulation, ObjectKind::Extraction, false);
        Require(simulation.GetState().phase == GamePhase::Results && simulation.GetState().results.at(1).success, "El contrato completo debe poder terminarse con controles reales");
        Require(simulation.GetState().secondary >= 1, "La ruta debe completar también el objetivo secundario");
        Require(!simulation.GetState().results.at(1).maestroEligible, "Una operación que usa armas no puede lograr Maestro");
        simulation.ReturnToBase();
        Require(simulation.GetState().phase == GamePhase::Base, "El ciclo completo debe volver a base");
        std::cout << "PASS operación: " << std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count() << " s" << std::endl;
      }
    }
    Simulation clean;
    clean.ResetBase(); clean.SetLoadout(1, {0, 0, 6, 6}); clean.Deploy(0);
    ActionCommand approach; approach.moveX = 1.f; approach.moveY = 0.25f; approach.run = true;
    Advance(clean, approach, 7, 1.f / 30.f);
    ReachAndInteract(clean, ObjectKind::AlarmPanel, true);
    ReachAndInteract(clean, ObjectKind::Terminal, true);
    ReachAndInteract(clean, ObjectKind::Secondary, true);
    ReachAndInteract(clean, ObjectKind::Extraction, true);
    Require(clean.GetState().results.at(1).rank == Rank::Maestro, "Una infiltración real limpia debe lograr Maestro");
    Require(clean.GetState().events.size() >= 2, "La misión limpia debe conservar registro real de objetivos y extracción");
  }
}

int main(int argc, char **argv) {
  try {
    Require(argc == 1 || (argc == 3 && std::string(argv[1]) == "--case"), "Uso: NoTraceSimulationTests [--case nombre]");
    const std::string selected = argc == 3 ? argv[2] : "";
    bool ran = false;
    const auto run = [&](const char *name, auto test) {
      if (!selected.empty() && selected != name) return;
      ran = true; currentCase = name;
      const auto started = std::chrono::steady_clock::now();
      std::cout << "RUN simulación: " << name << std::endl;
      test();
      std::cout << "PASS " << name << ": " << std::chrono::duration<float>(std::chrono::steady_clock::now() - started).count() << " s" << std::endl;
    };
    run("MovementAndBase", MovementAndBase);
    run("MovementOwnsBodyFacing", MovementOwnsBodyFacing);
    run("PerceptionAndNoise", PerceptionAndNoise);
    run("CombatAndTools", CombatAndTools);
    run("WeaponTrajectoriesAndVitals", WeaponTrajectoriesAndVitals);
    run("BallisticHeightRoundTrip", BallisticHeightRoundTrip);
    run("GuardAttacksAndRecovery", GuardAttacksAndRecovery);
    run("AtomicLifeAndExplicitRescue", AtomicLifeAndExplicitRescue);
    run("RecoveryPresentationState", [] { checks += EngineTests::RunRecoverySimulationFixtures(); });
    run("ScoringAndObjectives", ScoringAndObjectives);
    run("PrivacyAndDisconnect", PrivacyAndDisconnect);
    run("DisconnectNeutralizesMovement", DisconnectNeutralizesMovement);
    run("SeedReachability", SeedReachability);
    run("SafeInsertion", SafeInsertion);
    run("CompleteOperationWithoutStateShortcuts", CompleteOperationWithoutStateShortcuts);
    Require(ran, "No existe el caso de simulación solicitado");
    std::cout << "NO TRACE: " << checks << " comprobaciones de simulación correctas.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NO TRACE: fallo en " << currentCase << " tras " << checks << " comprobaciones: " << error.what() << '\n';
    return 1;
  }
}
