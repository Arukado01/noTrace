#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
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
    auto snapshot = simulation.SnapshotFor(1); TestFixture::LegacyPlane(snapshot);
    auto object = snapshot["objects"][0]; object["id"] = 77; object["kind"] = static_cast<int>(ObjectKind::Hideout);
    object["position"] = {300.f, 300.f}; object["facing"] = {-1.f, 0.f}; object["hideoutKind"] = static_cast<int>(kind);
    object["footprint"] = {0.f, 0.f}; object["open"] = false; object["occupied"] = false; object["occupantAgent"] = 0; object["occupantGuard"] = 0;
    object["locked"] = false; object["puzzleDevice"]["kind"] = static_cast<int>(PuzzleKind::None);
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
    for (const auto direction : std::array<sf::Vector2f, 8>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}, {1.f, -1.f}, {-1.f, -1.f}}}) {
      agent.weaponAim = direction.normalized();
      const auto point = agent.position + direction.normalized() * 100.f;
      Require(Visibility::IsGeometricallyVisible(world, agent, point), "El cursor no permite orientar la visión bajo la mesa en una dirección válida");
    }
    agent.weaponAim = {1.f, 0.f};
    Require(!Visibility::IsGeometricallyVisible(world, agent, {300.f + VisibilityConfig::ProneRange + 1.f, 300.f}), "La mesa amplía el alcance normal tumbado");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {200.f, 300.f}), "La mesa revela la espalda del cono actual");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {280.f, 300.f}, Stance::Prone) && !Visibility::IsGeometricallyVisible(world, agent, {300.f, 320.f}, Stance::Prone),
      "La mesa conserva un halo de visión detrás o al lado del cursor");
    Require(Visibility::IsGeometricallyVisible(world, agent, {330.f, 320.f}, Stance::Prone) && !Visibility::IsGeometricallyVisible(world, agent, {320.f, 330.f}, Stance::Prone),
      "El cono bajo la mesa no limita la visión a noventa grados alrededor del cursor");
    Require(!Visibility::IsGeometricallyVisible(world, agent, {340.f, 300.f}), "El agente ve una cabeza a través del tablero");
    Require(Visibility::IsGeometricallyVisible(world, agent, {340.f, 300.f}, Stance::Prone), "El tablero bloquea incorrectamente un rayo bajo su altura");
    agent.facing = {0.f, -1.f};
    Require(!Visibility::IsGeometricallyVisible(world, agent, {300.f, 200.f}) && Visibility::IsGeometricallyVisible(world, agent, {400.f, 300.f}), "La orientación corporal añade una segunda dirección al cono controlado por cursor");
    world.obstacles.push_back({{{270.f, 230.f}, {60.f, 12.f}}, CoverKind::High, true, "Pared"});
    Require(!Visibility::IsGeometricallyVisible(world, agent, {300.f, 200.f}), "La visión bajo la mesa atraviesa una pared");
    world.obstacles.clear(); agent.facing = {1.f, 0.f};
    Guard front; front.id = 10; front.position = {400.f, 300.f};
    Guard back = front; back.id = 11; back.position = {200.f, 300.f};
    Require(Visibility::GuardVisible(world, agent, front) && !Visibility::GuardVisible(world, agent, back), "El cono de mesa revela guardias detrás del cursor");
    agent.weaponAim = {-1.f, 0.f};
    Require(!Visibility::GuardVisible(world, agent, front) && Visibility::GuardVisible(world, agent, back), "Girar el cursor no cambia las entidades visibles bajo mesa");
    Interactable cabinet{78, ObjectKind::Armory, {230.f, 300.f}, "Armario opaco"}; cabinet.footprint = {12.f, 60.f}; world.objects.push_back(cabinet);
    Require(!Visibility::GuardVisible(world, agent, back), "El cursor revela guardias detrás de otro objeto opaco");
    world.objects.pop_back(); agent.weaponAim = {1.f, 0.f};
    const auto grid = Visibility::BuildGrid(world, agent);
    Require(grid.visualRadii.size() == VisibilityConfig::VisualRayCount, "El escondite no produce frontera continua para la niebla");
    Require(grid.visualRadii[0] > VisibilityConfig::ProneRange - 0.5f && grid.visualRadii[0] <= VisibilityConfig::ProneRange &&
      grid.visualRadii[180] <= VisibilityConfig::SurfaceOffset + 0.5f && grid.visualRadii[90] <= VisibilityConfig::SurfaceOffset + 0.5f &&
      std::ranges::all_of(grid.visualRadii, [](float radius) { return radius <= VisibilityConfig::ProneRange; }), "La niebla no comparte el único cono del cursor bajo la mesa");
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
    ActionCommand mouse; mouse.aimY = -1.f; Step(simulation, mouse);
    Require(simulation.GetState().agents[0].facing == sf::Vector2f{1.f, 0.f}, "El ratón gira el cuerpo oculto bajo la mesa");
    Require(simulation.GetState().agents[0].weaponAim == sf::Vector2f{0.f, -1.f} && !simulation.GetState().agents[0].aiming, "El arma oculta no sigue el cursor o activa el disparo dentro de la mesa");
    ActionCommand look; look.aimX = 1.f; look.moveY = -1.f; Step(simulation, look);
    const auto &agent = simulation.GetState().agents[0];
    Require(agent.hidden && agent.hideoutId == 77 && agent.position == sf::Vector2f{300.f, 300.f} && agent.facing == sf::Vector2f{1.f, 0.f} && agent.weaponAim == sf::Vector2f{1.f, 0.f},
      "WASD cambia la posición o la orientación independiente del cuerpo bajo la mesa");
    mouse.aimY = 1.f; Step(simulation, mouse);
    Require(agent.facing == sf::Vector2f{1.f, 0.f} && agent.position == sf::Vector2f{300.f, 300.f}, "Mover el ratón cambia el cuerpo fijo bajo la mesa");
    Require(agent.weaponAim == sf::Vector2f{0.f, 1.f}, "Girar el cursor bajo la mesa pierde la orientación independiente del arma");
    snapshot = Scenario(simulation, HideoutKind::Closet); snapshot["phase"] = static_cast<int>(GamePhase::Base); Load(simulation, snapshot); Step(simulation, use);
    ActionCommand opening; opening.tool = true; Step(simulation, opening);
    snapshot = simulation.SnapshotFor(1); snapshot["phase"] = static_cast<int>(GamePhase::Mission);
    Simulation donor; donor.ResetBase(); donor.Deploy(1); auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot); auto guard = donorSnapshot["guards"][0];
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
  void InstantTableTransitions() {
    for (const auto kind : {HideoutKind::UnderTable, HideoutKind::Closet, HideoutKind::TrashBin}) {
      Simulation simulation; auto snapshot = Scenario(simulation, kind); Load(simulation, snapshot);
      ActionCommand use; use.interact = true; use.crouch = true; Step(simulation, use);
      const auto &agent = simulation.GetState().agents[0];
      Require(agent.hidden && agent.position == sf::Vector2f{300.f, 300.f}, "La interacción no coloca inmediatamente al ocupante en su escondite");
      if (kind == HideoutKind::UnderTable) Require(agent.actionPose == ActionPose::None && agent.actionPoseTime == 0.f && agent.transitionTime == 0.f && agent.transitionFrom == Stance::Prone,
        "Entrar bajo la mesa conserva una pose o transición de postura gradual");
      else Require(agent.actionPose == ActionPose::HideEnter && agent.actionPoseTime > 0.f, "Quitar la animación de mesa elimina también la entrada de otro escondite");
      use.crouch = false; Step(simulation, use);
      Require(!agent.hidden && agent.hideoutId == 0 && !WorldGeometry::PositionBlocked(simulation.GetState(), agent.position, GameplayConfig::AgentRadius), "Salir del escondite no actualiza la posición exterior libre de colisiones");
      if (kind == HideoutKind::UnderTable) {
        Require(agent.actionPose == ActionPose::None && agent.actionPoseTime == 0.f && agent.transitionTime == 0.f, "Salir bajo la mesa reproduce una pose o transición gradual");
        const auto position = agent.position; Step(simulation);
        Require(agent.position == position && agent.actionPose == ActionPose::None, "La salida instantánea continúa deslizando al agente en un tick posterior");
        ActionCommand crawl; crawl.moveX = 1.f; Step(simulation, crawl);
        Require(agent.position.x > position.x && agent.stance == Stance::Prone && agent.moving, "Eliminar la transición de mesa impide el movimiento normal tumbado después de salir");
      } else Require(agent.actionPose == ActionPose::HideExit && agent.actionPoseTime > 0.f, "Quitar la animación de mesa elimina también la salida de otro escondite");
    }
  }
  void UnderTableExposureAndCamera() {
    for (const bool wall : {false, true}) {
      Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable); Load(simulation, snapshot);
      ActionCommand use; use.interact = true; Step(simulation, use);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      const auto &agent = world.agents[0];
      Require(simulation.CanSee({200.f, 300.f}, {1.f, 0.f}, agent), "El estado oculto impide ver el cuerpo por una línea válida bajo el tablero");
      Require(!simulation.CanSee({249.f, 300.f}, {1.f, 0.f}, agent), "El tablero deja de ocultar físicamente al cuerpo cuando corta los rayos visuales");
      Require(!simulation.CanSee({200.f, 300.f}, {-1.f, 0.f}, agent) && !simulation.CanSee({150.f, 300.f}, {1.f, 0.f}, agent), "La visión del jugador modifica el cono o alcance normal de los enemigos");
      if (wall) world.obstacles.push_back({{{240.f, 250.f}, {12.f, 100.f}}, CoverKind::High, true, "Pared opaca"});
      Require(simulation.CanSee({200.f, 300.f}, {1.f, 0.f}, agent) == !wall, "El guardia no respeta la pared al observar bajo la mesa");
      Guard guard; guard.id = 91; guard.position = {200.f, 300.f}; guard.facing = {1.f, 0.f}; guard.attackCooldown = 60.f; world.guards.push_back(guard);
      Interactable camera{92, ObjectKind::Camera, {200.f, 300.f}, "Cámara"}; camera.facing = {1.f, 0.f}; camera.camera.sweeping = false; camera.camera.range = 300.f;
      world.objects.push_back(camera); Step(simulation);
      Require((world.guards[0].suspicion > 0.f) == !wall, "La detección real del guardia omite el cuerpo visible bajo la mesa o atraviesa una pared");
      Require((world.objects.back().progress > 0.f) == !wall, "La cámara trata la mesa como invisibilidad absoluta o ve a través de una pared");
      Require(agent.hidden && agent.hideoutId == 77 && world.securityAlert != SecurityAlert::Red, "Observar bajo la mesa expulsa al agente o confirma una alerta sin completar detección");
    }
  }
  void UnderTablePrivateDirectionalSnapshots() {
    Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable); Load(simulation, snapshot);
    ActionCommand use; use.interact = true; Step(simulation, use);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    Guard front; front.id = 91; front.position = {400.f, 300.f};
    Guard back = front; back.id = 92; back.position = {200.f, 300.f};
    Guard blocked = front; blocked.id = 93; blocked.position = {300.f, 200.f};
    world.guards = {front, back, blocked};
    world.obstacles.push_back({{{270.f, 230.f}, {60.f, 12.f}}, CoverKind::High, true, "Pared"});
    Agent other; other.id = 2; other.position = blocked.position; world.agents.push_back(other);
    const auto filtered = simulation.SnapshotFor(1, true);
    Require(filtered["guards"].size() == 1 && filtered["guards"][0]["id"] == 91,
      "La instantánea bajo mesa revela la espalda del cono o transmite un guardia detrás de la pared");
    Require(filtered["agents"].size() == 1 && filtered["agents"][0]["id"] == 1, "El cursor revela a otro agente detrás de una pared");
    Simulation replica; std::string error;
    Require(replica.LoadSnapshot(filtered, error) && replica.GetState().agents[0].hidden && replica.GetState().agents[0].hideoutId == 77 && replica.GetState().guards.size() == 1,
      "La réplica rechaza la mesa y las entidades visibles dentro del cono actual");
    Require(replica.GetState().visibility.visualRadii[0] > VisibilityConfig::ProneRange - 0.5f && replica.GetState().visibility.visualRadii[180] <= VisibilityConfig::SurfaceOffset + 0.5f &&
      replica.GetState().visibility.visualRadii[270] <= VisibilityConfig::SurfaceOffset + 0.5f,
      "La frontera transmitida añade visión lateral o trasera fuera del cono del cursor");
    const auto facing = world.agents[0].facing;
    ActionCommand cursor; cursor.aimX = -1.f; Step(simulation, cursor);
    const auto opposite = simulation.SnapshotFor(1, true);
    Require(opposite["guards"].size() == 1 && opposite["guards"][0]["id"] == 92 && world.agents[0].facing == facing, "El comando de cursor no cambia el cono privado conservando el cuerpo fijo");
    Require(replica.LoadSnapshot(opposite, error), "El cliente rechaza el giro de visión bajo la mesa");
  }
  void UnderTableExitFollowsCursor() {
    for (const auto direction : std::array<sf::Vector2f, 8>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}, {1.f, 1.f}, {-1.f, 1.f}, {1.f, -1.f}, {-1.f, -1.f}}}) {
      Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable);
      snapshot["phase"] = static_cast<int>(GamePhase::Base); Load(simulation, snapshot);
      ActionCommand use; use.interact = true; Step(simulation, use);
      Require(simulation.GetState().agents[0].hideoutId == 77, "La fixture no entra bajo la mesa");
      ActionCommand look; look.moveX = -direction.x; look.moveY = -direction.y; look.aimX = direction.x; look.aimY = direction.y; Step(simulation, look); Step(simulation, use);
      const auto &world = simulation.GetState(); const auto &agent = world.agents[0]; const auto offset = agent.position - sf::Vector2f{300.f, 300.f};
      Require(!agent.hidden && agent.hideoutId == 0 && world.objects[0].occupantAgent == 0, "La salida orientada conserva una ocupación o un estado oculto obsoleto");
      Require(offset.dot(direction) > 0.f && std::abs(offset.x * direction.y - offset.y * direction.x) < SpatialConfig::CollisionSkin,
        "Salir bajo la mesa usa WASD o el acceso anterior en lugar del lado señalado por el cursor");
      Require(!WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius), "La salida orientada queda dentro del tablero o de otra huella sólida");
    }
    for (const auto direction : {sf::Vector2f{1.f, 0.f}, sf::Vector2f{0.f, -1.f}}) {
      Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable); snapshot["phase"] = static_cast<int>(GamePhase::Base); Load(simulation, snapshot);
      ActionCommand use; use.interact = true; Step(simulation, use); ActionCommand look; look.aimX = direction.x; look.aimY = direction.y; Step(simulation, look);
      snapshot = simulation.SnapshotFor(1);
      const auto wall = direction.x > 0.f ? Json{{"position", {340.f, 250.f}}, {"size", {16.f, 100.f}}} : Json{{"position", {250.f, 260.f}}, {"size", {100.f, 16.f}}};
      auto obstacle = wall; obstacle["cover"] = 2; obstacle["solid"] = true; obstacle["name"] = "Salida orientada bloqueada"; snapshot["obstacles"].push_back(obstacle); Load(simulation, snapshot); Step(simulation, use);
      const auto &agent = simulation.GetState().agents[0];
      Require(agent.hidden && agent.hideoutId == 77 && agent.position == sf::Vector2f{300.f, 300.f}, "Una salida frontal bloqueada fuerza la salida por el acceso anterior o atraviesa la pared");
      Require(simulation.GetState().objects[0].occupantAgent == 1 && !simulation.GetState().message.empty(), "Rechazar la salida pierde la ocupación o no informa al jugador");
      look.aimX = -direction.x; look.aimY = -direction.y; Step(simulation, look); Step(simulation, use);
      Require(!simulation.GetState().agents[0].hidden, "Girar hacia un lado libre no permite salir después de un rechazo válido");
    }
  }
  void WitnessedInspection() {
    for (int witness = 0; witness < 5; ++witness) {
      Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable);
      Simulation donor; donor.ResetBase(); donor.Deploy(1); auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot); auto guard = donorSnapshot["guards"][0];
      guard["id"] = 10; guard["position"] = {180.f, 300.f}; guard["facing"] = witness == 2 ? Json{-1.f, 0.f} : Json{1.f, 0.f};
      guard["state"] = static_cast<int>(witness == 0 ? GuardState::Patrol : GuardState::Alert); guard["suspicion"] = witness == 0 ? 0.f : 1.f;
      guard["observedAgent"] = witness == 4 ? 2 : 1;
      guard["patrol"] = Json::array(); guard["waypoint"] = 0; guard["attackCooldown"] = 60.f; snapshot["guards"] = Json::array({guard});
      if (witness == 3) snapshot["obstacles"].push_back({{"position", {200.f, 250.f}}, {"size", {12.f, 100.f}}, {"cover", 2}, {"solid", true}, {"name", "Vista bloqueada"}});
      if (witness == 4) {
        auto other = snapshot["agents"][0]; other["id"] = 2; other["name"] = "Otro agente"; other["position"] = {700.f, 650.f}; other.erase("decoyGuide"); other.erase("privateOrder"); other.erase("privateComplete"); other.erase("spectatorTarget"); snapshot["agents"].push_back(other);
      }
      Load(simulation, snapshot); ActionCommand use; use.interact = true; Step(simulation, use);
      Require(simulation.GetState().agents[0].hidden, "La mesa expone al agente antes de iniciar una inspección");
      if (witness != 1) {
        Require(simulation.GetState().guards[0].knownHideout == 0, "La IA conoce una entrada que no observó con detección válida");
        Step(simulation, {}, 40);
        Require(!simulation.GetState().objects[0].overturned && simulation.GetState().agents[0].hidden, "Un guardia sin pista identifica mágicamente la mesa ocupada");
        continue;
      }
      Require(simulation.GetState().guards[0].knownHideout == 77, "El guardia pierde la entrada que observó tras confirmar al agente");
      Require(simulation.GetState().guards[0].state == GuardState::Search && simulation.GetState().guards[0].lastKnown != simulation.GetState().agents[0].position,
        "Ver todavía el cuerpo bajo el tablero interrumpe el acercamiento a la mesa observada");
      bool intermediate = false, approachFootstep = false;
      for (unsigned int frame = 0; frame < 60 && !simulation.GetState().objects[0].overturned; ++frame) {
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
      Require(intermediate && world.objects[0].overturned && !world.objects[0].lifted && world.objects[0].inspectionProgress == 1.f, "La inspección no confirma el estado volcado sin dejar una mesa flotante");
      Require(!world.agents[0].hidden && world.agents[0].hideoutId == 0 && world.agents[0].position.x > 300.f && world.agents[0].position.y == 300.f, "Volcar la mesa no usa una salida física válida del escondite");
      Require(!WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius), "La mesa levantada deja al agente dentro de una colisión sólida");
      Require(world.guards[0].knownHideout == 0, "Completar la inspección deja una pista de escondite obsoleta");
    }
  }
  void PrepareKnownTableInspection(Simulation &simulation, sf::Vector2f inspector) {
    const auto snapshot = Scenario(simulation, HideoutKind::UnderTable); Load(simulation, snapshot);
    auto &world = const_cast<WorldState &>(simulation.GetState()); auto &agent = world.agents[0]; auto &table = world.objects[0];
    agent.position = table.position; agent.stance = Stance::Prone; agent.hidden = true; agent.hideoutId = table.id; agent.hideoutExit = {230.f, 300.f};
    agent.weaponAim = (inspector - table.position).normalized(); table.occupied = true; table.occupantAgent = agent.id;
    Guard guard; guard.id = 10; guard.position = inspector; guard.facing = (table.position - inspector).normalized(); guard.state = GuardState::Search;
    guard.knownHideout = table.id; guard.observedAgent = agent.id; guard.suspicion = 1.f; guard.hideoutInspectionTime = SpatialConfig::HideoutInspectionDuration - 0.1f; guard.attackCooldown = 60.f;
    world.guards = {guard};
  }
  void InspectorClearsTheFutureTableFootprint() {
    for (const auto position : std::array<sf::Vector2f, 8>{{{249.f, 300.f}, {351.f, 300.f}, {300.f, 264.f}, {300.f, 336.f},
        {246.05f, 300.f}, {353.95f, 300.f}, {300.f, 261.05f}, {300.f, 338.95f}}}) {
      Simulation simulation; PrepareKnownTableInspection(simulation, position);
      auto &world = const_cast<WorldState &>(simulation.GetState());
      auto flipped = world.objects[0]; flipped.overturned = true; flipped.footprint = {};
      Require(WorldGeometry::CircleOverlaps(world.guards[0].position, GameplayConfig::AgentRadius, WorldGeometry::ObjectFootprint(flipped)),
        "La fixture no reproduce al inspector demasiado cerca de la huella ampliada");
      Step(simulation);
      Require(!WorldGeometry::CircleOverlaps(world.guards[0].position, GameplayConfig::AgentRadius, WorldGeometry::ObjectFootprint(flipped)) &&
        world.guards[0].hideoutInspectionTime == 0.f && !world.objects[0].overturned,
        "El inspector no se aparta físicamente antes de volcar la mesa");
      Step(simulation, {}, 14);
      Require(world.agents[0].hidden && !world.objects[0].overturned, "Apartarse del tablero omite la duración real de inspección");
      Step(simulation, {}, 2);
      Require(world.objects[0].overturned && !world.agents[0].hidden && world.agents[0].hideoutId == 0,
        "El inspector queda bloqueado por su propia colisión o por el cursor del ocupante");
      Require((world.agents[0].position - world.guards[0].position).length() >= GameplayConfig::AgentRadius * 2.f &&
        !WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius),
        "La expulsión forzada deja al ocupante dentro del inspector o del tablero volcado");
    }
  }
  void InspectionExpelsThroughAFreeSideAndPausesDuringCqc() {
    Simulation simulation; PrepareKnownTableInspection(simulation, {245.f, 300.f});
    auto &world = const_cast<WorldState &>(simulation.GetState());
    Agent other; other.id = 2; other.name = "Paso ocupado"; other.position = {354.02f, 300.f}; other.stance = Stance::Prone; world.agents.push_back(other);
    Step(simulation);
    Require(world.objects[0].overturned && !world.agents[0].hidden && std::abs(world.agents[0].position.y - 300.f) > GameplayConfig::AgentRadius && world.agents[0].position.x == 300.f,
      "El guardia no busca otro lado libre cuando la expulsión opuesta está ocupada");
    Require((world.agents[0].position - world.agents[1].position).length() >= GameplayConfig::AgentRadius * 2.f,
      "La expulsión forzada atraviesa a otro agente");
    PrepareKnownTableInspection(simulation, {245.f, 300.f});
    auto &paused = const_cast<WorldState &>(simulation.GetState()); paused.guards[0].cqcState = CqcState::KnockedDown;
    Step(simulation, {}, 20);
    Require(paused.agents[0].hidden && !paused.objects[0].overturned && paused.guards[0].hideoutInspectionTime == 0.f,
      "Un guardia derribado por CQC continúa levantando la mesa");
    paused.guards[0].cqcState = CqcState::None; Step(simulation, {}, 16);
    Require(paused.objects[0].overturned && !paused.agents[0].hidden, "El guardia no reanuda la inspección legítima después de recuperar movilidad");
  }
  void InspectionPreservesTheLandingSupport() {
    Simulation simulation; PrepareKnownTableInspection(simulation, {245.f, 300.f});
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.walkSurfaces = {
      {10, 1, WalkSurfaceKind::Floor, {{250.f, 200.f}, {155.f, 200.f}}, 96.f},
      {11, 1, WalkSurfaceKind::Floor, {{405.f, 200.f}, {100.f, 200.f}}, 96.f}
    };
    world.objects[0].position = world.agents[0].position = {360.f, 300.f}; world.guards[0].position = {305.f, 300.f};
    world.objects[0].elevation = world.agents[0].elevation = world.guards[0].elevation = 96.f;
    world.objects[0].supportId = world.agents[0].supportId = world.guards[0].supportId = 10;
    Step(simulation);
    Require(world.objects[0].overturned && !world.agents[0].hidden && world.agents[0].supportId == 11 && world.agents[0].elevation == 96.f,
      "Expulsar hacia un apoyo vecino conserva la planta de la mesa en lugar del suelo de llegada");
    Simulation replica; std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "La expulsión de una mesa elevada genera una instantánea con apoyo incoherente");
  }
  void DifferentOccupantIsDetected() {
    Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable, {700.f, 650.f});
    auto occupant = snapshot["agents"][0]; occupant["id"] = 2; occupant["name"] = "Ocupante actual"; occupant["position"] = {300.f, 300.f}; occupant["stance"] = static_cast<int>(Stance::Prone);
    occupant["hidden"] = true; occupant["hideoutId"] = 77; occupant["hideoutExit"] = {230.f, 300.f}; occupant.erase("decoyGuide"); occupant.erase("privateOrder"); occupant.erase("privateComplete"); occupant.erase("spectatorTarget"); snapshot["agents"].push_back(occupant);
    snapshot["objects"][0]["occupantAgent"] = 2; snapshot["objects"][0]["occupied"] = true;
    Simulation donor; donor.ResetBase(); donor.Deploy(1); auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot); auto guard = donorSnapshot["guards"][0];
    guard["id"] = 10; guard["position"] = {245.f, 300.f}; guard["facing"] = {1.f, 0.f}; guard["state"] = static_cast<int>(GuardState::Search);
    guard["observedAgent"] = 1; guard["suspicion"] = 1.f; guard["knownHideout"] = 77; guard["hideoutInspectionTime"] = 1.4f;
    guard["patrol"] = Json::array(); guard["waypoint"] = 0; guard["attackCooldown"] = 60.f; snapshot["guards"] = Json::array({guard});
    Load(simulation, snapshot); Step(simulation);
    Require(simulation.GetState().objects[0].overturned && !simulation.GetState().agents[1].hidden, "La inspección legítima no expone al nuevo ocupante de la mesa");
    Require(std::ranges::any_of(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection && event.actor == 2 && event.target == 10; }), "Exponer un ocupante diferente omite su detección y conserva Maestro erróneamente");
  }
  void InspectionWaitsForFreeTableAndExit() {
    Simulation simulation; auto snapshot = Scenario(simulation, HideoutKind::UnderTable);
    auto &actor = snapshot["agents"][0]; actor["position"] = {300.f, 300.f}; actor["hidden"] = true; actor["hideoutId"] = 77; actor["stance"] = static_cast<int>(Stance::Prone);
    snapshot["objects"][0]["occupantAgent"] = 1; snapshot["objects"][0]["occupied"] = true;
    Simulation donor; donor.ResetBase(); donor.Deploy(1); auto guardState = donor.SnapshotFor(1); TestFixture::LegacyPlane(guardState);
    auto guard = guardState["guards"][0]; guard["id"] = 10; guard["position"] = {245.f, 300.f}; guard["facing"] = {1.f, 0.f};
    guard["state"] = static_cast<int>(GuardState::Search); guard["knownHideout"] = 77; guard["hideoutInspectionTime"] = SpatialConfig::HideoutInspectionDuration;
    guard["patrol"] = Json::array(); guard["waypoint"] = 0; guard["attackCooldown"] = 60.f; snapshot["guards"] = Json::array({guard});
    snapshot["obstacles"].push_back({{"position", {340.f, 270.f}}, {"size", {4.f, 60.f}}, {"cover", 2}, {"solid", true}, {"name", "Salida bloqueada"}});
    Load(simulation, snapshot); Step(simulation);
    Require(simulation.GetState().agents[0].hidden && !simulation.GetState().objects[0].overturned && !simulation.GetState().objects[0].lifted,
      "La inspección fuerza una salida bloqueada o vuelca la mesa sobre un sólido");
    snapshot = simulation.SnapshotFor(1); snapshot["obstacles"] = Json::array(); Load(simulation, snapshot); Step(simulation);
    const auto &world = simulation.GetState();
    Require(world.objects[0].overturned && !world.agents[0].hidden && !WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius),
      "La inspección no se completa al liberar el volumen de mesa y salida");
    snapshot = simulation.SnapshotFor(1); snapshot["objects"][0]["overturned"] = false; snapshot["objects"][0]["lifted"] = true;
    std::string error;
    Require(!simulation.LoadSnapshot(snapshot, error), "La réplica acepta de nuevo una mesa flotante sin colisión");
  }
}

int main() {
  try {
    UnderTableSight(); ClosetSight(); ClosetEntranceAndExit(); HiddenOrientationAndNetworkPrivacy(); InstantTableTransitions(); UnderTableExposureAndCamera(); UnderTablePrivateDirectionalSnapshots(); UnderTableExitFollowsCursor(); WitnessedInspection(); InspectorClearsTheFutureTableFootprint(); InspectionExpelsThroughAFreeSideAndPausesDuringCqc(); InspectionPreservesTheLandingSupport(); DifferentOccupantIsDetected(); InspectionWaitsForFreeTableAndExit();
    std::cout << "PASS escondites avanzados: " << checks << " comprobaciones de visión, aberturas, accesos e inspección observada\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL escondites avanzados tras " << checks << ": " << error.what() << '\n'; return 1; }
}
