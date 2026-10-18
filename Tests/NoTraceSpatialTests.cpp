#include "NoTraceSimulation.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceMultilevelTests.h"
#include "NoTraceFurniture.h"
#include "NoTraceInteraction.h"
#include "Config/NoTraceNavigationConfig.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  using Json = nlohmann::json;
  unsigned int checks = 0;

  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }

  void Load(Simulation &simulation, const Json &snapshot) {
    std::string error;
    if (!simulation.LoadSnapshot(snapshot, error)) throw std::runtime_error("Fixture espacial: " + error);
  }

  Json Empty(Simulation &simulation, sf::Vector2f position = {120.f, 200.f}) {
    simulation.ResetBase();
    auto snapshot = simulation.SnapshotFor(1);
    TestFixture::LegacyPlane(snapshot);
    snapshot["mode"] = static_cast<int>(SimulationMode::Operation);
    snapshot["obstacles"] = Json::array(); snapshot["objects"] = Json::array(); snapshot["guards"] = Json::array();
    snapshot["agents"][0]["position"] = {position.x, position.y};
    return snapshot;
  }

  Json Object(ObjectKind kind, sf::Vector2f position, std::uint32_t id = 77) {
    Simulation donor; donor.ResetBase();
    auto snapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(snapshot);
    auto object = snapshot["objects"][0];
    object["kind"] = static_cast<int>(kind); object["id"] = id;
    object["position"] = {position.x, position.y}; object["name"] = "Objeto de prueba";
    return object;
  }

  Json Wall(sf::FloatRect bounds, CoverKind cover = CoverKind::High) {
    Simulation donor; donor.ResetBase();
    auto snapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(snapshot);
    auto obstacle = snapshot["obstacles"][0];
    obstacle["position"] = {bounds.position.x, bounds.position.y}; obstacle["size"] = {bounds.size.x, bounds.size.y};
    obstacle["cover"] = static_cast<int>(cover); obstacle["name"] = "Pared de prueba";
    return obstacle;
  }

  void Advance(Simulation &simulation, const ActionCommand &command = {}, unsigned int frames = 1, float dt = 0.1f) {
    for (unsigned int frame = 0; frame < frames; ++frame) simulation.Step(dt, {{1, command}});
  }

  const Agent &Local(const Simulation &simulation) { return simulation.GetState().agents.front(); }

  void RequireClear(const Simulation &simulation, sf::Vector2f position) {
    const auto &world = simulation.GetState();
    Require(position.x >= GameplayConfig::AgentRadius && position.y >= GameplayConfig::AgentRadius && position.x <= world.size.x - GameplayConfig::AgentRadius && position.y <= world.size.y - GameplayConfig::AgentRadius, "El movimiento salió del mapa");
    WorldGeometry::ForEachSolid(world, [&](const sf::FloatRect &bounds) { Require(!WorldGeometry::CircleOverlaps(position, GameplayConfig::AgentRadius, bounds), "El cuerpo penetró una huella sólida"); });
  }

  void Walk(Simulation &simulation, sf::Vector2f target) {
    for (unsigned int frame = 0; frame < 500; ++frame) {
      auto offset = target - Local(simulation).position;
      if (offset.length() <= 3.f) return;
      offset /= std::max(offset.length(), GameplayConfig::WalkSpeed * 0.1f);
      ActionCommand command; command.moveX = offset.x; command.moveY = offset.y;
      Advance(simulation, command); RequireClear(simulation, Local(simulation).position);
    }
    throw std::runtime_error("No se pudo rodear el objeto por espacio libre");
  }

  void PropertiesAndFootprints() {
    Interactable object; object.kind = ObjectKind::Operations; object.position = {200.f, 200.f};
    Require(WorldGeometry::ObjectFootprint(object).size == SpatialConfig::ObjectGroundSizes[0], "La mesa no comparte su huella tipada");
    Require(WorldGeometry::BlocksMovement(object), "Las mesas siguen siendo atravesables");
    Require(!WorldGeometry::BlocksVision(object, Stance::Standing) && WorldGeometry::BlocksVision(object, Stance::Prone), "La cobertura baja no distingue la postura");
    Require(!WorldGeometry::SupportsWallCover(object), "Una mesa baja admite cobertura de pared");
    Require(WorldGeometry::DistanceToBounds({200.f, 240.f}, WorldGeometry::ObjectFootprint(object)) == 16.f, "La distancia de interacción no usa el borde de la mesa");
    object.footprint = {120.f, 80.f};
    Require(WorldGeometry::ObjectFootprint(object).size == object.footprint, "La huella personalizada no sustituye la predeterminada");
    object.solid = SpatialOverride::Disabled;
    Require(!WorldGeometry::BlocksMovement(object), "La propiedad atravesable no se respeta");
    object.decorative = true;
    Require(WorldGeometry::Properties(object).decorative && !WorldGeometry::Properties(object).interactive && !WorldGeometry::BlocksMovement(object), "El objeto decorativo atravesable conserva interacción");
    object.solid = SpatialOverride::Enabled;
    Require(WorldGeometry::BlocksMovement(object) && !WorldGeometry::Properties(object).interactive, "La decoración sólida confunde colisión e interacción");
    for (const auto kind : {ObjectKind::Camera, ObjectKind::Deployment, ObjectKind::Extraction}) {
      object = {}; object.kind = kind;
      Require(!WorldGeometry::BlocksMovement(object), "Un marcador o escondite bloquea el movimiento");
    }
    object = {}; object.kind = ObjectKind::Hideout;
    Require(WorldGeometry::BlocksMovement(object), "Un contenedor de ocultación carece de huella sólida");
    object = {}; object.kind = ObjectKind::Armory;
    Require(WorldGeometry::SupportsWallCover(object) && WorldGeometry::BlocksVision(object), "El armario alto no ofrece cobertura sólida");
    object.blocksVision = SpatialOverride::Disabled;
    Require(!WorldGeometry::BlocksVision(object) && !WorldGeometry::SupportsWallCover(object), "Una superficie transparente permite cobertura opaca");
    object = {}; object.kind = ObjectKind::Door;
    Require(WorldGeometry::BlocksMovement(object) && WorldGeometry::BlocksVision(object), "La puerta cerrada no bloquea");
    object.open = true;
    Require(!WorldGeometry::BlocksMovement(object) && !WorldGeometry::BlocksVision(object), "La puerta abierta sigue bloqueando");
  }

  void MilitaryServiceTableHasFlipAndExitSpace() {
    for (const auto seed : {0u, 1u, 2u, 3u}) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(seed);
      const auto &world = simulation.GetState();
      const auto table = std::ranges::find(world.objects, 20u, &Interactable::id);
      Require(table != world.objects.end() && table->position == sf::Vector2f{380.f, 610.f} && table->supportId == 1 && table->elevation == 0.f,
        "La mesa de servicio no queda íntegra sobre el suelo norte de la misión");
      Require(!table->overturned && Furniture::TablePlacementClear(world, *table, false) && Furniture::TablePlacementClear(world, *table, true),
        "La huella normal o volcada de la mesa militar invade otro apoyo, muro o patrulla inicial");
    }
    Simulation simulation; auto &world = MultilevelTests::QuietMission(simulation); auto &agent = world.agents.front();
    auto &table = *std::ranges::find(world.objects, 20u, &Interactable::id);
    agent.position = table.position + sf::Vector2f{61.f, 0.f}; agent.elevation = table.elevation; agent.supportId = table.supportId;
    Require(Furniture::CanFlipTable(world, agent, table), "El acceso real a la mesa militar no permite usar T");
    ActionCommand flip; flip.tool = true; simulation.Step(1.f / 60.f, {{agent.id, flip}});
    Require(table.overturned && Furniture::TablePlacementClear(world, table, true), "T no vuelca la mesa militar en su espacio físico libre");
    for (const auto direction : std::array<sf::Vector2f, 4>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}}}) {
      auto departing = agent; departing.position = table.position; departing.stance = Stance::Prone; departing.weaponAim = direction;
      sf::Vector2f exit;
      Require(Interaction::HideoutExit(world, departing, table, exit), "La mesa militar volcada bloquea una salida física del ocupante");
      const auto support = WorldGeometry::FindSupport(world, exit, table.elevation, table.supportId, GameplayConfig::AgentRadius);
      Require(support && !WorldGeometry::PositionBlocked(world, exit, GameplayConfig::AgentRadius, support->elevation, CombatConfig::ProneHeight, support->id) &&
          Furniture::TablePlacementClear(world, table, true, agent.id, exit), "La expulsión de la mesa militar termina sin apoyo o dentro de la mesa o el muro");
    }
    Advance(simulation, {}, 60, 1.f / 60.f); simulation.Step(1.f / 60.f, {{agent.id, flip}});
    Require(!table.overturned, "T no restablece la mesa militar para volver a ocultarse");
    Advance(simulation, {}, 60, 1.f / 60.f);
    ActionCommand enter; enter.interact = true; enter.aimX = -1.f; simulation.Step(1.f / 60.f, {{agent.id, enter}});
    Require(agent.hidden && agent.hideoutId == table.id && agent.supportId == 1, "E no entra bajo la mesa militar con el apoyo real");
    ActionCommand leave; leave.interact = true; leave.aimY = -1.f; simulation.Step(1.f / 60.f, {{agent.id, leave}});
    Require(!agent.hidden && agent.hideoutId == 0 && agent.position.y < table.position.y && agent.supportId == 1 &&
        !WorldGeometry::PositionBlocked(world, agent.position, GameplayConfig::AgentRadius, agent.elevation, WorldGeometry::ActorHeight(agent), agent.supportId),
      "E no devuelve al ocupante a un suelo libre por el lado del cursor de la mesa militar");
    MultilevelTests::Replica(simulation);
  }

  void SolidMovement() {
    Simulation simulation;
    auto snapshot = Empty(simulation);
    snapshot["objects"].push_back(Object(ObjectKind::Operations, {200.f, 200.f})); Load(simulation, snapshot);
    ActionCommand run; run.moveX = 1.f; run.run = true;
    for (unsigned int frame = 0; frame < 35; ++frame) { Advance(simulation, run); RequireClear(simulation, Local(simulation).position); }
    const float tableEdge = WorldGeometry::ObjectFootprint(simulation.GetState().objects[0]).position.x - GameplayConfig::AgentRadius;
    Require(Local(simulation).position.x <= tableEdge + SpatialConfig::CollisionSkin && Local(simulation).position.x >= tableEdge - SpatialConfig::MaximumMovementStep, "La mesa no bloquea cerca de su huella real");
    for (const auto waypoint : {sf::Vector2f{150.f, 145.f}, {270.f, 145.f}, {270.f, 255.f}, {200.f, 255.f}}) Walk(simulation, waypoint);
    Require(Local(simulation).position.y > 240.f, "El agente no rodea la mesa");

    snapshot = Empty(simulation, {160.f, 160.f});
    snapshot["obstacles"].push_back(Wall({{200.f, 100.f}, {1.f, 200.f}})); Load(simulation, snapshot);
    run.moveY = 1.f;
    for (unsigned int frame = 0; frame < 15; ++frame) {
      Advance(simulation, run, 1, 10.f); RequireClear(simulation, Local(simulation).position);
      if (Local(simulation).position.y < 295.f) Require(Local(simulation).position.x <= 188.01f, "La diagonal rápida atraviesa una pared delgada");
    }

    snapshot = Empty(simulation);
    snapshot["objects"].push_back(Object(ObjectKind::Door, {200.f, 200.f})); Load(simulation, snapshot);
    run.moveY = 0.f; Advance(simulation, run, 20);
    Require(Local(simulation).position.x <= 176.01f, "La puerta cerrada permite atravesarla");
    snapshot["objects"][0]["open"] = true; Load(simulation, snapshot); Advance(simulation, run, 20);
    Require(Local(simulation).position.x > 240.f, "La puerta abierta impide el paso");

    snapshot = Empty(simulation, {200.f, 200.f});
    snapshot["objects"].push_back(Object(ObjectKind::Operations, {200.f, 200.f})); Load(simulation, snapshot); Advance(simulation);
    RequireClear(simulation, Local(simulation).position);
    const auto recovered = Local(simulation).position;
    Require((recovered - sf::Vector2f{200.f, 200.f}).length() < 50.f, "La corrección de penetración desplaza al agente arbitrariamente lejos");
    Advance(simulation, {}, 20);
    Require(Local(simulation).position == recovered, "La corrección de penetración vibra contra la mesa");
  }

  void GuardNavigation() {
    Simulation simulation, donor;
    auto snapshot = Empty(simulation, {100.f, 650.f}); snapshot["phase"] = static_cast<int>(GamePhase::Mission);
    snapshot["agents"][0]["hidden"] = true;
    donor.ResetBase(); donor.Deploy(0);
    auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot); auto guard = donorSnapshot["guards"][0];
    guard["position"] = {120.f, 200.f}; guard["patrol"] = Json::array({{400.f, 200.f}}); guard["waypoint"] = 0;
    guard["facing"] = {1.f, 0.f}; guard["state"] = static_cast<int>(GuardState::Patrol);
    snapshot["guards"].push_back(guard); snapshot["objects"].push_back(Object(ObjectKind::Armory, {250.f, 200.f}));
    Load(simulation, snapshot);
    for (unsigned int frame = 0; frame < 130; ++frame) { Advance(simulation); RequireClear(simulation, simulation.GetState().guards[0].position); }
    Require(simulation.GetState().guards[0].position.x > 360.f, "La navegación enemiga no rodea el mismo objeto sólido del jugador");
    snapshot["guards"][0]["position"] = {250.f, 200.f}; Load(simulation, snapshot); Advance(simulation);
    RequireClear(simulation, simulation.GetState().guards[0].position);
  }

  void GuardCornerRecovery() {
    for (const auto state : {GuardState::Patrol, GuardState::Investigate, GuardState::Search, GuardState::Alert}) {
      Simulation simulation, donor;
      auto snapshot = Empty(simulation, {100.f, 650.f}); snapshot["phase"] = static_cast<int>(GamePhase::Mission); snapshot["agents"][0]["hidden"] = true;
      donor.ResetBase(); donor.Deploy(0); auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot); auto guard = donorSnapshot["guards"][0];
      guard["position"] = {160.f, 220.f}; guard["patrol"] = Json::array({{450.f, 220.f}}); guard["waypoint"] = 0;
      guard["facing"] = {1.f, 0.f}; guard["state"] = static_cast<int>(state); guard["lastKnown"] = {450.f, 220.f};
      snapshot["guards"] = Json::array({guard});
      snapshot["obstacles"] = Json::array({Wall({{230.f, 150.f}, {24.f, 220.f}}), Wall({{230.f, 150.f}, {150.f, 20.f}})});
      Load(simulation, snapshot);
      for (unsigned int frame = 0; frame < 180; ++frame) {
        const auto previous = simulation.GetState().guards[0].position; Advance(simulation);
        const auto position = simulation.GetState().guards[0].position;
        RequireClear(simulation, position);
        Require((position - previous).length() <= GameplayConfig::GuardAlertSpeed * 0.1f + SpatialConfig::CollisionSkin, "La recuperación de esquina teletransporta al guardia");
      }
      Require(simulation.GetState().guards[0].position.x > 420.f, "La patrulla, búsqueda o investigación queda oscilando antes de una esquina");
    }
    Simulation simulation, donor;
    auto snapshot = Empty(simulation, {100.f, 650.f}); snapshot["phase"] = static_cast<int>(GamePhase::Mission); snapshot["agents"][0]["hidden"] = true;
    donor.ResetBase(); donor.Deploy(0); auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot); auto guard = donorSnapshot["guards"][0];
    guard["position"] = {140.f, 300.f}; guard["patrol"] = Json::array({{300.f, 300.f}, {140.f, 500.f}}); guard["waypoint"] = 0;
    guard["facing"] = {1.f, 0.f}; guard["state"] = static_cast<int>(GuardState::Patrol);
    snapshot["guards"] = Json::array({guard}); snapshot["obstacles"] = Json::array({Wall({{250.f, 250.f}, {100.f, 100.f}})}); Load(simulation, snapshot);
    bool rejected = false, alternative = false;
    for (unsigned int frame = 0; frame < 160; ++frame) {
      const auto previous = simulation.GetState().guards[0].position; Advance(simulation); const auto &current = simulation.GetState().guards[0];
      RequireClear(simulation, current.position);
      Require((current.position - previous).length() <= GameplayConfig::GuardAlertSpeed * 0.1f + SpatialConfig::CollisionSkin, "Rechazar un destino inaccesible cambia instantáneamente la posición");
      rejected |= current.navigationRejectTime > 0.f && current.navigationFailures >= NavigationConfig::RejectFailure;
      alternative |= current.position.y > 470.f;
    }
    Require(rejected && alternative, "El destino dentro de una pared no se rechaza temporalmente ni permite continuar la patrulla");
    snapshot["obstacles"] = Json::array({Wall({{200.f, 200.f}, {100.f, 100.f}})});
    snapshot["guards"][0]["position"] = {190.f, 190.f}; snapshot["guards"][0]["patrol"] = Json::array({{350.f, 160.f}}); Load(simulation, snapshot);
    for (unsigned int frame = 0; frame < 60; ++frame) { Advance(simulation); RequireClear(simulation, simulation.GetState().guards[0].position); }
    Require(simulation.GetState().guards[0].position.x > 320.f, "La huella circular válida junto a una esquina queda atrapada por una expansión rectangular ficticia");
  }

  void IncrementalGuardNavigation() {
    Simulation simulation, donor;
    auto snapshot = Empty(simulation, {100.f, 650.f}); snapshot["phase"] = static_cast<int>(GamePhase::Mission); snapshot["agents"][0]["hidden"] = true;
    donor.ResetBase(); donor.Deploy(0); auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot);
    auto guard = donorSnapshot["guards"][0];
    guard["facing"] = {1.f, 0.f}; guard["state"] = static_cast<int>(GuardState::Patrol); guard["waypoint"] = 0;
    for (unsigned int index = 0; index < 3; ++index) {
      guard["id"] = 901u + index; guard["position"] = {120.f, 200.f + index * 20.f};
      guard["patrol"] = Json::array({{index == 0 ? 280.f : 500.f, 200.f + index * 20.f}});
      snapshot["guards"].push_back(guard);
    }
    snapshot["obstacles"] = Json::array({Wall({{240.f, 140.f}, {80.f, 160.f}})}); Load(simulation, snapshot);
    const std::array<sf::Vector2f, 3> origins{{{120.f, 200.f}, {120.f, 220.f}, {120.f, 240.f}}};
    Advance(simulation, {}, 1, 1.f / 60.f);
    for (std::size_t index = 0; index < origins.size(); ++index)
      Require(simulation.GetState().guards[index].position == origins[index] && simulation.GetState().guards[index].navigation.empty(), "Una búsqueda incompleta mueve al guardia o consume toda la rejilla en el primer tick");
    std::array<bool, 3> moved{};
    bool sharedBeforeImpossibleFinished = false;
    auto &world = const_cast<WorldState &>(simulation.GetState());
    for (unsigned int frame = 0; frame < 30; ++frame) {
      world.guards[1].patrol[0].x += 0.25f;
      const std::array<sf::Vector2f, 3> before{{world.guards[0].position, world.guards[1].position, world.guards[2].position}};
      Advance(simulation, {}, 1, 1.f / 60.f);
      for (std::size_t index = 0; index < moved.size(); ++index) {
        moved[index] = moved[index] || world.guards[index].position != origins[index];
        RequireClear(simulation, world.guards[index].position);
        Require((world.guards[index].position - before[index]).length() <= GameplayConfig::GuardSpeed / 60.f + SpatialConfig::CollisionSkin, "Una ruta incremental teletransporta al guardia");
      }
      sharedBeforeImpossibleFinished = sharedBeforeImpossibleFinished || (!moved[0] && moved[1] && moved[2]);
    }
    Require(sharedBeforeImpossibleFinished && moved[1] && moved[2], "El primer destino imposible acapara la cola o un objetivo móvil impide avanzar en medio segundo");

    snapshot["guards"] = Json::array({snapshot["guards"][1]}); snapshot["guards"][0]["position"] = {120.f, 200.f}; snapshot["guards"][0]["patrol"] = Json::array({{700.f, 200.f}});
    snapshot["obstacles"] = Json::array({Wall({{160.f, 80.f}, {20.f, 320.f}}), Wall({{240.f, 0.f}, {24.f, 168.f}}), Wall({{240.f, 232.f}, {24.f, 528.f}})});
    auto door = Object(ObjectKind::Door, {252.f, 200.f}, 910); door["open"] = true; snapshot["objects"] = Json::array({door}); Load(simulation, snapshot);
    Advance(simulation, {}, 1, 1.f / 60.f);
    Require(world.guards.front().position == sf::Vector2f{120.f, 200.f} && world.guards.front().navigation.empty(), "La prueba de puerta no conserva una búsqueda pendiente");
    const auto pending = simulation.SnapshotFor(1);
    Simulation fresh; Load(fresh, pending); Load(simulation, pending);
    for (unsigned int frame = 0; frame < 50; ++frame) {
      Advance(simulation, {}, 1, 1.f / 60.f); Advance(fresh, {}, 1, 1.f / 60.f);
      Require(world.guards.front().position == fresh.GetState().guards.front().position, "Una búsqueda privada sobrevive al reemplazo aceptado del snapshot");
    }
    const auto doorRouteLength = [&] {
      const auto &moving = world.guards.front(); auto previous = moving.position;
      float remaining = 0.f;
      for (const auto point : moving.navigation) { remaining += (point - previous).length(); previous = point; }
      return remaining;
    };
    const auto describeDoorRoute = [&](const char *stage, unsigned int ticks, float travelled) {
      const auto &moving = world.guards.front(); const float remaining = doorRouteLength();
      std::cout << "Ruta puerta etapa=" << stage << " ticks=" << ticks << " pos=" << moving.position.x << ',' << moving.position.y << " estado=" << static_cast<int>(moving.state)
        << " nodos=" << moving.navigation.size() << " longitud=" << remaining << " recorrido=" << travelled << " minimoTicks=" << remaining / GameplayConfig::GuardSpeed * 60.f
        << " objetivo=" << moving.navigationTarget.x << ',' << moving.navigationTarget.y << " fallos=" << moving.navigationFailures << " atasco=" << moving.navigationStuckTime
        << " rechazo=" << moving.navigationRejectTime << " refresh=" << moving.navigationTimer << " abierta=" << world.objects.front().open;
      if (!moving.navigation.empty()) std::cout << " siguiente=" << moving.navigation.front().x << ',' << moving.navigation.front().y;
      std::cout << '\n';
    };
    Load(simulation, snapshot); Advance(simulation, {}, 1, 1.f / 60.f); world.objects.front().open = false;
    float closedTravelled = 0.f;
    for (unsigned int frame = 0; frame < 80; ++frame) {
      const auto previous = world.guards.front().position;
      Advance(simulation, {}, 1, 1.f / 60.f); RequireClear(simulation, world.guards.front().position);
      closedTravelled += (world.guards.front().position - previous).length();
      Require(world.guards.front().position.x <= 240.f - GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin, "Una búsqueda pendiente conserva una ruta a través de una puerta que se cerró");
    }
    describeDoorRoute("cerrada", 80, closedTravelled);
    // La ruta válida rodea el extremo del muro; el plazo incluye su distancia y la cuantización de cada waypoint.
    const auto openedFrameLimit = static_cast<unsigned int>(std::ceil(doorRouteLength() / GameplayConfig::GuardSpeed * 60.f)) +
        static_cast<unsigned int>(world.guards.front().navigation.size()) * 2u + 60u;
    world.objects.front().open = true;
    float openedTravelled = 0.f, previousProgress = 0.f;
    unsigned int openedFrames = 0;
    for (; openedFrames < openedFrameLimit && world.guards.front().position.x <= 300.f; ++openedFrames) {
      const auto previous = world.guards.front().position;
      Advance(simulation, {}, 1, 1.f / 60.f); RequireClear(simulation, world.guards.front().position);
      const float progress = (world.guards.front().position - previous).length(); openedTravelled += progress;
      Require(progress <= GameplayConfig::GuardSpeed / 60.f + SpatialConfig::CollisionSkin, "La reapertura teletransporta al guardia para cumplir el plazo");
      if ((openedFrames + 1) % 60 == 0) {
        Require(openedTravelled > previousProgress + 1.f, "Una ruta física válida deja de avanzar durante un segundo tras abrir la puerta");
        previousProgress = openedTravelled;
      }
    }
    describeDoorRoute("abierta", openedFrames, openedTravelled);
    std::cout << "Ruta puerta plazo derivado=" << openedFrameLimit << " ticks\n";
    Require(world.guards.front().position.x > 300.f, "La búsqueda no recupera una ruta física después de abrir la puerta");
  }

  void SixGuardNavigationRespondsWithinOneSecond() {
    Simulation simulation, donor;
    auto snapshot = Empty(simulation, {100.f, 650.f}); snapshot["phase"] = static_cast<int>(GamePhase::Mission); snapshot["agents"][0]["hidden"] = true;
    donor.ResetBase(); donor.Deploy(0); auto donorSnapshot = donor.SnapshotFor(1); TestFixture::LegacyPlane(donorSnapshot);
    auto guard = donorSnapshot["guards"][0];
    guard["facing"] = {1.f, 0.f}; guard["state"] = static_cast<int>(GuardState::Patrol); guard["waypoint"] = 0;
    std::array<sf::Vector2f, 6> origins;
    for (std::size_t index = 0; index < origins.size(); ++index) {
      origins[index] = {120.f, 180.f + static_cast<float>(index) * 12.f};
      guard["id"] = 921u + index; guard["position"] = {origins[index].x, origins[index].y};
      guard["patrol"] = Json::array({{index == 0 ? 280.f : 500.f, origins[index].y}}); snapshot["guards"].push_back(guard);
    }
    snapshot["obstacles"] = Json::array({Wall({{240.f, 140.f}, {80.f, 160.f}})}); Load(simulation, snapshot);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    std::array<unsigned int, 6> firstMovement{};
    for (unsigned int frame = 1; frame <= 60; ++frame) {
      world.guards[1].patrol[0].x += 0.25f;
      std::array<sf::Vector2f, 6> before;
      for (std::size_t index = 0; index < before.size(); ++index) before[index] = world.guards[index].position;
      Advance(simulation, {}, 1, 1.f / 60.f);
      for (std::size_t index = 0; index < before.size(); ++index) {
        if (firstMovement[index] == 0 && world.guards[index].position != origins[index]) firstMovement[index] = frame;
        RequireClear(simulation, world.guards[index].position);
        Require((world.guards[index].position - before[index]).length() <= GameplayConfig::GuardSpeed / 60.f + SpatialConfig::CollisionSkin, "Compartir la búsqueda entre seis guardias permite atravesar una esquina o teletransportarse");
      }
    }
    for (std::size_t index = 1; index < firstMovement.size(); ++index) {
      std::cout << "Navegación seis guardias id=" << world.guards[index].id << " primer movimiento=" << firstMovement[index] << "/60 ticks\n";
      Require(firstMovement[index] > 0 && firstMovement[index] <= 60, "Un destino imposible o móvil impide que las otras cinco rutas comunes avancen en un segundo");
    }
  }

  void WallCoverAndPeek() {
    Simulation simulation;
    auto snapshot = Empty(simulation, {175.f, 180.f});
    snapshot["obstacles"].push_back(Wall({{200.f, 150.f}, {30.f, 200.f}})); Load(simulation, snapshot);
    ActionCommand enter; enter.coverToggle = true; Advance(simulation, enter);
    Require(Local(simulation).cover.active && Local(simulation).cover.normal == sf::Vector2f{-1.f, 0.f}, "F no identifica la cara real de una pared alta");
    const auto anchor = Local(simulation).cover.anchor;
    Require(!simulation.CanSee({300.f, 132.f}, {-1.f, 0.f}, Local(simulation)), "La pared no protege la posición de cobertura");
    ActionCommand peek; peek.peekLeft = true; peek.aimX = 1.f;
    for (unsigned int frame = 0; frame < 6; ++frame) { Advance(simulation, peek); RequireClear(simulation, Local(simulation).position); }
    Require(Local(simulation).cover.peekSide == PeekSide::Left && Local(simulation).position.y < 150.f, "Asomarse no desplaza el cuerpo físicamente alrededor del extremo");
    Require(simulation.CanSee({300.f, 132.f}, {-1.f, 0.f}, Local(simulation)), "El asomado real no modifica la percepción enemiga");
    Advance(simulation, {}, 6);
    Require((Local(simulation).position - anchor).length() <= SpatialConfig::CoverPositionTolerance && Local(simulation).cover.peekSide == PeekSide::None, "Soltar el asomado no devuelve al punto protegido");
    peek.peekLeft = false; peek.peekRight = true; Advance(simulation, peek, 6);
    Require((Local(simulation).position - anchor).length() <= SpatialConfig::CoverPositionTolerance && !Local(simulation).cover.canPeekRight, "El agente puede asomarse por un extremo demasiado lejano");
    ActionCommand slide; slide.moveY = 1.f; Advance(simulation, slide, 20);
    Require(Local(simulation).cover.active && Local(simulation).cover.anchor.y > 295.f, "El movimiento no desliza el ancla a lo largo de la pared");
    Advance(simulation, peek, 6);
    Require(Local(simulation).position.y > 350.f && Local(simulation).cover.peekSide == PeekSide::Right, "El agente no cambia al otro extremo al deslizarse");
    Advance(simulation, {}, 6);
    ActionCommand prone; prone.prone = true; Advance(simulation, prone);
    Require(!Local(simulation).cover.active && Local(simulation).stance == Stance::Prone, "Tumbarse no abandona la cobertura");

    snapshot["obstacles"].push_back(Wall({{160.f, 115.f}, {60.f, 20.f}})); Load(simulation, snapshot); Advance(simulation, enter);
    peek.peekLeft = true; peek.peekRight = false; Advance(simulation, peek, 6);
    Require(Local(simulation).cover.active && !Local(simulation).cover.canPeekLeft && (Local(simulation).position - Local(simulation).cover.anchor).length() < 0.1f, "El asomado atraviesa la geometría de una esquina obstruida");

    snapshot = Empty(simulation, {175.f, 180.f}); snapshot["obstacles"].push_back(Wall({{200.f, 150.f}, {30.f, 200.f}}, CoverKind::Low)); Load(simulation, snapshot); Advance(simulation, enter);
    Require(!Local(simulation).cover.active, "La cobertura de pared admite cualquier obstáculo bajo");
  }

  void AutomaticCoverEdgeAndBack() {
    Simulation simulation;
    auto snapshot = Empty(simulation, {175.f, 155.f}); snapshot["phase"]=static_cast<int>(GamePhase::Mission);
    snapshot["obstacles"].push_back(Wall({{200.f,150.f},{30.f,200.f}})); Load(simulation,snapshot);
    ActionCommand enter; enter.coverToggle=true; Advance(simulation,enter,1);
    Advance(simulation,{},8);
    Require(Local(simulation).cover.peekSide==PeekSide::Left && Local(simulation).position.y<150.f,"El extremo no activa el asomado automáticamente");
    Require(Local(simulation).facing==Local(simulation).cover.normal,"La cobertura no mantiene la espalda contra la pared");
    ActionCommand inward; inward.moveY=1.f; Advance(simulation,inward,15); Advance(simulation,{},8);
    Require(Local(simulation).cover.peekSide==PeekSide::None && (Local(simulation).position-Local(simulation).cover.anchor).length()<.2f,"Volver hacia el centro no recupera la cobertura completa");
    ActionCommand knock; knock.knock=true; Advance(simulation,knock);
    Require(Local(simulation).actionPose==ActionPose::KnockWall && Local(simulation).facing==Local(simulation).cover.normal,"Golpear la pared gira el cuerpo o inicia otro ataque");
    Require(!simulation.GetState().noises.empty() && simulation.GetState().noises.back().position.x==200.f,"El golpe no se origina en el contacto real con la pared");
  }

  void CoverBackUsesEveryPhysicalWallFace() {
    const sf::FloatRect wall{{300.f, 300.f}, {140.f, 120.f}};
    const std::array<sf::Vector2f, 4> normals{{{-1.f, 0.f}, {1.f, 0.f}, {0.f, -1.f}, {0.f, 1.f}}};
    const std::array<sf::Vector2f, 4> contacts{{{300.f, 360.f}, {440.f, 360.f}, {370.f, 300.f}, {370.f, 420.f}}};
    for (std::size_t side = 0; side < normals.size(); ++side) {
      Simulation simulation, replica;
      const auto normal = normals[side]; const sf::Vector2f tangent{-normal.y, normal.x};
      auto snapshot = Empty(simulation, contacts[side] + normal * 25.f); snapshot["phase"] = static_cast<int>(GamePhase::Mission);
      snapshot["obstacles"].push_back(Wall(wall)); Load(simulation, snapshot);
      ActionCommand enter; enter.coverToggle = true; enter.aimX = -normal.x; enter.aimY = -normal.y; Advance(simulation, enter);
      Require(Local(simulation).cover.active && Local(simulation).cover.normal == normal && Local(simulation).facing == normal,
        "Una cara lateral o proyectada en diagonal orienta el cuerpo hacia la pared o hacia el cursor");
      const auto contact = WorldGeometry::ClosestPoint(Local(simulation).position, wall);
      const auto outward = Local(simulation).position - contact;
      Require(outward.dot(Local(simulation).facing) > 0.f && std::abs(outward.length() - GameplayConfig::AgentRadius - SpatialConfig::CollisionSkin) < 0.01f,
        "La normal no sitúa la espalda junto a la cara física manteniendo el cuerpo fuera de la pared");
      for (const auto cursor : normals) {
        ActionCommand aim; aim.aiming = true; aim.aimX = cursor.x; aim.aimY = cursor.y; Advance(simulation, aim);
        Require(Local(simulation).facing == normal && Local(simulation).weaponAim == cursor, "Apuntar desde una cara de cobertura gira el cuerpo en lugar del arma");
      }
      ActionCommand slide; slide.moveX = tangent.x; slide.moveY = tangent.y; slide.aimX = -normal.x; slide.aimY = -normal.y; Advance(simulation, slide, 3);
      Require(Local(simulation).facing == normal && Local(simulation).cover.normal == normal && Local(simulation).moving, "Deslizarse lateralmente pierde la postura de espalda a la pared");
      RequireClear(simulation, Local(simulation).position);
      const auto beforeKnock = Local(simulation).position;
      ActionCommand knock; knock.cqc = true; knock.cqcHeld = true; knock.aimX = -normal.x; knock.aimY = -normal.y; Advance(simulation, knock);
      Require(Local(simulation).actionPose == ActionPose::KnockWall && Local(simulation).position == beforeKnock && Local(simulation).facing == normal && !Local(simulation).moving && !Local(simulation).running,
        "El golpe contextual mueve o gira el cuerpo en lugar de conservar el apoyo y animar sólo el brazo");
      Require(!simulation.GetState().noises.empty() && simulation.GetState().noises.back().position == WorldGeometry::ClosestPoint(beforeKnock, wall),
        "El golpe de una cara lateral no utiliza el contacto real de la pared como origen acústico");
      knock.cqc = false; Advance(simulation, knock, 10);
      Require(Local(simulation).position == beforeKnock && Local(simulation).facing == normal && std::ranges::count_if(simulation.GetState().events, [](const MissionEvent &event) { return event.kind == EventKind::Noise; }) == 1,
        "Mantener F repite el impacto o separa el cuerpo de su apoyo de cobertura");
      Load(replica, simulation.SnapshotFor(1));
      Require(Local(replica).cover.normal == normal && Local(replica).facing == normal, "La réplica pierde la normal física y la postura de una cara de cobertura");
      ActionCommand leave; leave.coverToggle = true; leave.moveX = tangent.x; leave.moveY = tangent.y; Advance(simulation, leave);
      Require(!Local(simulation).cover.active && Local(simulation).facing == tangent && Local(simulation).moving,
        "Salir de cobertura conserva un tick la orientación de la pared en lugar de recuperar WASD inmediatamente");
    }
  }

  void AutomaticPeekUsesTheCurrentAnchor() {
    Simulation simulation, replica;
    auto snapshot = Empty(simulation, {175.f, 174.f}); snapshot["phase"] = static_cast<int>(GamePhase::Mission);
    snapshot["obstacles"].push_back(Wall({{200.f, 150.f}, {30.f, 200.f}})); Load(simulation, snapshot);
    ActionCommand enter; enter.coverToggle = true; Advance(simulation, enter);
    Require(Local(simulation).cover.peekSide == PeekSide::None, "La fixture empieza asomada antes de alcanzar el extremo");
    ActionCommand slide; slide.moveY = -1.f; Advance(simulation, slide, 2);
    Require(Local(simulation).cover.anchor.y == 162.f && Local(simulation).cover.peekSide == PeekSide::Left,
      "Llegar al umbral de asomado usa el ancla del tick anterior");
    Load(replica, simulation.SnapshotFor(1));
    slide.moveY = 1.f; Advance(simulation, slide);
    Require(Local(simulation).cover.anchor.y == 168.f && Local(simulation).cover.peekSide == PeekSide::None && Local(simulation).position.y > 154.5f,
      "Alejarse del extremo conserva un asomado anterior y desplaza el cuerpo hacia la esquina contraria");
    Load(replica, simulation.SnapshotFor(1));
  }

  void MalformedCoverSnapshots() {
    Simulation simulation;
    auto snapshot = Empty(simulation, {175.f, 180.f});
    snapshot["obstacles"].push_back(Wall({{200.f, 150.f}, {30.f, 200.f}})); Load(simulation, snapshot);
    ActionCommand enter; enter.coverToggle = true; Advance(simulation, enter);
    const auto valid = simulation.SnapshotFor(1);
    Simulation replica; Load(replica, valid);
    Require(Local(replica).cover.active, "Una cobertura autoritativa válida no completa el recorrido de snapshot");
    for (int malformed = 0; malformed < 10; ++malformed) {
      auto invalid = valid;
      auto &cover = invalid["agents"][0]["cover"];
      if (malformed == 0) cover["normal"] = {0.70710678f, 0.70710678f};
      if (malformed == 1) cover["obstacle"] = 999;
      if (malformed == 2) { cover["obstacle"] = -1; cover["object"] = 999; }
      if (malformed == 3) cover["surfacePosition"][0] = 201.f;
      if (malformed == 4) cover["anchor"][0] = 185.f;
      if (malformed == 5) { cover["peekSide"] = static_cast<int>(PeekSide::Right); cover["canPeekRight"] = false; }
      if (malformed == 6) { cover["active"] = false; cover["peekSide"] = static_cast<int>(PeekSide::Left); }
      if (malformed == 7) invalid["agents"][0]["position"][0] = 140.f;
      if (malformed == 8) cover["object"] = 77;
      if (malformed == 9) cover["canPeekRight"] = true;
      std::string error;
      Require(!replica.LoadSnapshot(invalid, error) && !error.empty(), "El parser aceptó una cobertura incompatible con su geometría");
      Require(Local(replica).cover.anchor == Local(simulation).cover.anchor && Local(replica).position == Local(simulation).position, "Un rechazo de cobertura alteró el estado válido anterior");
    }
    ActionCommand peek; peek.peekLeft = true; Advance(simulation, peek, 6); Load(replica, simulation.SnapshotFor(1));
    Require(Local(replica).cover.peekSide == PeekSide::Left, "El snapshot no conserva el asomado válido");
    Advance(simulation); Load(replica, simulation.SnapshotFor(1));
    Require(Local(replica).cover.peekSide == PeekSide::None, "El snapshot rechaza el retorno físico hacia el ancla");
    snapshot = Empty(simulation, {670.f, 210.f}); snapshot["objects"].push_back(Object(ObjectKind::Armory, {720.f, 210.f}));
    Load(simulation, snapshot); Advance(simulation, enter); Load(replica, simulation.SnapshotFor(1));
    Require(Local(replica).cover.active && Local(replica).cover.object == 77, "El snapshot no valida cobertura en un objeto alto real");
  }
}

int main() {
  try {
    MultilevelTests::Run(); PropertiesAndFootprints(); MilitaryServiceTableHasFlipAndExitSpace(); SolidMovement(); GuardNavigation(); GuardCornerRecovery(); IncrementalGuardNavigation(); SixGuardNavigationRespondsWithinOneSecond(); WallCoverAndPeek(); AutomaticCoverEdgeAndBack(); CoverBackUsesEveryPhysicalWallFace(); AutomaticPeekUsesTheCurrentAnchor(); MalformedCoverSnapshots();
    std::cout << "PASS spatial: " << checks << " comprobaciones de huellas, colisiones, navegación, cobertura y asomado físico\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL spatial: " << error.what() << '\n'; return 1;
  }
}
