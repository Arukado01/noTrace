#include "NoTraceSimulation.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  using Json = nlohmann::json;
  unsigned int checks = 0;
  void Require(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
  void Load(Simulation &simulation, const Json &snapshot) {
    std::string error;
    if (!simulation.LoadSnapshot(snapshot, error)) throw std::runtime_error("Fixture de escondite avanzado: " + error);
  }
  Json Scenario(Simulation &simulation, HideoutKind kind, sf::Vector2f entry = {230.f, 300.f}) {
    simulation.ResetBase(); simulation.Deploy(1);
    auto snapshot = simulation.SnapshotFor(1);
    auto object = snapshot["objects"][0]; object["id"] = 77; object["kind"] = static_cast<int>(ObjectKind::Hideout);
    object["position"] = {300.f, 300.f}; object["facing"] = {-1.f, 0.f}; object["hideoutKind"] = static_cast<int>(kind);
    object["footprint"] = {0.f, 0.f}; object["open"] = false; object["occupied"] = false; object["occupantAgent"] = 0; object["occupantGuard"] = 0;
    object["hideoutAperture"] = SpatialConfig::ClosetDefaultAperture; object["lifted"] = false; object["inspectionProgress"] = 0.f;
    snapshot["objects"] = Json::array({object}); snapshot["guards"] = Json::array(); snapshot["obstacles"] = Json::array(); snapshot["events"] = Json::array();
    snapshot["agents"][0]["position"] = {entry.x, entry.y}; snapshot["agents"][0]["facing"] = {1.f, 0.f};
    return snapshot;
  }
  void Step(Simulation &simulation, ActionCommand command = {}, unsigned int count = 1) {
    for (unsigned int frame = 0; frame < count; ++frame) simulation.Step(0.1f, {{1, command}});
  }
  void UnderTableSight() {
    WorldState world; world.phase = GamePhase::Mission;
    Agent agent; agent.id = 1; agent.position = {300.f, 300.f}; agent.facing = {1.f, 0.f}; agent.hidden = true; agent.hideoutId = 77; agent.stance = Stance::Prone;
    Interactable table{77, ObjectKind::Hideout, agent.position, "Mesa"}; table.hideoutKind = HideoutKind::UnderTable; table.occupantAgent = 1; table.occupied = true;
    world.objects = {table}; world.agents = {agent};
    Require(Visibility::IsGeometricallyVisible(world, agent, {450.f, 300.f}), "La mesa elimina la visión frontal accesible bajo el tablero");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {200.f, 300.f}), "La mesa revela enemigos a espaldas del agente");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {450.f, 450.f}), "La mesa conserva un campo visual demasiado amplio");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {500.f, 300.f}), "La mesa conserva el alcance de un agente de pie");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {340.f, 300.f}), "El agente ve una cabeza a través del tablero");
    Require(Visibility::IsGeometricallyVisible(world, agent, {340.f, 300.f}, Stance::Prone), "El tablero bloquea incorrectamente un rayo bajo su altura");
    agent.facing = {0.f, -1.f};
    Require(Visibility::IsGeometricallyVisible(world, agent, {300.f, 180.f}) && !Visibility::IsGeometricallyVisible(world, agent, {450.f, 300.f}), "Girar bajo la mesa no modifica el campo visual");
    world.obstacles.push_back({{{270.f, 230.f}, {60.f, 12.f}}, CoverKind::High, true, "Pared"});
    Require(!Visibility::IsGeometricallyVisible(world, agent, {300.f, 180.f}), "La visión bajo la mesa atraviesa una pared");
    world.obstacles.clear(); agent.facing = {1.f, 0.f};
    Guard front; front.id = 10; front.position = {450.f, 300.f};
    Guard back = front; back.id = 11; back.position = {200.f, 300.f};
    Require(Visibility::GuardVisible(world, agent, front) && !Visibility::GuardVisible(world, agent, back), "Las señales de guardias no respetan la visión parcial");
    const auto grid = Visibility::BuildGrid(world, agent);
    Require(grid.visualRadii.size() == VisibilityConfig::VisualRayCount, "El escondite no produce frontera continua para la niebla");
    Require(grid.visualRadii[0] > SpatialConfig::UnderTableViewRange - 0.5f && grid.visualRadii[0] <= SpatialConfig::UnderTableViewRange && grid.visualRadii[180] < 0.5f, "La frontera de niebla no aplica el alcance y la orientación de la mesa");
    Require(agent.hidden && agent.hideoutId == table.id, "Calcular la visión expone al agente oculto");
  }
  void ClosetSight() {
    WorldState world; world.phase = GamePhase::Mission;
    Agent agent; agent.id = 1; agent.position = {300.f, 300.f}; agent.hidden = true; agent.hideoutId = 77;
    Interactable closet{77, ObjectKind::Hideout, agent.position, "Armario"}; closet.hideoutKind = HideoutKind::Closet; closet.facing = {1.f, 0.f}; closet.occupantAgent = 1;
    world.objects = {closet};
    Require(!Visibility::IsGeometricallyVisible(world, agent, {400.f, 300.f}), "El armario cerrado conserva visión ficticia exterior");
    const auto closed = Visibility::BuildGrid(world, agent);
    Require(std::ranges::all_of(closed.visualRadii, [](float radius) { return radius < 0.5f; }), "La niebla del armario cerrado conserva un sector exterior visible");
    world.objects[0].open = true;
    Require(Visibility::IsGeometricallyVisible(world, agent, {400.f, 300.f}), "La abertura frontal no permite observar el exterior");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {200.f, 300.f}) && !Visibility::IsGeometricallyVisible(world, agent, {400.f, 340.f}), "El armario revela la parte trasera o los laterales");
    const auto opened = Visibility::BuildGrid(world, agent);
    Require(opened.visualRadii[0] > SpatialConfig::ClosetViewRange - 0.5f && opened.visualRadii[0] <= SpatialConfig::ClosetViewRange && opened.visualRadii[90] < 0.5f && opened.visualRadii[180] < 0.5f, "La frontera de niebla del armario no coincide con la abertura frontal");
    agent.facing = {-1.f, 0.f};
    Require(Visibility::IsGeometricallyVisible(world, agent, {400.f, 300.f}), "La cámara interior desvía la visión fuera de la abertura real");
    world.objects[0].hideoutAperture = 0.1f;
    Require(!Visibility::IsGeometricallyVisible(world, agent, {400.f, 310.f}), "El tamaño de la abertura no delimita el campo visual");
    world.objects[0].hideoutAperture = 0.f;
    Require(!Visibility::IsGeometricallyVisible(world, agent, {400.f, 300.f}), "Una abertura nula permite observar el exterior");
    world.objects[0].hideoutAperture = SpatialConfig::ClosetDefaultAperture;
    world.obstacles.push_back({{{350.f, 270.f}, {12.f, 60.f}}, CoverKind::High, true, "Pared"});
    Require(!Visibility::IsGeometricallyVisible(world, agent, {400.f, 300.f}), "La abertura del armario permite ver a través de paredes");
  }
  void ClosetEntranceAndExit() {
    for (const auto facing : {sf::Vector2f{-1.f, 0.f}, sf::Vector2f{1.f, 0.f}, sf::Vector2f{0.f, -1.f}, sf::Vector2f{0.f, 1.f}}) {
      Simulation simulation;
      auto snapshot = Scenario(simulation, HideoutKind::Closet);
      snapshot["phase"] = static_cast<int>(GamePhase::Base); snapshot["objects"][0]["facing"] = {facing.x, facing.y};
      const auto entry = sf::Vector2f{300.f, 300.f} + facing * 60.f;
      snapshot["agents"][0]["position"] = {entry.x, entry.y}; Load(simulation, snapshot);
      ActionCommand use; use.interact = true; Step(simulation, use);
      Require(simulation.GetState().agents[0].hideoutId == 77, "La puerta frontal no admite la entrada según su orientación");
      ActionCommand opening; opening.tool = true; Step(simulation, opening);
      Require(simulation.GetState().objects[0].open && simulation.GetState().agents[0].hidden, "La apertura contextual no permite mirar sin abandonar el armario");
      Step(simulation, opening);
      Require(!simulation.GetState().objects[0].open, "La apertura contextual no vuelve a cerrar el armario");
      Step(simulation, use);
      Require(simulation.GetState().agents[0].position == entry && !WorldGeometry::PositionBlocked(simulation.GetState(), entry, GameplayConfig::AgentRadius), "Salir del armario no respeta la puerta frontal libre");
      snapshot = Scenario(simulation, HideoutKind::Closet); snapshot["phase"] = static_cast<int>(GamePhase::Base); snapshot["objects"][0]["facing"] = {facing.x, facing.y};
      const auto rear = sf::Vector2f{300.f, 300.f} - facing * 60.f;
      snapshot["agents"][0]["position"] = {rear.x, rear.y}; Load(simulation, snapshot); Step(simulation, use);
      Require(simulation.GetState().agents[0].hideoutId == 0, "El armario permite entrar atravesando su panel trasero");
      snapshot = Scenario(simulation, HideoutKind::Closet); snapshot["phase"] = static_cast<int>(GamePhase::Base); snapshot["objects"][0]["facing"] = {facing.x, facing.y};
      const sf::Vector2f tangent{-facing.y, facing.x};
      const auto side = sf::Vector2f{300.f, 300.f} + tangent * 45.f;
      snapshot["agents"][0]["position"] = {side.x, side.y}; Load(simulation, snapshot); Step(simulation, use);
      Require(simulation.GetState().agents[0].hideoutId == 0, "El armario permite entrar atravesando un lateral sólido");
    }
    Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::Closet); snapshot["phase"] = static_cast<int>(GamePhase::Base); Load(simulation, snapshot);
    ActionCommand use; use.interact = true; Step(simulation, use); snapshot = simulation.SnapshotFor(1);
    snapshot["obstacles"].push_back({{"position", {240.f, 260.f}}, {"size", {24.f, 80.f}}, {"cover", 2}, {"solid", true}, {"name", "Puerta bloqueada"}});
    Load(simulation, snapshot); Step(simulation, use);
    Require(simulation.GetState().agents[0].hideoutId == 77 && simulation.GetState().agents[0].hidden, "El armario bloqueado expulsa al agente por detrás o por un lateral");
  }
  void HiddenOrientationAndNetworkPrivacy() {
    Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable); Load(simulation, snapshot);
    ActionCommand use; use.interact = true; Step(simulation, use);
    ActionCommand look; look.aimY = -1.f; look.moveX = 1.f; Step(simulation, look);
    const auto &agent = simulation.GetState().agents[0];
    Require(agent.hidden && agent.hideoutId == 77 && agent.position == sf::Vector2f{300.f, 300.f} && agent.facing == sf::Vector2f{0.f, -1.f}, "La entrada autoritativa no permite girar bajo la mesa sin mover al agente");
    snapshot = Scenario(simulation, HideoutKind::Closet); snapshot["phase"] = static_cast<int>(GamePhase::Base); Load(simulation, snapshot); Step(simulation, use);
    ActionCommand opening; opening.tool = true; Step(simulation, opening);
    snapshot = simulation.SnapshotFor(1); snapshot["phase"] = static_cast<int>(GamePhase::Mission);
    Simulation donor; donor.ResetBase(); donor.Deploy(1); auto guard = donor.SnapshotFor(1)["guards"][0];
    guard["id"] = 10; guard["position"] = {210.f, 300.f}; guard["patrol"] = Json::array(); guard["waypoint"] = 0;
    auto hidden = guard; hidden["id"] = 11; hidden["position"] = {390.f, 300.f}; snapshot["guards"] = Json::array({guard, hidden});
    Load(simulation, snapshot);
    const auto filtered = simulation.SnapshotFor(1, true);
    Require(filtered["guards"].size() == 1 && filtered["guards"][0]["id"] == 10, "El anfitrión revela al cliente del armario guardias fuera de su abertura");
    Simulation replica; std::string error;
    Require(replica.LoadSnapshot(filtered, error) && replica.GetState().agents[0].hidden && replica.GetState().objects[0].open, "La réplica rechaza el armario entreabierto y su visibilidad privada");
    Step(simulation, opening);
    Require(simulation.SnapshotFor(1, true)["guards"].empty(), "Cerrar la puerta no retira las posiciones enemigas del snapshot LAN");
  }
  void WitnessedInspection() {
    for (int witness = 0; witness < 5; ++witness) {
      Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable);
      Simulation donor; donor.ResetBase(); donor.Deploy(1); auto guard = donor.SnapshotFor(1)["guards"][0];
      guard["id"] = 10; guard["position"] = {180.f, 300.f}; guard["facing"] = witness == 2 ? Json{-1.f, 0.f} : Json{1.f, 0.f};
      guard["state"] = static_cast<int>(witness == 0 ? GuardState::Patrol : GuardState::Alert); guard["suspicion"] = witness == 0 ? 0.f : 1.f;
      guard["observedAgent"] = witness == 4 ? 2 : 1;
      guard["patrol"] = Json::array(); guard["waypoint"] = 0; guard["attackCooldown"] = 60.f; snapshot["guards"] = Json::array({guard});
      if (witness == 3) snapshot["obstacles"].push_back({{"position", {200.f, 250.f}}, {"size", {12.f, 100.f}}, {"cover", 2}, {"solid", true}, {"name", "Vista bloqueada"}});
      if (witness == 4) {
        auto other = snapshot["agents"][0]; other["id"] = 2; other["name"] = "Otro agente"; other["position"] = {700.f, 650.f}; other.erase("decoyGuide"); other.erase("privateOrder"); other.erase("privateComplete"); snapshot["agents"].push_back(other);
      }
      Load(simulation, snapshot); ActionCommand use; use.interact = true; Step(simulation, use);
      Require(simulation.GetState().agents[0].hidden, "La mesa expone al agente antes de iniciar una inspección");
      if (witness != 1) {
        Require(simulation.GetState().guards[0].knownHideout == 0, "La IA conoce una entrada que no observó con detección válida");
        Step(simulation, {}, 40);
        Require(!simulation.GetState().objects[0].lifted && simulation.GetState().agents[0].hidden, "Un guardia sin pista identifica mágicamente la mesa ocupada");
        continue;
      }
      Require(simulation.GetState().guards[0].knownHideout == 77, "El guardia pierde la entrada que observó tras confirmar al agente");
      bool intermediate = false, approachFootstep = false;
      for (unsigned int frame = 0; frame < 60 && !simulation.GetState().objects[0].lifted; ++frame) {
        Step(simulation);
        approachFootstep |= std::ranges::any_of(simulation.GetState().footsteps, [](const FootstepCue &cue) { return cue.sourceGuard == 10 && cue.remaining > 0.f; });
        const auto &object = simulation.GetState().objects[0];
        if (object.inspectionProgress > 0.f && object.inspectionProgress < 1.f) {
          intermediate = true;
          Require(simulation.GetState().agents[0].hidden, "La inspección expone al ocupante antes de completar su duración");
          Require(WorldGeometry::DistanceToBounds(simulation.GetState().guards[0].position, WorldGeometry::ObjectFootprint(object)) < GameplayConfig::AgentRadius + SpatialConfig::HideoutInspectionReach + 1.f, "La mesa se inspecciona a distancia arbitraria");
        }
      }
      const auto &world = simulation.GetState();
      Require(approachFootstep, "El guardia que se aproxima a una mesa conocida pierde sus pasos anónimos");
      Require(intermediate && world.objects[0].lifted && world.objects[0].inspectionProgress == 1.f, "La inspección no recorre una transición temporal reconocible");
      Require(!world.agents[0].hidden && world.agents[0].hideoutId == 0 && world.agents[0].position == sf::Vector2f{300.f, 300.f}, "Levantar la mesa no expone al agente en su posición real");
      Require(!WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius), "La mesa levantada deja al agente dentro de una colisión sólida");
      Require(world.guards[0].knownHideout == 0, "Completar la inspección deja una pista de escondite obsoleta");
    }
  }
  void DifferentOccupantIsDetected() {
    Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable, {700.f, 650.f});
    auto occupant = snapshot["agents"][0]; occupant["id"] = 2; occupant["name"] = "Ocupante actual"; occupant["position"] = {300.f, 300.f}; occupant["stance"] = static_cast<int>(Stance::Prone);
    occupant["hidden"] = true; occupant["hideoutId"] = 77; occupant["hideoutExit"] = {230.f, 300.f}; occupant.erase("decoyGuide"); occupant.erase("privateOrder"); occupant.erase("privateComplete"); snapshot["agents"].push_back(occupant);
    snapshot["objects"][0]["occupantAgent"] = 2; snapshot["objects"][0]["occupied"] = true;
    Simulation donor; donor.ResetBase(); donor.Deploy(1); auto guard = donor.SnapshotFor(1)["guards"][0];
    guard["id"] = 10; guard["position"] = {249.f, 300.f}; guard["facing"] = {1.f, 0.f}; guard["state"] = static_cast<int>(GuardState::Search);
    guard["observedAgent"] = 1; guard["suspicion"] = 1.f; guard["knownHideout"] = 77; guard["hideoutInspectionTime"] = 1.4f;
    guard["patrol"] = Json::array(); guard["waypoint"] = 0; guard["attackCooldown"] = 60.f; snapshot["guards"] = Json::array({guard});
    Load(simulation, snapshot); Step(simulation);
    Require(simulation.GetState().objects[0].lifted && !simulation.GetState().agents[1].hidden, "La inspección legítima no expone al nuevo ocupante de la mesa");
    Require(std::ranges::any_of(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection && event.actor == 2 && event.target == 10; }), "Exponer un ocupante diferente omite su detección y conserva Maestro erróneamente");
  }
}

int main() {
  try {
    UnderTableSight(); ClosetSight(); ClosetEntranceAndExit(); HiddenOrientationAndNetworkPrivacy(); WitnessedInspection(); DifferentOccupantIsDetected();
    std::cout << "PASS escondites avanzados: " << checks << " comprobaciones de visión, aberturas, accesos e inspección observada\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL escondites avanzados tras " << checks << ": " << error.what() << '\n'; return 1; }
}
