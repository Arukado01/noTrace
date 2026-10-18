#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceWorldGeometry.h"
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  using Json = nlohmann::json;
  unsigned int checks = 0;
  void Require(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
  void Load(Simulation &simulation, const Json &snapshot) {
    std::string error;
    if (!simulation.LoadSnapshot(snapshot, error)) throw std::runtime_error("Fixture escondite: " + error);
  }
  const Agent &AgentAt(const Simulation &simulation, std::size_t index = 0) { return simulation.GetState().agents.at(index); }
  void Step(Simulation &simulation, ActionCommand command = {}, unsigned int frames = 1) {
    for (unsigned int frame = 0; frame < frames; ++frame) simulation.Step(0.1f, {{1, command}});
  }
  Json Empty(Simulation &simulation, sf::Vector2f position = {150.f, 180.f}, bool second = false) {
    simulation.ResetBase(second ? std::vector<PlayerProfile>{{1, "Uno"}, {2, "Dos"}} : std::vector<PlayerProfile>{{1, "Uno"}});
    auto snapshot = simulation.SnapshotFor(1);
    TestFixture::LegacyPlane(snapshot);
    snapshot["mode"] = static_cast<int>(SimulationMode::Operation);
    snapshot["objects"] = Json::array(); snapshot["obstacles"] = Json::array(); snapshot["guards"] = Json::array(); snapshot["events"] = Json::array();
    for (auto &agent : snapshot["agents"]) agent["position"] = {position.x, position.y};
    return snapshot;
  }
  Json Hideout(HideoutKind kind) {
    Simulation donor; donor.ResetBase(); auto snapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(snapshot); auto value = snapshot["objects"][0];
    value["id"] = 77; value["kind"] = static_cast<int>(ObjectKind::Hideout); value["position"] = {210.f, 180.f};
    value["hideoutKind"] = static_cast<int>(kind); value["occupantAgent"] = 0; value["occupantGuard"] = 0; value["footprint"] = {0.f, 0.f};
    return value;
  }
  Json Body(std::uint32_t id = 10, sf::Vector2f position = {170.f, 180.f}) {
    Simulation donor; donor.ResetBase(); donor.Deploy(0); auto snapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(snapshot); auto value = snapshot["guards"][0];
    value["id"] = id; value["position"] = {position.x, position.y}; value["state"] = static_cast<int>(GuardState::Incapacitated);
    value["health"] = CombatConfig::MaxGuardHealth; value["stun"] = GameplayConfig::MaxStamina; value["incapacityRemaining"] = GameplayConfig::GuardIncapacityDuration;
    value["bodyHidden"] = false; value["storedIn"] = 0; value["patrol"] = Json::array(); value["waypoint"] = 0;
    return value;
  }
  Json Wall(sf::FloatRect bounds) {
    Simulation donor; donor.ResetBase(); auto snapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(snapshot); auto value = snapshot["obstacles"][0];
    value["position"] = {bounds.position.x, bounds.position.y}; value["size"] = {bounds.size.x, bounds.size.y}; return value;
  }
  void AgentHideouts() {
    for (const auto kind : {HideoutKind::TrashBin, HideoutKind::Closet, HideoutKind::UnderTable}) {
      Simulation simulation;
      auto snapshot = Empty(simulation, {150.f, 180.f}, true); snapshot["objects"].push_back(Hideout(kind)); Load(simulation, snapshot);
      const auto entry = AgentAt(simulation).position;
      ActionCommand use; use.interact = true; use.aiming = true; Step(simulation, use);
      Require(AgentAt(simulation).hidden && AgentAt(simulation).hideoutId == 77 && simulation.GetState().objects[0].occupantAgent == 1, "El agente no ocupa su escondite");
      Require(!AgentAt(simulation).aiming, "Entrar en un escondite conserva el apuntado exterior del agente");
      const auto hiddenPosition = AgentAt(simulation).position;
      ActionCommand invalid; invalid.moveX = 1.f; invalid.run = true; invalid.prone = true; invalid.fireLethal = true; invalid.fireNonLethal = true; invalid.cqc = true; invalid.dragBody = true;
      invalid.aiming = true;
      const auto ammo = AgentAt(simulation).loadout;
      Step(simulation, invalid, 8);
      Require(AgentAt(simulation).position == hiddenPosition && !AgentAt(simulation).moving && !AgentAt(simulation).running, "El agente se mueve libremente dentro del escondite");
      Require(!AgentAt(simulation).aiming, "Mantener el apuntado reactiva una acción incompatible dentro del escondite");
      Require(AgentAt(simulation).loadout.lethalAmmo == ammo.lethalAmmo && AgentAt(simulation).loadout.nonLethalAmmo == ammo.nonLethalAmmo && simulation.GetState().shots.empty(), "El escondite permite disparar a través de su volumen");
      Require(simulation.CanSee({120.f, 180.f}, {1.f, 0.f}, AgentAt(simulation)) == (kind == HideoutKind::UnderTable), "La detección no distingue el espacio abierto bajo una mesa del interior de otros contenedores");
      if (kind == HideoutKind::UnderTable) Require(AgentAt(simulation).stance == Stance::Prone, "Debajo de la mesa el agente puede permanecer de pie");
      simulation.Step(0.1f, {{2, use}});
      Require(!AgentAt(simulation, 1).hidden && simulation.GetState().objects[0].occupantAgent == 1, "El escondite admite dos agentes simultáneos");
      Load(simulation, simulation.SnapshotFor(1));
      Step(simulation, use);
      Require(!AgentAt(simulation).hidden && AgentAt(simulation).hideoutId == 0 && simulation.GetState().objects[0].occupantAgent == 0, "E no libera la ocupación del escondite");
      Require(!WorldGeometry::HideoutOccupied(simulation.GetState().objects[0]), "Recargar el estado completo deja ocupación residual tras salir");
      const auto exitOffset = AgentAt(simulation).position - hiddenPosition;
      const auto view = WeaponFacing(AgentAt(simulation));
      const bool expectedExit = kind == HideoutKind::UnderTable ? exitOffset.dot(view) > 0.f &&
          std::abs(exitOffset.x * view.y - exitOffset.y * view.x) < SpatialConfig::CollisionSkin : AgentAt(simulation).position == entry;
      Require(expectedExit && !WorldGeometry::PositionBlocked(simulation.GetState(), AgentAt(simulation).position, GameplayConfig::AgentRadius), "La salida no respeta la orientación actual de la mesa o deja una huella exterior bloqueada");
      simulation.Step(0.1f, {{2, use}});
      Require(AgentAt(simulation, 1).hidden && simulation.GetState().objects[0].occupantAgent == 2, "Otro agente no puede reutilizar el escondite liberado tras recargar");
      simulation.Step(0.1f, {{2, use}});
      Step(simulation, use); simulation.DisconnectPlayer(1);
      Require(!WorldGeometry::HideoutOccupied(simulation.GetState().objects[0]) && AgentAt(simulation).hideoutId == 0, "La desconexión deja ocupado el escondite");
    }
  }
  void MalformedOccupancyIsAtomic() {
    Simulation simulation;
    auto snapshot = Empty(simulation); snapshot["objects"].push_back(Hideout(HideoutKind::Closet)); Load(simulation, snapshot);
    const auto reject = [&](const Json &invalid) {
      const auto before = simulation.SnapshotFor(1);
      std::string error;
      Require(!simulation.LoadSnapshot(invalid, error), "El estado completo acepta una ocupación contradictoria");
      Require(simulation.SnapshotFor(1) == before, "La ocupación rechazada altera el último estado válido");
    };
    snapshot = simulation.SnapshotFor(1); snapshot["objects"][0]["occupied"] = true; reject(snapshot);
    ActionCommand use; use.interact = true; Step(simulation, use);
    snapshot = simulation.SnapshotFor(1); snapshot["objects"][0]["occupied"] = false; reject(snapshot);
  }
  void BlockedExit() {
    Simulation simulation;
    auto snapshot = Empty(simulation); snapshot["objects"].push_back(Hideout(HideoutKind::TrashBin)); Load(simulation, snapshot);
    ActionCommand use; use.interact = true; Step(simulation, use); snapshot = simulation.SnapshotFor(1);
    for (const auto bounds : {sf::FloatRect{{138.f, 140.f}, {36.f, 80.f}}, sf::FloatRect{{246.f, 140.f}, {36.f, 80.f}},
                              sf::FloatRect{{174.f, 120.f}, {72.f, 24.f}}, sf::FloatRect{{174.f, 216.f}, {72.f, 24.f}}}) snapshot["obstacles"].push_back(Wall(bounds));
    Load(simulation, snapshot); Step(simulation, use);
    Require(AgentAt(simulation).hidden && simulation.GetState().objects[0].occupantAgent == 1, "La salida atraviesa paredes cuando todos los accesos están bloqueados");
    snapshot = simulation.SnapshotFor(1); snapshot["obstacles"] = Json::array(); Load(simulation, snapshot); Step(simulation, use);
    Require(!AgentAt(simulation).hidden, "El escondite no recupera la salida al quedar libre");
  }
  void HeldDragAndCollisions() {
    Simulation simulation;
    auto snapshot = Empty(simulation, {150.f, 180.f}, true); snapshot["guards"].push_back(Body()); Load(simulation, snapshot);
    ActionCommand held; held.dragBody = true;
    simulation.Step(0.1f, {{1, held}, {2, held}});
    Require(AgentAt(simulation).draggingGuard == 10 && AgentAt(simulation, 1).draggingGuard == -1, "Un cuerpo admite dos propietarios");
    held.moveY = 1.f; const auto origin = simulation.GetState().guards[0].position;
    Step(simulation, held, 15);
    Require(simulation.GetState().guards[0].position.y > origin.y && AgentAt(simulation).draggingGuard == 10, "Mantener E no conserva el arrastre durante el movimiento");
    Step(simulation); const auto released = simulation.GetState().guards[0].position; Step(simulation, {}, 5);
    Require(AgentAt(simulation).draggingGuard == -1 && simulation.GetState().guards[0].position == released, "Soltar E no deja el cuerpo en su posición real");
    snapshot = Empty(simulation, {150.f, 180.f}); snapshot["guards"].push_back(Body(10, {185.f, 180.f}));
    snapshot["obstacles"].push_back(Wall({{170.f, 100.f}, {2.f, 200.f}})); Load(simulation, snapshot); held.moveY = 0.f; Step(simulation, held);
    Require(AgentAt(simulation).draggingGuard == -1, "El arrastre alcanza un cuerpo a través de una pared");
    snapshot = Empty(simulation, {160.f, 180.f}); snapshot["guards"].push_back(Body(10, {140.f, 180.f}));
    snapshot["obstacles"].push_back(Wall({{180.f, 100.f}, {2.f, 200.f}})); Load(simulation, snapshot);
    held.moveX = 1.f; held.moveY = 1.f;
    for (unsigned int frame = 0; frame < 40; ++frame) {
      Step(simulation, held);
      Require(!WorldGeometry::PositionBlocked(simulation.GetState(), simulation.GetState().guards[0].position, GameplayConfig::AgentRadius), "El cuerpo arrastrado penetra una pared");
      Require(WorldGeometry::ClearMovementSegment(simulation.GetState(), AgentAt(simulation).position, simulation.GetState().guards[0].position, GameplayConfig::AgentRadius), "El cuerpo se teletransporta al otro lado de una esquina");
    }
  }
  void BodyStorageAndPriority() {
    Simulation simulation;
    auto snapshot = Empty(simulation); snapshot["objects"].push_back(Hideout(HideoutKind::Closet)); snapshot["guards"].push_back(Body()); Load(simulation, snapshot);
    ActionCommand store; store.dragBody = true; store.interact = true; Step(simulation, store);
    Require(simulation.GetState().objects[0].occupantGuard == 10 && simulation.GetState().guards[0].storedIn == 77 && simulation.GetState().guards[0].bodyHidden && AgentAt(simulation).draggingGuard == -1 && !AgentAt(simulation).hidden, "E contextual no almacena un cuerpo neutralizado");
    snapshot = simulation.SnapshotFor(1); snapshot["guards"].push_back(Body(11, {170.f, 175.f})); Load(simulation, snapshot); Step(simulation, store);
    Require(simulation.GetState().objects[0].occupantGuard == 10 && simulation.GetState().guards[1].storedIn == 0 && !simulation.GetState().guards[1].bodyHidden, "El contenedor almacena cuerpos sin respetar su capacidad");
    snapshot = Empty(simulation); snapshot["objects"].push_back(Hideout(HideoutKind::UnderTable)); snapshot["guards"].push_back(Body(10, {130.f, 180.f})); Load(simulation, snapshot); Step(simulation, store);
    Require(simulation.GetState().objects[0].occupantGuard == 0 && !simulation.GetState().guards[0].bodyHidden && AgentAt(simulation).draggingGuard == 10, "Una mesa incompatible almacena un cuerpo");
    snapshot = Empty(simulation); auto door = Hideout(HideoutKind::None); door["kind"] = static_cast<int>(ObjectKind::Door);
    snapshot["objects"].push_back(door); snapshot["guards"].push_back(Body()); Load(simulation, snapshot); ActionCommand press; press.interact = true; Step(simulation, press);
    Require(!simulation.GetState().objects[0].open, "Interactuar cerca de un cuerpo abre otra puerta sin respetar prioridad");
    snapshot = Empty(simulation); auto live = Body(); live["state"] = static_cast<int>(GuardState::Patrol); live["stun"] = 0.f; live["incapacityRemaining"] = 0.f;
    snapshot["guards"].push_back(live); Load(simulation, snapshot); Step(simulation, store);
    Require(AgentAt(simulation).draggingGuard == -1, "Mantener E permite arrastrar un enemigo vivo");
  }
  void StoredBodiesAndMaps() {
    Simulation simulation;
    auto snapshot = Empty(simulation); snapshot["objects"].push_back(Hideout(HideoutKind::Closet)); snapshot["guards"].push_back(Body()); Load(simulation, snapshot);
    ActionCommand store; store.dragBody = true; store.interact = true; Step(simulation, store);
    snapshot = simulation.SnapshotFor(1); snapshot["phase"] = static_cast<int>(GamePhase::Mission); snapshot["agents"][0]["position"] = {50.f, 650.f};
    auto patrol = Body(11, {230.f, 230.f}); patrol["state"] = static_cast<int>(GuardState::Patrol); patrol["stun"] = 0.f; patrol["incapacityRemaining"] = 0.f;
    snapshot["guards"].push_back(patrol); Load(simulation, snapshot); Step(simulation, {}, 30);
    Require(simulation.GetState().guards[0].bodyHidden && simulation.GetState().guards[0].state == GuardState::Incapacitated && !simulation.GetState().alarm, "La patrulla descubre o reactiva un cuerpo almacenado sin inspección");
    for (const auto &event : simulation.GetState().events) Require(event.kind != EventKind::BodyFound, "El cuerpo almacenado produce detección visual");
    simulation.ResetBase(); simulation.Deploy(3); bool trash = false, closet = false, table = false;
    for (const auto &object : simulation.GetState().objects) {
      trash |= WorldGeometry::HideoutType(object) == HideoutKind::TrashBin; closet |= WorldGeometry::HideoutType(object) == HideoutKind::Closet; table |= WorldGeometry::HideoutType(object) == HideoutKind::UnderTable;
    }
    Require(trash && closet && table, "La misión no incluye contenedor, armario y mesa compatibles");
  }
}

int main() {
  try {
    AgentHideouts(); MalformedOccupancyIsAtomic(); BlockedExit(); HeldDragAndCollisions(); BodyStorageAndPriority(); StoredBodiesAndMaps();
    std::cout << "PASS hideouts: " << checks << " comprobaciones de ocultación, ocupación, salidas, arrastre mantenido, colisiones y almacenamiento\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL hideouts tras " << checks << ": " << error.what() << '\n'; return 1; }
}
