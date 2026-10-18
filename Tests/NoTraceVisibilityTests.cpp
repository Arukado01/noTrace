#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceVisibility.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceOcclusionGeometry.h"
#include "NoTraceWorldRenderer.h"
#include "Config/NoTraceVisibilityConfig.h"
#include "Config/NoTraceDistractionConfig.h"
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }
  WorldState Scenario() {
    WorldState world;
    world.phase = GamePhase::Mission;
    Agent first; first.id = 1; first.name = "Uno"; first.position = {120.f, 180.f};
    Agent second; second.id = 2; second.name = "Dos"; second.position = {320.f, 180.f}; second.facing = {-1.f, 0.f};
    world.agents = {first, second};
    world.obstacles.push_back({{{220.f, 80.f}, {20.f, 220.f}}, CoverKind::High, true, "Pared"});
    Guard near; near.id = 11; near.position = {165.f, 180.f}; near.patrol = {{165.f, 180.f}, {165.f, 240.f}};
    Guard hidden; hidden.id = 12; hidden.position = {300.f, 180.f};
    world.guards = {near, hidden};
    world.objects.push_back({30, ObjectKind::Terminal, {165.f, 220.f}, "Visible"});
    world.objects.push_back({31, ObjectKind::Terminal, {310.f, 220.f}, "Secreto"});
    return world;
  }
  void GeometryAndGrid() {
    auto world = Scenario();
    const auto &observer = world.agents[0];
    Require(Visibility::GuardVisible(world, observer, world.guards[0]), "El guardia en espacio abierto no es visible");
    Require(!Visibility::GuardVisible(world, observer, world.guards[1]), "La pared revela al guardia oculto");
    Require(!Visibility::IsVisible(world, observer, {60.f, 180.f}), "El campo de vision revela la espalda del agente");
    Require(Visibility::IsVisible(world, observer, {110.f, 180.f}), "El campo cercano oculta al propio agente");
    Require(!Visibility::IsVisible(world, observer, {800.f, 180.f}), "La vision carece de alcance finito");
    const auto grid = Visibility::BuildGrid(world, observer);
    Require(grid.observer == 1 && grid.cells.size() == static_cast<std::size_t>(grid.columns) * grid.rows, "La mascara no corresponde al observador");
    world.visibility = grid;
    Require(Visibility::GuardVisible(world, observer, world.guards[0]), "La mascara oculta el guardia conocido");
    Require(!Visibility::GuardVisible(world, observer, world.guards[1]), "La mascara autoritativa revela tras pared");
    world.visibility = {};
    world.obstacles.clear(); world.objects.clear();
    Interactable door{40, ObjectKind::Door, {220.f, 180.f}, "Puerta"};
    world.objects.push_back(door);
    Require(!Visibility::IsVisible(world, observer, {300.f, 180.f}), "Una puerta cerrada deja ver el pasillo");
    world.objects[0].open = true;
    Require(Visibility::IsVisible(world, observer, {300.f, 180.f}), "Una puerta abierta mantiene el bloqueo visual");
    world.objects.clear();
    world.obstacles.push_back({{{200.f, 140.f}, {25.f, 80.f}}, CoverKind::Low, true, "Caja baja"});
    Require(Visibility::IsVisible(world, world.agents[0], {300.f, 180.f}), "De pie no se puede observar sobre cobertura baja");
    world.agents[0].stance = Stance::Prone;
    Require(!Visibility::IsVisible(world, world.agents[0], {300.f, 180.f}), "Tumbado ve a traves de cobertura baja");
  }
  void ReducedPostureRanges() {
    WorldState world; world.phase = GamePhase::Mission;
    Agent observer; observer.id = 1; observer.position = {120.f, 400.f}; observer.facing = {1.f, 0.f};
    const std::array<float, 3> ranges{{VisibilityConfig::StandingRange, VisibilityConfig::CrouchedRange, VisibilityConfig::ProneRange}};
    const std::array<float, 3> references{{VisibilityConfig::ReferenceStandingRange, VisibilityConfig::ReferenceCrouchedRange, VisibilityConfig::ReferenceProneRange}};
    for (std::size_t index = 0; index < ranges.size(); ++index) {
      observer.stance = static_cast<Stance>(index);
      Require(ranges[index] == references[index] * VisibilityConfig::PlayerRangeScale && ranges[index] <= references[index] * 0.6f, "El alcance no conserva una reducción considerable configurable de su referencia");
      Require(Visibility::IsGeometricallyVisible(world, observer, observer.position + sf::Vector2f{ranges[index] - 1.f, 0.f}), "La reducción visual impide observar dentro del alcance de la postura");
      Require(!Visibility::IsGeometricallyVisible(world, observer, observer.position + sf::Vector2f{ranges[index] + 1.f, 0.f}), "La simulación sigue revelando entidades fuera del nuevo alcance");
      Require(!Visibility::IsGeometricallyVisible(world, observer, observer.position - sf::Vector2f{80.f, 0.f}), "La reducción del alcance elimina el campo de visión orientado");
      const auto grid = Visibility::BuildGrid(world, observer);
      Require(grid.visualRadii[0] > ranges[index] - 0.5f && grid.visualRadii[0] <= ranges[index], "La frontera continua dibuja un alcance distinto del autoritativo");
    }
    Require(ranges[0] > ranges[1] && ranges[1] > ranges[2] && ranges[2] > VisibilityConfig::CloseRange, "La reducción deja de distinguir posturas o elimina la exploración fuera del campo cercano");
  }
  void CoverPeekFollowsAimFromThePhysicalPosition() {
    for (const auto side : {PeekSide::Left, PeekSide::Right}) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(17);
      auto &world = const_cast<WorldState &>(simulation.GetState()); TestFixture::LegacyPlane(world);
      world.size = {600.f, 500.f}; world.objects.clear(); world.guards.clear(); world.securityEntries.clear(); world.obstacles.clear(); world.visibility = {};
      world.obstacles.push_back({{{200.f, 150.f}, {30.f, 200.f}}, CoverKind::High, true, "Pared de asomado"});
      auto &observer = world.agents.front(); observer.position = {175.f, side == PeekSide::Left ? 180.f : 320.f};
      const auto advance = [&](const ActionCommand &command, unsigned int frames = 1) {
        for (unsigned int frame = 0; frame < frames; ++frame) simulation.Step(.05f, {{observer.id, command}});
      };
      ActionCommand attach; attach.coverToggle = true; advance(attach);
      Require(observer.cover.active && observer.facing == sf::Vector2f{-1.f, 0.f}, "La regresión visual no establece una cobertura física de espalda a la pared");
      const auto anchor = observer.cover.anchor;
      sf::Vector2f exposed;
      Require(WorldGeometry::CoverPeekPosition(observer.cover, side, exposed), "El extremo físico no ofrece un destino de asomado");
      const auto target = exposed + sf::Vector2f{100.f, 0.f};
      ActionCommand peek; peek.peekLeft = side == PeekSide::Left; peek.peekRight = side == PeekSide::Right; peek.aimX = 1.f; peek.aiming = true;
      advance(peek);
      Require(observer.cover.peekSide == side && (observer.position - exposed).length() > SpatialConfig::CoverPositionTolerance,
        "El primer paso no conserva el desplazamiento gradual hacia el extremo");
      Require(!Visibility::IsGeometricallyVisible(world, observer, target), "Solicitar el asomado adelanta el ojo al destino y revela a través de la pared");
      advance(peek, 20);
      Require(observer.cover.peekSide == side && (observer.position - exposed).length() <= SpatialConfig::CoverPositionTolerance &&
        observer.facing == observer.cover.normal && WeaponFacing(observer) == sf::Vector2f{1.f, 0.f}, "El asomado no separa el apuntado del cuerpo apoyado en la pared");
      Require(Visibility::IsGeometricallyVisible(world, observer, target), "Apuntar desde el extremo expuesto conserva en sombra la dirección del arma");
      const auto rear = observer.position - sf::Vector2f{100.f, 0.f};
      const sf::Vector2f behindWall{target.x, 250.f};
      Require(!Visibility::IsGeometricallyVisible(world, observer, rear), "Asomarse concede visión simultánea a la espalda del arma");
      Require(!Visibility::IsGeometricallyVisible(world, observer, behindWall), "El apuntado asomado atraviesa el volumen real de la pared");
      Require(!Visibility::IsGeometricallyVisible(world, observer, observer.position + sf::Vector2f{VisibilityConfig::StandingRange + 1.f, 0.f}),
        "El asomado amplía el alcance visual de la postura");
      const WorldGeometry::PreparedOccluders geometry(world);
      Require(Visibility::IsGeometricallyVisible(world, observer, target, Stance::Standing, observer.elevation, &geometry) &&
        !Visibility::IsGeometricallyVisible(world, observer, behindWall, Stance::Standing, observer.elevation, &geometry),
        "La geometría preparada no conserva la visión y la oclusión del asomado");
      const auto grid = Visibility::BuildGrid(world, observer);
      Require(grid.visualOrigin == observer.position && grid.visualOrigin != anchor && grid.visualRadii[0] > 100.f &&
        grid.visualRadii[180] <= VisibilityConfig::CloseRange + .1f, "La niebla usa el ancla o la normal de cobertura en lugar del ojo y el apuntado reales");
      world.visibility = grid;
      Require(Visibility::IsVisible(world, observer, target) && !Visibility::IsVisible(world, observer, rear), "La cuadrícula de niebla no acompaña el cono del asomado");
      world.visibility = {};
      for (unsigned int index = 0; index < 3; ++index) {
        Guard guard; guard.id = 71 + index; guard.position = index == 0 ? target : index == 1 ? rear : behindWall;
        guard.facing = {1.f, 0.f}; guard.patrol = {guard.position}; world.guards.push_back(guard);
      }
      const auto filtered = simulation.SnapshotFor(observer.id, true);
      Require(filtered["guards"].size() == 1 && filtered["guards"][0]["id"] == 71,
        "El snapshot del asomado oculta al guardia apuntado o revela la espalda y el interior de la pared");
      world.guards.clear();
      auto invalidSide = observer;
      if (side == PeekSide::Left) invalidSide.cover.canPeekLeft = false; else invalidSide.cover.canPeekRight = false;
      Require(!Visibility::IsGeometricallyVisible(world, invalidSide, target) && Visibility::IsGeometricallyVisible(world, invalidSide, rear),
        "Un lado de asomado inválido conserva un cono orientado por el arma");
      invalidSide = observer; invalidSide.cover.active = false;
      Require(!Visibility::IsGeometricallyVisible(world, invalidSide, target), "Una cobertura inactiva conserva el cono especial del asomado");
      auto notAiming = observer; notAiming.aiming = false;
      Require(!Visibility::IsGeometricallyVisible(world, notAiming, target) && Visibility::IsGeometricallyVisible(world, notAiming, rear),
        "Dejar de apuntar conserva el cono del arma en vez de la orientación corporal de cobertura");
      const auto position = observer.position;
      auto turnAim = peek; turnAim.aimX = -1.f; advance(turnAim);
      Require(observer.position == position && observer.facing == observer.cover.normal && Visibility::IsGeometricallyVisible(world, observer, rear) &&
        !Visibility::IsGeometricallyVisible(world, observer, target), "Cambiar el apuntado no gira el cono del extremo o desplaza el cuerpo");
      advance(peek); advance({}, 20);
      Require(observer.cover.active && observer.cover.peekSide == PeekSide::None && (observer.position - anchor).length() <= SpatialConfig::CoverPositionTolerance,
        "Soltar el asomado no recupera la posición protegida");
      Require(!Visibility::IsGeometricallyVisible(world, observer, target) && Visibility::IsGeometricallyVisible(world, observer, observer.position - sf::Vector2f{100.f, 0.f}),
        "Volver al ancla conserva la visión del arma o pierde la orientación normal de cobertura");
      const auto returned = Visibility::BuildGrid(world, observer);
      Require(returned.visualOrigin == observer.position && returned.visualRadii[0] < 100.f && returned.visualRadii[180] > 100.f,
        "La frontera de niebla no vuelve al cono protegido al dejar el extremo");
    }
  }
  void PrivateSnapshots() {
    Simulation simulation;
    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.Deploy(17);
    auto snapshot = simulation.SnapshotFor(1); TestFixture::LegacyPlane(snapshot);
    const auto world = Scenario();
    snapshot["agents"][0]["position"] = {120.f, 180.f}; snapshot["agents"][0]["facing"] = {1.f, 0.f};
    snapshot["agents"][1]["position"] = {320.f, 180.f}; snapshot["agents"][1]["facing"] = {-1.f, 0.f};
    const auto guardTemplate = snapshot["guards"][0];
    snapshot["guards"] = nlohmann::json::array();
    for (const auto &guard : world.guards) {
      auto value = guardTemplate; value["id"] = guard.id; value["position"] = {guard.position.x, guard.position.y}; value["patrol"] = {{guard.position.x, guard.position.y}}; value["waypoint"] = 0;
      snapshot["guards"].push_back(value);
    }
    snapshot["objects"] = nlohmann::json::array();
    snapshot["message"] = "ORDEN PRIVADA DE OTRO AGENTE";
    snapshot["events"] = {{{"sequence", 1}, {"tick", 0}, {"actor", 1}, {"kind", static_cast<int>(EventKind::WeaponLethal)}, {"target", 0}}};
    snapshot["obstacles"] = {{{"position", {220.f, 80.f}}, {"size", {20.f, 220.f}}, {"cover", 2}, {"solid", true}, {"name", "Pared"}}};
    std::string error;
    Require(simulation.LoadSnapshot(snapshot, error), "No se pudo crear el escenario autoritativo de privacidad");
    const auto first = simulation.SnapshotFor(1, true), second = simulation.SnapshotFor(2, true);
    Require(first["guards"].size() == 1 && first["guards"][0]["id"] == 11, "El cliente uno recibio un guardia tras la pared");
    Require(second["guards"].size() == 1 && second["guards"][0]["id"] == 12, "La visibilidad no depende del participante");
    Require(first["guards"][0]["patrol"].empty(), "El snapshot visual revela la ruta futura del guardia");
    Require(first["agents"].size() == 1 && first["agents"][0]["id"] == 1, "Un agente oculto conserva informacion privilegiada en la red");
    Require(first["message"] != snapshot["message"], "Un mensaje global revela instrucciones privadas de otro participante");
    Simulation replica;
    Require(replica.LoadSnapshot(first, error), "El cliente rechazo una mascara valida");
    Require(replica.GetState().visibility.observer == 1 && replica.GetState().guards.size() == 1, "La mascara no se cargo con las entidades filtradas");
    for (int malformed = 0; malformed < 5; ++malformed) {
      auto invalid = first;
      if (malformed == 0) invalid["visibility"]["observer"] = 2;
      if (malformed == 1) invalid["visibility"]["cells"][0] = 2;
      if (malformed == 2) invalid["visibility"]["cells"].erase(invalid["visibility"]["cells"].begin());
      if (malformed == 3) invalid["visibility"]["columns"] = 90000;
      if (malformed == 4) invalid["visibility"]["tick"] = first["tick"].get<std::uint64_t>() + 1;
      Require(!replica.LoadSnapshot(invalid, error), "El cliente acepto una mascara malformada");
      Require(replica.GetState().visibility.observer == 1 && replica.GetState().guards.size() == 1, "Un rechazo parcial altero el ultimo estado valido");
    }
    Require(simulation.GetState().guards.size() == 2, "Filtrar el snapshot altero la simulacion del anfitrion");
  }
  void HideoutPrivacy() {
    Simulation simulation;
    simulation.ResetBase({{1, "Uno"}, {2, "Dos"}}); simulation.Deploy(17);
    auto snapshot = simulation.SnapshotFor(1); TestFixture::LegacyPlane(snapshot);
    auto closet = snapshot["objects"][0]; closet["id"] = 77; closet["kind"] = static_cast<int>(ObjectKind::Hideout);
    closet["hideoutKind"] = static_cast<int>(HideoutKind::Closet); closet["position"] = {210.f, 180.f}; closet["footprint"] = {0.f, 0.f};
    snapshot["objects"] = nlohmann::json::array({closet}); snapshot["guards"] = nlohmann::json::array(); snapshot["obstacles"] = nlohmann::json::array();
    snapshot["agents"][0]["position"] = {150.f, 180.f}; snapshot["agents"][0]["facing"] = {1.f, 0.f};
    snapshot["agents"][1]["position"] = {150.f, 220.f}; snapshot["agents"][1]["facing"] = {1.f, 0.f};
    std::string error;
    const bool loaded = simulation.LoadSnapshot(snapshot, error);
    Require(loaded, ("No se pudo crear el escenario de privacidad del escondite: " + error).c_str());
    ActionCommand use; use.interact = true; simulation.Step(0.1f, {{1, use}});
    const auto own = simulation.SnapshotFor(1, true), other = simulation.SnapshotFor(2, true);
    Require(own["objects"].size() == 1 && own["objects"][0]["id"] == 77 && own["objects"][0]["occupantAgent"] == 1,
      "El agente oculto no recibe su propio contenedor y su referencia de ocupación");
    Require(std::ranges::none_of(other["agents"], [](const auto &agent) { return agent["id"] == 1; }), "El contenedor revela al agente oculto a otro participante");
    Require(other["objects"].size() == 1 && other["objects"][0]["occupied"] == true && other["objects"][0]["occupantAgent"] == 0 && other["objects"][0]["occupantGuard"] == 0,
      "La ocupación pública desaparece o revela una identidad privada");
    Simulation ownerReplica, otherReplica;
    Require(ownerReplica.LoadSnapshot(own, error) && ownerReplica.GetState().agents[0].hideoutId == 77,
      "El cliente oculto rechaza su contenedor autoritativo");
    Require(otherReplica.LoadSnapshot(other, error) && WorldGeometry::HideoutOccupied(otherReplica.GetState().objects[0]),
      "El cliente ajeno pierde la ocupación sin identidad del escondite");
  }
  void PrivateFootstepFiltering() {
    Simulation simulation; simulation.ResetBase(); simulation.Deploy(17);
    auto snapshot = simulation.SnapshotFor(1); TestFixture::LegacyPlane(snapshot);
    const auto guardTemplate = snapshot["guards"][0];
    auto closet = snapshot["objects"][0]; closet["id"] = 90; closet["kind"] = static_cast<int>(ObjectKind::Hideout);
    closet["hideoutKind"] = static_cast<int>(HideoutKind::Closet); closet["position"] = {320.f, 220.f};
    closet["occupied"] = true; closet["occupantGuard"] = 13; closet["footprint"] = {0.f, 0.f}; snapshot["objects"] = nlohmann::json::array({closet});
    snapshot["agents"][0]["position"] = {150.f, 180.f}; snapshot["agents"][0]["facing"] = {1.f, 0.f};
    snapshot["obstacles"] = {{{"position", {240.f, 80.f}}, {"size", {20.f, 220.f}}, {"cover", 2}, {"solid", true}, {"name", "Pared"}}};
    snapshot["guards"] = nlohmann::json::array(); snapshot["footsteps"] = nlohmann::json::array();
    const std::array<sf::Vector2f, 4> positions{{{180.f, 180.f}, {320.f, 180.f}, {320.f, 220.f}, {450.f, 180.f}}};
    for (std::size_t index = 0; index < positions.size(); ++index) {
      const auto id = static_cast<std::uint32_t>(11 + index);
      const auto position = positions[index]; auto guard = guardTemplate; guard["id"] = id; guard["position"] = {position.x, position.y};
      guard["patrol"] = nlohmann::json::array(); guard["waypoint"] = 0;
      if (index == 2) { guard["bodyHidden"] = true; guard["storedIn"] = 90; guard["state"] = static_cast<int>(GuardState::Incapacitated); guard["stun"] = 100.f; guard["incapacityRemaining"] = GameplayConfig::GuardIncapacityDuration; }
      snapshot["guards"].push_back(guard);
      snapshot["footsteps"].push_back({{"position", {position.x, position.y}}, {"direction", index}, {"remaining", DistractionConfig::FootstepLifetime}, {"sourceGuard", id}, {"elevation", 0.f}});
    }
    std::string error;
    const bool loaded = simulation.LoadSnapshot(snapshot, error);
    Require(loaded, ("No se pudo crear el escenario de filtrado de pasos: " + error).c_str());
    const auto filtered = simulation.SnapshotFor(1, true);
    Require(filtered["footsteps"].size() == 1 && filtered["footsteps"][0]["direction"] == 1,
      "Se reciben pasos de una fuente visible, almacenada o fuera del alcance auditivo");
    Require(!filtered["footsteps"][0].contains("sourceGuard"), "La pista de pasos contiene la identidad privada de su fuente");
    Require(std::ranges::none_of(filtered["guards"], [](const auto &guard) { return guard["id"] == 13; }), "El filtrado de pasos revela el cuerpo almacenado");
    Simulation replica;
    Require(replica.LoadSnapshot(filtered, error) && replica.GetState().footsteps[0].sourceGuard == 0,
      "El cliente no carga la pista aproximada sin identidad privada");
  }
  void HiddenNeutralizationFeedback() {
    for (const bool lethal : {true, false}) {
      Simulation simulation; simulation.ResetBase(); simulation.Deploy(17);
      auto snapshot = simulation.SnapshotFor(1); TestFixture::LegacyPlane(snapshot); auto guard = snapshot["guards"][0];
      guard["id"] = 77; guard["position"] = {450.f, 180.f}; guard["facing"] = {1.f, 0.f};
      guard["patrol"] = nlohmann::json::array(); guard["waypoint"] = 0;
      snapshot["guards"] = nlohmann::json::array({guard}); snapshot["objects"] = nlohmann::json::array(); snapshot["obstacles"] = nlohmann::json::array();
      snapshot["agents"][0]["position"] = {150.f, 180.f}; snapshot["agents"][0]["facing"] = {1.f, 0.f}; snapshot["agents"][0]["stance"] = static_cast<int>(Stance::Prone);
      std::string error;
      const bool loaded = simulation.LoadSnapshot(snapshot, error);
      Require(loaded, ("No se pudo crear el escenario de disparo fuera de visión: " + error).c_str());
      ActionCommand fire; fire.aimX = 1.f;
      fire.aimSlope = (CombatGeometry::HeadCenterHeight(Stance::Standing) - CombatGeometry::WeaponHeight(Stance::Prone)) / 300.f;
      fire.fireLethal = lethal; fire.fireNonLethal = !lethal; simulation.Step(0.1f, {{1, fire}});
      const auto kind = lethal ? EventKind::GuardKilled : EventKind::GuardIncapacitated;
      const auto weapon = lethal ? EventKind::WeaponLethal : EventKind::WeaponNonLethal;
      Require(simulation.GetState().guards[0].state == (lethal ? GuardState::Dead : GuardState::Incapacitated), "El disparo de privacidad no neutraliza al guardia oculto");
      const auto filtered = simulation.SnapshotFor(1, true);
      Require(filtered["guards"].empty(), "El disparo revela la entidad del guardia fuera de visión");
      Require(filtered["shots"].size() == 1 && filtered["shots"][0]["targetId"] == 0 && filtered["shots"][0]["targetKind"] == static_cast<int>(ShotTarget::None),
        "El trazo revela la identidad del guardia oculto neutralizado");
      Require(std::ranges::any_of(filtered["events"], [kind](const auto &event) { return event["kind"] == static_cast<int>(kind) && event["actor"] == 1 && event["target"] == 0; }),
        "El resultado propio desaparece o conserva la identidad privada del guardia");
      Require(std::ranges::any_of(filtered["events"], [weapon](const auto &event) { return event["kind"] == static_cast<int>(weapon) && event["actor"] == 1; }),
        "Filtrar la identidad elimina el evento válido del disparo propio");
      Require(std::ranges::none_of(filtered["events"], [](const auto &event) { return event["target"] == 77; }), "Un evento propio revela el identificador del enemigo oculto");
      Require(std::ranges::any_of(simulation.GetState().events, [kind](const MissionEvent &event) { return event.kind == kind && event.target == 77; }),
        "Anonimizar el cliente altera el registro autoritativo de la neutralización");
      Simulation replica;
      Require(replica.LoadSnapshot(filtered, error), "El cliente rechaza el resultado propio anonimizado");
    }
  }
  void BodyExposureAndRescueDirection() {
    WorldState world; world.phase = GamePhase::Mission; world.size = {1000.f,760.f};
    Agent target; target.id=2; target.position={250.f,200.f}; target.facing={-1.f,0.f};
    world.obstacles.push_back({{{200.f,150.f},{25.f,100.f}},CoverKind::High,true,"Pared"});
    Require(!Visibility::AgentExposed(world,{150.f,200.f},target), "El torso o cabeza atraviesa una pared completa");
    target.position.y=145.f;
    Require(Visibility::AgentExposed(world,{150.f,145.f},target), "Una cabeza expuesta junto al borde permanece invisible");
    target.cover.active=true;
    Require(Visibility::AgentExposed(world,{150.f,145.f},target), "La bandera cobertura concede invisibilidad fuera de geometría");
    target.position={250.f,200.f}; target.stance=Stance::Prone;
    Require(!Visibility::AgentExposed(world,{150.f,200.f},target), "Arrastrarse atraviesa la pared en percepción");
    Simulation simulation; simulation.AddPlayer(1,"Uno"); simulation.AddPlayer(2,"Dos"); simulation.SetContract(ContractId::GhostArchive); simulation.Deploy(812);
    auto &state=const_cast<WorldState&>(simulation.GetState()); TestFixture::LegacyPlane(state); state.guards.clear(); state.objects.clear(); state.obstacles=world.obstacles;
    state.agents[0].position={150.f,200.f}; state.agents[1].position={250.f,200.f}; state.agents[1].lifeState=LifeState::Incapacitated;
    state.agents[1].incapacitated=true; state.agents[1].health=0.f; state.agents[1].incapacityRemaining=42.f;
    const auto snapshot=simulation.SnapshotFor(1,true);
    Require(snapshot["agents"].size()==1 && snapshot["rescueSignals"].size()==1,"El rescate revela cuerpo oculto o no avisa al compañero");
    const auto signal=snapshot["rescueSignals"][0];
    Require(signal.size()==3 && signal["direction"]==nlohmann::json::array({1.f,0.f}) && signal["remaining"]==42.f, "La señal revela posición exacta o un plazo local");
    Simulation client; std::string error; Require(client.LoadSnapshot(snapshot,error),"El cliente rechaza el rescate fuera de visión");
    state.agents[1].position=state.agents[0].position;
    Require(client.LoadSnapshot(simulation.SnapshotFor(1,true),error),"Coincidir con el compañero incapacitado genera una dirección inválida");
    auto malformed=snapshot; malformed["rescueSignals"][0]["direction"]={7.f,0.f};
    Require(!client.LoadSnapshot(malformed,error),"Se acepta una dirección de rescate no unitaria");
  }

  void HeightAndTableCursorGeometry() {
    WorldState world; world.phase = GamePhase::Mission; world.size = {1000.f, 760.f};
    Agent observer; observer.id = 1; observer.position = {300.f, 300.f}; observer.stance = Stance::Prone;
    observer.hidden = true; observer.hideoutId = 77; observer.weaponAim = {1.f, 0.f}; observer.facing = {0.f, 1.f};
    Interactable table{77, ObjectKind::Hideout, observer.position, "Mesa"}; table.hideoutKind = HideoutKind::UnderTable; table.occupantAgent = 1; table.occupied = true;
    world.objects.push_back(table);
    Require(Visibility::IsGeometricallyVisible(world, observer, {400.f, 300.f}, Stance::Prone), "El cursor no permite observar delante bajo la mesa");
    Require(!Visibility::IsGeometricallyVisible(world, observer, {200.f, 300.f}, Stance::Prone), "La mesa conserva visión de 360 grados");
    Require(!Visibility::IsGeometricallyVisible(world, observer, {290.f, 300.f}, Stance::Prone) &&
      !Visibility::IsGeometricallyVisible(world, observer, {300.f, 310.f}, Stance::Prone) &&
      !Visibility::IsGeometricallyVisible(world, observer, {330.f, 340.f}, Stance::Prone),
      "La mesa revela un halo cercano o un lado fuera del cono frontal del ratón");
    const auto tableView = Visibility::BuildGrid(world, observer);
    Require(tableView.visualRadii[90] <= VisibilityConfig::SurfaceOffset && tableView.visualRadii[180] <= VisibilityConfig::SurfaceOffset,
      "La frontera de niebla conserva visión lateral o trasera bajo mesa");
    world.visibility = tableView;
    Require(!Visibility::IsVisible(world, observer, {290.f, 300.f}, Stance::Prone), "La celda del propio agente revela la espalda bajo mesa");
    world.visibility = {};
    Require(!Visibility::IsGeometricallyVisible(world, observer, {300.f + VisibilityConfig::ProneRange + 1.f, 300.f}, Stance::Prone), "La mesa amplía el alcance tumbado");
    observer.weaponAim = {-1.f, 0.f};
    Require(Visibility::IsGeometricallyVisible(world, observer, {200.f, 300.f}, Stance::Prone) && observer.facing == sf::Vector2f{0.f, 1.f}, "El cursor no cambia el cono conservando el cuerpo fijo");
    observer.weaponAim = {1.f, 0.f};
    world.obstacles.push_back({{{350.f, 270.f}, {10.f, 60.f}}, CoverKind::High, true, "Pared"});
    Require(!Visibility::IsGeometricallyVisible(world, observer, {400.f, 300.f}, Stance::Prone), "El cursor atraviesa una pared desde debajo de la mesa");
    world.obstacles.clear();
    const auto corner = WorldGeometry::ObjectFootprint(table).size * 0.5f;
    Require(!Visibility::IsGeometricallyVisible(world, observer, observer.position + corner.normalized() * 100.f, Stance::Prone), "Las patas de la mesa carecen de oclusión física");
    world.phase = GamePhase::Base; world.mode = SimulationMode::Training;
    Require(Visibility::UsesFieldOfView(world, observer) && !Visibility::IsVisible(world, observer, {290.f, 300.f}, Stance::Prone),
      "La mesa de entrenamiento revela simultáneamente las direcciones ajenas al cursor");
    const auto trainingView = Visibility::BuildGrid(world, observer);
    Require(trainingView.visualRadii[180] <= VisibilityConfig::SurfaceOffset, "Entrenamiento no conserva el mismo cono frontal de mesa");
    world.phase = GamePhase::Mission; world.mode = SimulationMode::Operation;
    observer.hidden = false; observer.hideoutId = 0; observer.stance = Stance::Standing; observer.facing = {1.f, 0.f}; world.objects.clear();
    Agent target; target.position = observer.position; target.elevation = 96.f;
    world.walkSurfaces.push_back({1, 1, WalkSurfaceKind::Floor, {{200.f, 200.f}, {200.f, 200.f}}, 96.f});
    Require(!Visibility::AgentExposed(world, observer.position, target), "Un guardia detecta a través del suelo superior en el mismo XY");
    Require(!Visibility::IsGeometricallyVisible(world, observer, target.position, Stance::Standing, target.elevation), "El filtro revela actores superpuestos a través de otra planta");
    world.walkSurfaces.clear();
    Require(Visibility::AgentExposed(world, observer.position, target), "La altura por sí sola concede invisibilidad sin una oclusión real");
    Obstacle upper{{{350.f, 250.f}, {12.f, 100.f}}, CoverKind::High, true, "Muro superior"}; upper.elevation = 96.f; world.obstacles.push_back(upper);
    Require(Visibility::IsGeometricallyVisible(world, observer, {400.f, 300.f}), "Un muro de otra planta bloquea el espacio libre inferior");
    observer.elevation = 96.f;
    Require(!Visibility::IsGeometricallyVisible(world, observer, {400.f, 300.f}), "Un muro elevado no bloquea a los actores de su planta");
  }

  VisibilityGrid ReferenceGrid(const WorldState &world, const Agent &observer) {
    VisibilityGrid grid;
    grid.observer = observer.id; grid.tick = world.tick; grid.cellSize = VisibilityConfig::CellSize;
    grid.columns = static_cast<unsigned int>(std::ceil(world.size.x / grid.cellSize));
    grid.rows = static_cast<unsigned int>(std::ceil(world.size.y / grid.cellSize));
    grid.cells.resize(static_cast<std::size_t>(grid.columns) * grid.rows);
    for (unsigned int row = 0; row < grid.rows; ++row) for (unsigned int column = 0; column < grid.columns; ++column) {
      const sf::Vector2f point{std::min(world.size.x - .01f, (column + .5f) * grid.cellSize), std::min(world.size.y - .01f, (row + .5f) * grid.cellSize)};
      grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = !Visibility::UsesFieldOfView(world, observer) || Visibility::IsGeometricallyVisible(world, observer, point, Stance::Prone);
    }
    const auto column = static_cast<unsigned int>(std::max(0.f, observer.position.x) / grid.cellSize);
    const auto row = static_cast<unsigned int>(std::max(0.f, observer.position.y) / grid.cellSize);
    if (column < grid.columns && row < grid.rows) grid.cells[static_cast<std::size_t>(row) * grid.columns + column] = 1;
    grid.visualOrigin = observer.position; grid.elevation = observer.elevation;
    if (!IsTactical(world)) return grid;
    grid.visualRadii.resize(VisibilityConfig::VisualRayCount);
    for (std::size_t ray = 0; ray < grid.visualRadii.size(); ++ray) {
      const float angle = 2.f * std::numbers::pi_v<float> * static_cast<float>(ray) / grid.visualRadii.size();
      const sf::Vector2f direction{std::cos(angle), std::sin(angle)};
      float visible = 0.f, hidden = VisibilityConfig::StandingRange;
      for (unsigned int iteration = 0; iteration < VisibilityConfig::VisualRayIterations; ++iteration) {
        const float distance = (visible + hidden) * .5f;
        if (Visibility::IsGeometricallyVisible(world, observer, observer.position + direction * distance, Stance::Prone)) visible = distance;
        else hidden = distance;
      }
      grid.visualRadii[ray] = visible;
    }
    return grid;
  }

  void PreparedOcclusionMatchesReference() {
    WorldState world; world.phase = GamePhase::Mission; world.size = {440.f, 360.f};
    world.obstacles.push_back({{{215.f, 70.f}, {14.f, 160.f}}, CoverKind::High, true, "Muro"});
    world.obstacles.push_back({{{100.f, 245.f}, {170.f, 12.f}}, CoverKind::Low, true, "Cobertura"});
    Obstacle glass{{{280.f, 40.f}, {10.f, 120.f}}, CoverKind::High, true, "Cristal"}; glass.blocksVision = false; world.obstacles.push_back(glass);
    Interactable door{41, ObjectKind::Door, {205.f, 270.f}, "Puerta"}; door.footprint = {14.f, 100.f}; world.objects.push_back(door);
    Interactable table{42, ObjectKind::Hideout, {140.f, 180.f}, "Mesa"}; table.hideoutKind = HideoutKind::UnderTable; table.occupantAgent = 1; world.objects.push_back(table);
    Interactable closet{43, ObjectKind::Hideout, {330.f, 180.f}, "Armario"}; closet.hideoutKind = HideoutKind::Closet; closet.occupantAgent = 1; closet.open = true; world.objects.push_back(closet);
    Interactable zero{0, ObjectKind::Armory, {110.f, 130.f}, "Identificador cero"}; world.objects.push_back(zero);
    world.walkSurfaces = {{1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f},
      {2, 1, WalkSurfaceKind::Floor, {{230.f, 40.f}, {170.f, 200.f}}, 96.f},
      {3, 0, WalkSurfaceKind::Stair, {{40.f, 40.f}, {100.f, 100.f}}, 20.f, {.5f, -.125f}},
      {90, 1, WalkSurfaceKind::ElevatorCabin, {{300.f, 260.f}, {80.f, 80.f}}, 48.f}};
    Agent observer; observer.id = 1; observer.position = {140.f, 180.f};
    for (unsigned int state = 0; state < 8; ++state) {
      world.objects[0].open = state % 2 != 0;
      world.objects[1].overturned = state == 2;
      world.objects[1].lifted = state == 3;
      world.objects[2].hideoutAperture = state == 6 ? .2f : .8f;
      world.objects[2].open = state != 7;
      world.walkSurfaces[3].elevation = state % 2 == 0 ? 48.f : 96.f;
      world.walkSurfaces[1].enabled = state != 3;
      const WorldGeometry::PreparedOccluders geometry(world);
      WorldGeometry::PreparedOccluders::Query query(geometry);
      for (unsigned int direction = 0; direction < 8; ++direction) for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
        const float angle = direction * std::numbers::pi_v<float> * .25f;
        observer.facing = {std::cos(angle), std::sin(angle)}; observer.weaponAim = observer.facing; observer.stance = stance;
        observer.elevation = state == 4 ? 96.f : 0.f;
        observer.hideoutId = state == 5 ? table.id : state >= 6 ? closet.id : 0;
        observer.hidden = observer.hideoutId != 0;
        observer.position = state >= 6 ? closet.position : table.position;
        const auto expected = ReferenceGrid(world, observer), actual = Visibility::BuildGrid(world, observer);
        Require(actual.cells == expected.cells && actual.visualRadii == expected.visualRadii && actual.visualOrigin == expected.visualOrigin && actual.elevation == expected.elevation,
          "La geometría preparada cambia un bit de la cuadrícula o un radio visual respecto a las consultas originales");
        const sf::Vector3f from{observer.position.x, observer.position.y, observer.elevation + CombatGeometry::HeadCenterHeight(stance)};
        const auto full = observer.position + observer.facing * 300.f;
        query.Select(observer.position, full);
        for (const float fraction : {.001f, .125f, .5f, 1.f}) for (const float targetZ : {0.f, 8.5f, 40.f, 96.f, 192.f}) for (const auto ignored : {0u, table.id, closet.id}) {
          const auto position = observer.position + observer.facing * (300.f * fraction);
          const sf::Vector3f to{position.x, position.y, targetZ};
          const bool reference = WorldGeometry::RayOccluded(world, from, to, ignored);
          Require(geometry.Occluded(from, to, ignored) == reference && query.Occluded(from, to, ignored) == reference,
            "El filtro preparado cambia la oclusión de muros, puerta, mesa, armario, pendiente o cabina móvil");
        }
        for (const auto &object : world.objects) {
          Require(Visibility::ObjectVisible(world, observer, object, &geometry) == Visibility::ObjectVisible(world, observer, object),
            "La consulta preparada cambia la privacidad o los bordes visibles de un objeto");
          const auto footprint = WorldGeometry::ObjectFootprint(object);
          Require(WorldRenderer::FootprintVisible(world, observer, footprint, &geometry) == WorldRenderer::FootprintVisible(world, observer, footprint),
            "El minimapa preparado cambia la visibilidad exacta de una huella");
        }
        for (const auto &obstacle : world.obstacles) Require(WorldRenderer::FootprintVisible(world, observer, obstacle.bounds, &geometry) == WorldRenderer::FootprintVisible(world, observer, obstacle.bounds),
          "El minimapa preparado revela u oculta el borde de un muro");
        for (const float distance : {16.f, 100.f, 200.f, 300.f}) for (const float elevation : {0.f, 96.f, 192.f}) {
          const auto point = observer.position + observer.facing * distance;
          for (const auto targetStance : {Stance::Standing, Stance::Crouched, Stance::Prone}) Require(
            Visibility::IsGeometricallyVisible(world, observer, point, targetStance, elevation, &geometry) == Visibility::IsGeometricallyVisible(world, observer, point, targetStance, elevation),
            "La consulta geométrica preparada cambia alcance, postura, orientación o altura");
          Guard guard; guard.id = 71; guard.position = point; guard.elevation = elevation;
          for (const auto guardState : {GuardState::Patrol, GuardState::Incapacitated, GuardState::Dead}) {
            guard.state = guardState;
            Require(Visibility::GuardVisible(world, observer, guard, &geometry) == Visibility::GuardVisible(world, observer, guard),
              "La consulta preparada cambia la visibilidad de un guardia vivo, inconsciente o muerto");
          }
          guard.state = GuardState::Patrol; guard.cqcState = CqcState::KnockedDown;
          Require(Visibility::GuardVisible(world, observer, guard, &geometry) == Visibility::GuardVisible(world, observer, guard),
            "La consulta preparada pierde la altura del derribo CQC");
        }
      }
    }
    const WorldGeometry::PreparedOccluders geometry(world);
    world.visibility.authoritative = true; world.visibility.observer = observer.id;
    Guard authorized; authorized.id = 72; authorized.position = {1000.f, 1000.f};
    Require(Visibility::GuardVisible(world, observer, authorized, &geometry) == Visibility::GuardVisible(world, observer, authorized),
      "La consulta preparada vuelve a filtrar entidades ya autorizadas por el anfitrión");
    Interactable armed{73, ObjectKind::Explosive, observer.position, "Trampa oculta"}; armed.trap.state = TrapState::Armed;
    Require(!Visibility::ObjectVisible(world, observer, armed, &geometry), "La consulta preparada revela una trampa armada del estado autorizado");
  }

}
int main() {
  try {
    PreparedOcclusionMatchesReference(); HeightAndTableCursorGeometry(); BodyExposureAndRescueDirection(); GeometryAndGrid(); ReducedPostureRanges(); CoverPeekFollowsAimFromThePhysicalPosition(); PrivateSnapshots(); HideoutPrivacy(); PrivateFootstepFiltering(); HiddenNeutralizationFeedback();
    std::cout << "PASS visibilidad: " << checks << " comprobaciones de campo, paredes, puertas, posturas, privacidad y mascaras\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL visibilidad tras " << checks << ": " << error.what() << '\n'; return 1;
  }
}
