#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceVisibility.h"
#include "NoTraceInteraction.h"
#include "Config/NoTraceCqcConfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); }
  WorldState &Fixture(Simulation &simulation, bool partner = false) {
    simulation.ResetBase(partner ? std::vector<PlayerProfile>{{1,"Uno"},{2,"Dos"}} : std::vector<PlayerProfile>{{1,"Uno"}});
    simulation.Deploy(31);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    TestFixture::LegacyPlane(world);
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.noises.clear(); world.shots.clear(); world.events.clear(); world.securityEntries.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[0].facing = {1.f, 0.f};
    for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::None;
    if (partner) { world.agents[1].position = {228.f, 300.f}; world.agents[1].facing = {1.f, 0.f}; }
    else { Guard guard; guard.id = 9; guard.position = {228.f, 300.f}; guard.facing = {1.f, 0.f}; guard.patrol = {guard.position}; guard.attackCooldown = 60.f; world.guards.push_back(guard); }
    return world;
  }
  void Advance(Simulation &simulation, ActionCommand command, int count) { for (int frame = 0; frame < count; ++frame) simulation.Step(0.1f, {{1, command}}); }
  ActionCommand Held() { ActionCommand command; command.cqcHeld = true; command.aimX = 1.f; return command; }
  int CqcEvents(const WorldState &world) { return static_cast<int>(std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::Cqc; })); }
  void ShortPress(Simulation &simulation) {
    auto command = Held(); command.cqc = true;
    simulation.Step(0.1f, {{1, command}});
    simulation.Step(0.1f, {});
  }
  void PunchKickAndRecovery() {
    Simulation simulation, replica; auto &world = Fixture(simulation);
    world.guards[0].facing = {-1.f, 0.f};
    auto press = Held(); press.cqc = true;
    simulation.Step(0.1f, {{1, press}});
    Require(world.agents[0].cqcPressPending && world.agents[0].actionPose != ActionPose::Punch && CqcEvents(world) == 0, "La preparación de un agarre ya golpea antes de conocer la duración de F");
    simulation.Step(0.1f, {});
    Require(world.agents[0].actionPose == ActionPose::Punch && world.agents[0].cqcStrikePending && world.guards[0].stun == 0.f, "El F corto no inicia un puñetazo con preparación real");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error) && replica.GetState().agents[0].cqcStrikePending, "La fase anterior al impacto no se sincroniza");
    simulation.Step(0.1f, {});
    Require(CqcEvents(world) == 0, "El puñetazo aplica el impacto antes de su fase configurada");
    simulation.Step(0.1f, {});
    Require(CqcEvents(world) == 1 && world.guards[0].stun > 0.f && world.guards[0].staggerRemaining > 0.f, "El impacto del puñetazo no aplica una única reacción no letal");
    Advance(simulation, press, 5);
    Require(CqcEvents(world) == 1 && world.agents[0].cqcHoldingGuard == 0, "La repetición automática durante la recuperación encadena golpes o inicia agarre frontal");
    simulation.Step(0.1f, {});
    ShortPress(simulation);
    Require(world.agents[0].actionPose == ActionPose::Kick && world.agents[0].cqcStrikePending, "La segunda pulsación breve no alterna a patada");
    Advance(simulation, {}, 3);
    Require(CqcEvents(world) == 2 && world.guards[0].health == CombatConfig::MaxGuardHealth, "La patada repite el impacto o provoca daño letal");
    simulation.Step(0.1f, {});
    Require(world.agents[0].actionPose == ActionPose::Recover && world.agents[0].cqcAttackTime > 0.f, "La patada no muestra una fase de recuperación diferenciada");
    const auto previous = world.agents[0].position;
    ActionCommand move; move.moveX = -1.f; move.fireLethal = false;
    simulation.Step(0.1f, {{1, move}});
    Require(world.agents[0].position == previous, "Moverse cancela implícitamente la recuperación del golpe");
    Advance(simulation, {}, 5);
    ShortPress(simulation); Advance(simulation, {}, 3);
    Require(CqcEvents(world) == 3 && world.guards[0].state == GuardState::Incapacitated && world.guards[0].health == CombatConfig::MaxGuardHealth, "Los golpes consecutivos no acumulan inconsciencia sin matar");
  }
  void ImpactValidationAndRadialApproach() {
    Simulation simulation; auto &world = Fixture(simulation);
    world.guards[0].facing = {-1.f, 0.f};
    Advance(simulation, Held(), 5);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1 && CqcEvents(world) == 1 && world.guards[0].facing == sf::Vector2f{1.f, 0.f}, "Mantener F frente al objetivo no confirma el agarre girándolo de espaldas al jugador");
    simulation.Step(0.1f, {}); Advance(simulation, {}, 3);
    Require(world.agents[0].cqcAttackTime == 0.f && world.guards[0].heldBy == 1 && CqcEvents(world) == 1, "Soltar F después de adquirir el agarre ejecuta un golpe accidental");
    Fixture(simulation); ShortPress(simulation);
    world.obstacles.push_back({{{212.f, 260.f}, {4.f, 80.f}}, CoverKind::High, true, "Pared aparecida antes del impacto"});
    Advance(simulation, {}, 3);
    Require(CqcEvents(world) == 0 && world.guards[0].stun == 0.f, "El golpe ignora los obstáculos existentes durante el impacto");
    Fixture(simulation); ShortPress(simulation);
    world.guards[0].position = {280.f, 300.f}; world.guards[0].patrol = {world.guards[0].position};
    Advance(simulation, {}, 3);
    Require(CqcEvents(world) == 0 && world.guards[0].stun == 0.f, "El golpe alcanza un objetivo que salió de su distancia real");
    Fixture(simulation); auto command = Held(); Advance(simulation, command, 3);
    const auto playerPosition = world.agents[0].position, guardPosition = world.guards[0].position;
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1 && world.guards[0].position == sf::Vector2f{228.f, 300.f}, "La aproximación válida no conserva el lugar del enemigo");
    Require(world.agents[0].position == sf::Vector2f{200.f, 300.f} && (guardPosition - playerPosition).dot(world.agents[0].facing) > 0.f && world.guards[0].facing == world.agents[0].facing, "El agarre teletransporta al dueño o coloca al enemigo detrás del agente");
    command.push = true; simulation.Step(0.1f, {{1, command}});
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1 && world.agents[0].position == playerPosition && world.guards[0].position == guardPosition,
      "Pulsar V durante un agarre lo libera o empuja al objetivo sujeto");
  }
  void FriendlyGripAndCleanup() {
    Simulation simulation, replica; auto &world = Fixture(simulation, true);
    world.agents[1].position = {237.f, 300.f};
    Advance(simulation, Held(), 3);
    Require(world.agents[0].cqcHoldingAgent == 2 && world.agents[1].cqcHeldBy == 1 && world.agents[0].cqcHoldingGuard == 0, "El anfitrión no crea una sujeción amistosa con referencias recíprocas");
    Require(CqcEvents(world) == 0 && world.agents[1].health == GameplayConfig::MaxHealth, "Sujetarse entre aliados altera la puntuación enemiga o aplica daño accidental");
    const auto original = world.agents[1].position;
    ActionCommand move; move.moveX = 1.f; move.fireLethal = true; move.aimX = -1.f;
    simulation.Step(0.1f, {{1, Held()}, {2, move}});
    Require(world.agents[1].position == original && world.shots.empty() && world.agents[1].facing == world.agents[0].facing, "El compañero inmovilizado puede moverse, girar o disparar");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error) && replica.GetState().agents[1].cqcHeldBy == 1, "La sujeción amistosa no llega íntegra al cliente");
    Obstacle curtain{{{(world.agents[0].position.x + world.agents[1].position.x) * 0.5f, 280.f}, {1.f, 40.f}}, CoverKind::High, true, "Pantalla opaca sin colisión"};
    curtain.solid = false; world.obstacles.push_back(curtain);
    const auto victimSnapshot = simulation.SnapshotFor(2, true);
    Require(replica.LoadSnapshot(victimSnapshot, error), "La sujeción amistosa rechaza al receptor cuando el dueño queda detrás de su campo visual");
    Require(!Visibility::IsVisible(world, world.agents[1], world.agents[0].position), "La regresión necesita un captor fuera del campo visual ordinario del aliado sujetado");
    const auto captor = std::ranges::find_if(replica.GetState().agents, [](const Agent &entry) { return entry.id == 1; });
    const auto victim = std::ranges::find_if(replica.GetState().agents, [](const Agent &entry) { return entry.id == 2; });
    Require(captor != replica.GetState().agents.end() && victim != replica.GetState().agents.end() && captor->cqcHoldingAgent == 2 && victim->cqcHeldBy == 1, "El aliado sujetado tras una pantalla visual no recibe al captor real con las referencias recíprocas del agarre");
    auto invalid = simulation.SnapshotFor(1, true); invalid["agents"][1]["cqcHeldBy"] = 9999;
    Require(!replica.LoadSnapshot(invalid, error), "Se acepta un compañero retenido por un dueño inexistente");
    world.obstacles.clear();
    Agent observer; observer.id = 3; observer.name = "Observador"; observer.position = {200.f, 360.f}; observer.facing = {0.f, -1.f};
    world.agents.push_back(observer);
    world.obstacles.push_back({{{world.agents[0].position.x - 5.f, 320.f}, {7.f, 10.f}}, CoverKind::High, true, "Muro que oculta únicamente al dueño"});
    const auto observerSnapshot = simulation.SnapshotFor(3, true);
    Require(replica.LoadSnapshot(observerSnapshot, error), "Un observador recibe referencias CQC huérfanas al ver solo uno de los participantes");
    Require(std::ranges::none_of(replica.GetState().agents, [](const Agent &entry) { return entry.id == 1; }), "La sujeción amistosa revela al dueño detrás de una pared");
    Require(replica.LoadSnapshot(simulation.SnapshotFor(2, true), error) && std::ranges::none_of(replica.GetState().agents, [](const Agent &entry) { return entry.id == 3; }), "Compartir la pareja CQC revela a un tercero oculto ajeno al agarre");
    world.agents.pop_back(); world.obstacles.clear();
    for (unsigned int pulse = 0; pulse < CqcConfig::RequiredPulses; ++pulse) {
      Advance(simulation, {}, 3); auto command = Held(); command.cqc = true;
      simulation.Step(0.1f, {{1, command}});
    }
    Require(world.agents[1].incapacitated && world.agents[1].friendlyIncapacitated && world.agents[1].alive && world.agents[1].health == GameplayConfig::MaxHealth, "La asfixia amistosa no conserva un estado de incapacitación sin daño letal");
    Require(world.agents[1].cqcHeldBy == 0 && world.agents[0].cqcHoldingAgent == 0 && CqcEvents(world) == 0 && world.phase == GamePhase::Mission, "El sometimiento amistoso bloquea la pareja, termina la partida o puntúa como enemigo");
    ActionCommand revive; revive.interact = true; simulation.Step(0.1f, {{1, revive}});
    Require(!world.agents[1].incapacitated && !world.agents[1].friendlyIncapacitated, "La reanimación no limpia la incapacitación amistosa");
    Fixture(simulation, true); Advance(simulation, Held(), 3); simulation.DisconnectPlayer(2);
    Require(world.agents[0].cqcHoldingAgent == 0 && world.agents[1].cqcHeldBy == 0, "La desconexión del receptor deja bloqueado al dueño del agarre");
    Fixture(simulation, true); Advance(simulation, Held(), 3); simulation.DisconnectPlayer(1);
    Require(world.agents[0].cqcHoldingAgent == 0 && world.agents[1].cqcHeldBy == 0, "La desconexión del dueño deja bloqueado al compañero");
    Fixture(simulation, true); Advance(simulation, Held(), 3); Advance(simulation, {}, 11);
    Require(world.agents[0].cqcHoldingAgent == 2 && world.agents[1].cqcHeldBy == 1 && !world.agents[1].incapacitated, "Soltar F libera automáticamente al compañero sin una acción explícita");
    Advance(simulation, Held(), 24);
    Require(world.agents[0].cqcHoldingAgent == 0 && world.agents[1].cqcHeldBy == 0, "La presión larga explícita no permite terminar la sujeción amistosa");
    Fixture(simulation, true); ShortPress(simulation); Advance(simulation, {}, 7);
    Require(world.agents[1].health == GameplayConfig::MaxHealth && world.agents[1].stamina == GameplayConfig::MaxStamina && !world.agents[1].incapacitated, "Un golpe breve dirigido junto a un aliado le aplica daño accidental");
  }
  void AcquisitionAndSubdue() {
    Simulation simulation, replica; auto &world = Fixture(simulation);
    auto held = Held(); held.cqc = true;
    simulation.Step(0.1f, {{1, held}});
    Require(world.agents[0].cqcHoldingGuard == 0, "Una pulsación instantánea completa el agarre");
    held.cqc = false; Advance(simulation, held, 3);
    Require(world.guards[0].heldBy == 1 && world.guards[0].cqcState == CqcState::Grabbed, "Mantener F no inicia la sujeción autoritativa");
    const auto agentPosition = world.agents[0].position, guardPosition = world.guards[0].position;
    held.moveX = 1.f; held.fireLethal = true; Advance(simulation, held, 15);
    Require(world.agents[0].position == agentPosition && world.guards[0].position == guardPosition && world.shots.empty(), "Movimiento o disparo altera accidentalmente el agarre");
    Require(world.agents[0].cqcProgress == 0, "Mantener F cuenta como pulsaciones repetidas");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "El cliente rechaza la sujeción sincronizada");
    Require(replica.GetState().agents[0].cqcHoldingGuard == 9 && replica.GetState().guards[0].heldBy == 1, "La sujeción se pierde en LAN");
    auto invalid = simulation.SnapshotFor(1, true);
    invalid["visibility"]["visualRadii"][0] = -1.f;
    Require(!replica.LoadSnapshot(invalid, error) && replica.GetState().agents[0].cqcHoldingGuard == 9, "Una frontera inválida altera el estado confirmado del cliente");
    invalid = simulation.SnapshotFor(1, true); invalid["agents"][0]["cqcHoldingGuard"] = 7777;
    Require(!replica.LoadSnapshot(invalid, error), "El cliente acepta un agarre con objetivo inexistente");
    for (unsigned int progress = 1; progress <= CqcConfig::RequiredPulses; ++progress) {
      Advance(simulation, {}, 2);
      auto pulse = Held(); pulse.cqc = true; simulation.Step(0.1f, {{1, pulse}});
      if (progress < CqcConfig::RequiredPulses) {
        Require(world.agents[0].cqcProgress == progress, "Una pulsación deliberada no aumenta el sometimiento");
        Advance(simulation, pulse, 2);
        Require(world.agents[0].cqcProgress == progress, "La repetición de un único evento completa más fases");
      }
    }
    Require(world.guards[0].state == GuardState::Incapacitated && world.guards[0].health == CombatConfig::MaxGuardHealth && world.guards[0].heldBy == 0, "El sometimiento no libera a un guardia inconsciente y vivo");
    Require(world.agents[0].cqcHoldingGuard == 0, "El jugador conserva una referencia al enemigo neutralizado");
    Require(std::ranges::any_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::Cqc; }), "CQC no invalida Maestro mediante el registro real");
  }
  void RejectionAndCancellation() {
    Simulation simulation; auto &world = Fixture(simulation);
    world.agents[0].facing = {-1.f,0.f};
    Require(Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Grab).has_value() && !Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Push), "El agarre depende de mirar al objetivo o el empujón pierde su requisito de orientación");
    auto held = Held(); held.aimX = 1.f; Advance(simulation, held, 5);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.agents[0].facing == sf::Vector2f{1.f, 0.f} && world.guards[0].facing == world.agents[0].facing, "Mirar en sentido opuesto impide agarrar o no orienta ambos cuerpos al contacto");
    Fixture(simulation);
    world.obstacles.push_back({{{212.f, 260.f}, {4.f, 80.f}}, CoverKind::High, true, "Pared"});
    world.agents[0].facing = {1.f,0.f}; held.aimX = 1.f; Advance(simulation, held, 5);
    Require(world.agents[0].cqcHoldingGuard == 0, "Se agarra atravesando paredes");
    Fixture(simulation); Advance(simulation, Held(), 3); Advance(simulation, {}, 11);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1, "Soltar F durante la sujeción activa una huida por temporizador ordinario");
    Advance(simulation, Held(), 24);
    Require(world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0 && world.guards[0].cqcState == CqcState::None, "El umbral de presión continua explícita no permite escapar al guardia");
    Fixture(simulation); Advance(simulation, Held(), 3); simulation.DisconnectPlayer(1);
    Require(world.guards[0].heldBy == 0 && world.guards[0].cqcState == CqcState::None, "La desconexión conserva un agarre huérfano");
  }
  void PushAndCollisions() {
    for (const bool partner : {false, true}) {
      Simulation simulation; auto &world = Fixture(simulation, partner);
      world.obstacles.push_back({{{270.f, 250.f}, {20.f, 100.f}}, CoverKind::High, true, "Pared de empuje"});
      ActionCommand push; push.push = true; push.aimX = 1.f;
      if (partner) world.agents[1].aiming = true;
      simulation.Step(0.1f, {{1, push}});
      const auto position = partner ? world.agents[1].position : world.guards[0].position;
      Require(position.x > 228.f && position.x <= 258.01f && !WorldGeometry::PositionBlocked(world, position, GameplayConfig::AgentRadius), "El empujón atraviesa la pared o no desplaza al personaje");
      const auto health = partner ? world.agents[1].health : world.guards[0].health;
      Require(health == (partner ? GameplayConfig::MaxHealth : CombatConfig::MaxGuardHealth), "El empujón causa daño letal");
      if (partner) Require(world.agents[1].staggerRemaining > 0.f && !world.agents[1].aiming, "El compañero empujado conserva el apuntado mientras está desestabilizado");
      simulation.Step(0.1f, {{1, push}});
      Require((partner ? world.agents[1].position : world.guards[0].position) == position, "El empujón no respeta su enfriamiento");
      Simulation replica; std::string error;
      Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "El cliente rechaza el estado tras un empujón");
    }
  }
  WorldState &RaisedPushFixture(Simulation &simulation, bool partner, float upper, float landing = 0.f) {
    auto &world = Fixture(simulation, partner);
    world.walkSurfaces = {
      {1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f},
      {10, upper >= LevelConfig::StoreyHeight * 2.f ? 2u : 1u, WalkSurfaceKind::Floor, {{100.f, 200.f}, {150.f, 200.f}}, upper}
    };
    if (landing > 0.f) world.walkSurfaces.push_back({11, 1, WalkSurfaceKind::Catwalk, {{250.f, 200.f}, {250.f, 200.f}}, landing});
    for (auto &agent : world.agents) { agent.elevation = upper; agent.supportId = 10; }
    for (auto &guard : world.guards) { guard.elevation = upper; guard.supportId = 10; guard.patrol.clear(); }
    return world;
  }
  void ElevatedPushFallsAndKillsAtTheReceivingFloor() {
    for (const bool partner : {false, true}) for (const auto heights : {sf::Vector2f{96.f, 0.f}, sf::Vector2f{192.f, 0.f}, sf::Vector2f{192.f, 96.f}}) {
      Simulation simulation; auto &world = RaisedPushFixture(simulation, partner, heights.x, heights.y);
      ActionCommand push; push.push = true; push.aimX = 1.f; simulation.Step(0.1f, {{1, push}});
      const auto &initial = partner ? world.agents[1].forcedFall : world.guards[0].forcedFall;
      Require(initial.active && initial.from.z == heights.x && initial.to.z == heights.y && initial.actor == 1,
        "El empujón del borde no inicia una caída hacia el piso real inferior");
      Require((partner ? world.agents[1].elevation : world.guards[0].elevation) == heights.x &&
        (partner ? world.agents[1].alive : world.guards[0].state != GuardState::Dead), "El empujón teletransporta o mata al objetivo antes de caer");
      Simulation replica; std::string error;
      const auto initialSnapshot = simulation.SnapshotFor(1);
      Require(replica.LoadSnapshot(initialSnapshot, error), "La instantánea rechaza el inicio físico de una caída por empujón");
      const auto collection = partner ? "agents" : "guards"; const auto index = partner ? 1 : 0;
      auto invalid = initialSnapshot; invalid[collection][index]["forcedFall"][2][2] = heights.x;
      Require(!replica.LoadSnapshot(invalid, error), "La réplica acepta una caída cuyo destino no está debajo del origen");
      invalid = initialSnapshot; invalid[collection][index]["forcedFall"][5] = 9999;
      Require(!replica.LoadSnapshot(invalid, error), "La réplica acepta un autor inexistente para una caída forzada");
      invalid = initialSnapshot; invalid[collection][index].erase("forcedFall");
      Require(!replica.LoadSnapshot(invalid, error), "El protocolo nuevo acepta un estado sin la caída obligatoria");
      bool intermediate = false; float previous = heights.x;
      for (unsigned int frame = 0; frame < 30 && (partner ? world.agents[1].forcedFall.active : world.guards[0].forcedFall.active); ++frame) {
        ActionCommand escape; escape.moveX = 1.f; escape.traverse = true;
        simulation.Step(0.1f, {{partner ? 2u : 1u, partner ? escape : ActionCommand{}}});
        const float z = partner ? world.agents[1].elevation : world.guards[0].elevation;
        Require(z <= previous && previous - z <= TraversalConfig::DropSpeed * 0.1f + 0.01f, "La caída por empujón salta de altura o permite ascender con controles");
        if (z > heights.y && z < heights.x) {
          intermediate = true;
          Require(partner ? world.agents[1].alive : world.guards[0].state != GuardState::Dead, "La caída confirma la muerte antes de aterrizar");
          const auto filtered = simulation.SnapshotFor(partner ? 2u : 1u, true);
          Require(replica.LoadSnapshot(filtered, error), "La instantánea filtrada rechaza una caída intermedia");
          for (const auto &entity : filtered.at(collection)) if (entity.at("forcedFall")[0].get<bool>()) {
            Require(entity.at("forcedFall")[5] == 0, "La caída filtrada revela la identidad privada del autor");
          }
        }
        previous = z;
      }
      const auto support = partner ? world.agents[1].supportId : world.guards[0].supportId;
      Require(intermediate && previous == heights.y && support == (heights.y > 0.f ? 11u : 1u), "La caída atraviesa el primer piso receptor o no alcanza un apoyo estable");
      Require(partner ? (!world.agents[1].alive && !world.agents[1].incapacitated && world.agents[1].lifeState == LifeState::Dead) : world.guards[0].state == GuardState::Dead,
        "Una caída desde la segunda o tercera planta no confirma la muerte al aterrizar");
      Require(std::ranges::count_if(world.events, [partner](const MissionEvent &event) {
        return event.kind == (partner ? EventKind::AgentKilled : EventKind::GuardKilled) && event.actor == 1 && event.target == (partner ? 2u : 9u);
      }) == 1, "La muerte por caída pierde su autor o duplica el registro operacional");
      Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "La réplica rechaza el cuerpo sobre el piso de aterrizaje");
    }
  }
  void ElevatedPushRespectsWallsAndTrainingProtection() {
    for (const bool partner : {false, true}) for (const bool lowerObstacle : {false, true}) {
      Simulation simulation; auto &world = RaisedPushFixture(simulation, partner, 96.f);
      Obstacle obstacle{{{lowerObstacle ? 252.f : 246.f, 250.f}, {lowerObstacle ? 24.f : 8.f, 100.f}}, CoverKind::High, true, "Volumen que bloquea la caída"};
      obstacle.elevation = lowerObstacle ? 0.f : 96.f; obstacle.height = lowerObstacle ? 40.f : 66.f; world.obstacles.push_back(obstacle);
      ActionCommand push; push.push = true; push.aimX = 1.f; simulation.Step(0.1f, {{1, push}});
      Require(!(partner ? world.agents[1].forcedFall.active : world.guards[0].forcedFall.active) &&
        (partner ? world.agents[1].supportId : world.guards[0].supportId) == 10,
        "El empujón precipita a través de una pared o de un sólido situado bajo el borde");
      Require((partner ? world.agents[1].elevation : world.guards[0].elevation) == 96.f,
        "Un volumen sólido desplaza al objetivo a otra planta sin una caída libre válida");
    }
    for (const bool partner : {false, true}) {
      Simulation simulation; auto &world = RaisedPushFixture(simulation, partner, 96.f);
      world.phase = GamePhase::Base; world.mode = SimulationMode::Training;
      if (!partner) world.guards[0].trainingDummy = true;
      ActionCommand push; push.push = true; push.aimX = 1.f; simulation.Step(0.1f, {{1, push}});
      if (!partner) {
        const auto airbornePosition = world.guards[0].position;
        Require(world.guards[0].forcedFall.active && !Interaction::FindBody(world, world.agents[0]), "Una diana en caída anuncia arrastre antes de aterrizar");
        ActionCommand drag; drag.dragBody = true; simulation.Step(1.f / 60.f, {{1, drag}});
        Require(world.agents[0].draggingGuard < 0 && world.guards[0].position == airbornePosition && world.guards[0].forcedFall.active,
          "Arrastrar una diana en el aire altera su trayectoria autoritativa");
        Simulation replica; std::string error;
        Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "La interacción rechazada con una diana en caída invalida la instantánea");
      }
      Advance(simulation, {}, 20);
      Require((partner ? world.agents[1].elevation : world.guards[0].elevation) == 0.f &&
        (partner ? world.agents[1].alive && !world.agents[1].incapacitated : world.guards[0].state != GuardState::Dead && world.guards[0].state != GuardState::Incapacitated),
        "La caída de prácticas mata o incapacita al objetivo protegido");
      Require(CqcEvents(world) == 0, "El empujón de prácticas añade penalizaciones operacionales");
    }
  }
  void PhaseChangesDiscardAnUnfinishedPushFall() {
    Simulation simulation; auto &world = RaisedPushFixture(simulation, true, 192.f);
    ActionCommand push; push.push = true; push.aimX = 1.f; simulation.Step(0.1f, {{1, push}});
    Require(world.agents[1].forcedFall.active, "La fixture no inicia una caída antes de regresar a la base");
    simulation.ReturnToBase();
    Require(std::ranges::none_of(world.agents, [](const Agent &agent) { return agent.forcedFall.active; }), "Regresar a la base conserva una caída pendiente de la misión");
    Simulation replica; std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "Regresar durante una caída genera una instantánea de base inválida");
    RaisedPushFixture(simulation, true, 96.f); world.phase = GamePhase::Base; world.mode = SimulationMode::Training;
    simulation.Step(0.1f, {{1, push}});
    Require(world.agents[1].forcedFall.active, "La fixture no inicia la caída de prácticas antes del despliegue");
    simulation.Deploy(41);
    Require(std::ranges::none_of(world.agents, [](const Agent &agent) { return agent.forcedFall.active; }), "El despliegue arrastra la caída pendiente a la posición inicial de misión");
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "Desplegar durante una caída genera una instantánea de misión inválida");
    simulation.Step(0.1f, {});
    Require(std::ranges::all_of(world.agents, [](const Agent &agent) { return agent.alive && !agent.forcedFall.active; }), "La caída anterior mata al agente después de reaparecer en la misión");
  }
  void PushIntoAVoidKeepsAValidTerminalState() {
    for (const bool partner : {false, true}) {
      Simulation simulation; auto &world = RaisedPushFixture(simulation, partner, 96.f); world.walkSurfaces[0].enabled = false;
      ActionCommand push; push.push = true; push.aimX = 1.f; simulation.Step(0.1f, {{1, push}}); Advance(simulation, {}, 24);
      Require((partner ? world.agents[1].elevation : world.guards[0].elevation) == LevelConfig::MinimumElevation &&
        (partner ? !world.agents[1].alive : world.guards[0].state == GuardState::Dead), "La caída al vacío conserva un objetivo vivo o suspendido antes del límite inferior");
      Simulation replica; std::string error;
      Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "La caída mortal al vacío genera un cuerpo sin estado espacial válido");
    }
  }
  void RisingCabinCatchesThePushedCharacter() {
    for (const bool partner : {false, true}) for (const float top : {48.1f, 48.3f}) {
      Simulation simulation; auto &world = RaisedPushFixture(simulation, partner, 96.f);
      world.walkSurfaces.push_back({90, 0, WalkSurfaceKind::ElevatorCabin, {{252.f, 280.f}, {40.f, 40.f}}, 43.5f});
      ActionCommand push; push.push = true; push.aimX = 1.f; simulation.Step(0.1f, {{1, push}}); Advance(simulation, {}, 4);
      Require((partner ? world.agents[1].elevation : world.guards[0].elevation) == 48.f,
        "La fixture no alcanza el contacto intermedio con la cabina ascendente");
      world.walkSurfaces.back().elevation = top; simulation.Step(0.1f, {});
      Require(!(partner ? world.agents[1].forcedFall.active : world.guards[0].forcedFall.active) &&
        (partner ? world.agents[1].supportId : world.guards[0].supportId) == 90 && (partner ? world.agents[1].elevation : world.guards[0].elevation) == top,
        "La cabina que cruza los pies deja una caída activa congelada sin apoyo");
      Require(partner ? world.agents[1].alive : world.guards[0].state != GuardState::Dead, "La cabina convierte el contacto de menos de una planta en una caída mortal");
      Simulation replica; std::string error;
      Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "El contacto ascendente genera progreso o destino de caída incoherentes");
    }
  }
  void PushInterruptsPuzzle() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    Interactable relay{90, ObjectKind::CoopSwitch, {265.f, 300.f}, "Relé de coordinación"};
    relay.puzzle.kind = PuzzleKind::Coordination; relay.puzzle.family = PuzzleFamily::Coordination;
    relay.solid = SpatialOverride::Disabled; relay.blocksVision = SpatialOverride::Disabled;
    world.objects.push_back(relay);
    ActionCommand use; use.interact = true; simulation.Step(0.1f, {{2, use}});
    Require(world.agents[1].puzzle.target == relay.id, "El compañero no inicia la coordinación antes del empujón");
    ActionCommand push; push.push = true; push.aimX = 1.f; simulation.Step(0.1f, {{1, push}});
    Require(world.agents[1].puzzle.target == 0 && world.objects[0].user == 0 && world.objects[0].puzzle.coordinationProgress == 0.f,
      "El empujón conserva un operador o progreso de coordinación incompatible");
    const float stagger = world.agents[1].staggerRemaining;
    simulation.Step(0.1f, {});
    Require(world.agents[1].staggerRemaining < stagger, "La interfaz de puzle mantiene congelada la recuperación del empujón");
  }
  void TrainingCqcSafety() {
    Simulation simulation; auto &world = Fixture(simulation);
    world.phase = GamePhase::Base; world.mode = SimulationMode::Training; world.guards[0].trainingDummy = true;
    for (int attack = 0; attack < 12; ++attack) { ShortPress(simulation); Advance(simulation, {}, 9); }
    Require(world.guards[0].trainingHits >= 12 && world.guards[0].health >= 1.f && world.guards[0].state != GuardState::Dead && world.guards[0].state != GuardState::Incapacitated, "Los golpes repetidos incapacitan o eliminan un muñeco de entrenamiento");
    Require(CqcEvents(world) == 0, "El CQC de entrenamiento añade penalizaciones de la operación");
    world.agents[0].facing = {1.f, 0.f}; world.guards[0].facing = {1.f, 0.f}; Advance(simulation, Held(), 3);
    Require(world.guards[0].heldBy == 1, "El muñeco no admite agarres reales de entrenamiento");
    for (unsigned int pulse = 0; pulse < CqcConfig::RequiredPulses; ++pulse) { Advance(simulation, {}, 3); auto command = Held(); command.cqc = true; simulation.Step(0.1f, {{1, command}}); }
    Require(world.guards[0].heldBy == 0 && world.agents[0].cqcHoldingGuard == 0 && world.guards[0].state != GuardState::Incapacitated && world.guards[0].health >= 1.f, "La asfixia activa una incapacitación real o bloquea al muñeco de prácticas");
    Fixture(simulation, true); world.phase = GamePhase::Base; world.mode = SimulationMode::Training;
    Advance(simulation, Held(), 3);
    Require(world.agents[0].cqcHoldingAgent == 2, "No se puede practicar agarre amistoso en entrenamiento");
    for (unsigned int pulse = 0; pulse < CqcConfig::RequiredPulses; ++pulse) { Advance(simulation, {}, 3); auto command = Held(); command.cqc = true; simulation.Step(0.1f, {{1, command}}); }
    Require(world.agents[1].alive && !world.agents[1].incapacitated && !world.agents[1].friendlyIncapacitated && world.agents[1].health >= 1.f && world.agents[1].stamina >= 1.f, "El sometimiento amistoso omite la regla de seguridad del entrenamiento");
    Require(world.agents[0].cqcHoldingAgent == 0 && world.agents[1].cqcHeldBy == 0 && world.phase == GamePhase::Base, "El entrenamiento deja participantes sujetos o activa derrota tras asfixia");
  }
  void ContextualWallAndHeldPriority() {
    Simulation simulation; auto &world = Fixture(simulation);
    const Obstacle wall{{{160.f, 250.f}, {20.f, 100.f}}, CoverKind::High, true, "Pared contextual"};
    world.obstacles.push_back(wall);
    ActionCommand cover; cover.coverToggle = true; simulation.Step(0.1f, {{1, cover}});
    Require(world.agents[0].cover.active, "El escenario contextual no permite adherirse a su pared real");
    const auto attached = world.agents[0].cover;
    auto press = Held(); press.cqc = true; simulation.Step(0.1f, {{1, press}});
    Require(world.agents[0].actionPose == ActionPose::KnockWall && !world.agents[0].cqcPressPending && world.agents[0].cqcHoldingGuard == 0 && CqcEvents(world) == 0, "F en cobertura ejecuta simultáneamente combate o agarre y golpe a pared");
    Require(std::ranges::any_of(world.noises, [](const NoiseEvent &noise) { return noise.kind == SoundKind::Knock && std::abs(noise.position.x - 180.f) < 0.1f; }), "El golpe contextual no produce ruido en el contacto de la pared");
    Advance(simulation, press, 3);
    Require(std::ranges::count_if(world.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::Knock; }) == 1, "Mantener F repite automáticamente el golpe a pared");
    Fixture(simulation); Advance(simulation, Held(), 3);
    world.obstacles.push_back(wall); world.agents[0].cover = attached; world.agents[0].position = attached.anchor;
    Advance(simulation, {}, 2); simulation.Step(0.1f, {{1, press}});
    Require(world.agents[0].cqcProgress == 1 && world.guards[0].heldBy == 1 && !world.agents[0].cover.active, "La pared recibe prioridad sobre un enemigo ya agarrado");
    Require(std::ranges::none_of(world.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::Knock; }), "El sometimiento emite también un golpe de pared");
  }
  void PhysicalRadialAlignment() {
    Simulation simulation, replica; auto &world = Fixture(simulation);
    world.phase = GamePhase::Base; world.mode = SimulationMode::Training; world.guards[0].trainingDummy = true; world.guards[0].facing = {-1.f, 0.f};
    world.guards[0].position = {270.f, 300.f};
    const auto targetPosition = world.guards[0].position;
    std::string error;
    bool approached = false;
    for (int frame = 0; frame < 12 && world.agents[0].cqcHoldingGuard == 0; ++frame) {
      const auto previous = world.agents[0].position;
      simulation.Step(0.1f, {{1, Held()}});
      Require((world.agents[0].position - previous).length() <= CqcConfig::ApproachSpeed * 0.1f + 0.1f && world.guards[0].position == targetPosition, "La colocación del agarre salta posiciones o arrastra al objetivo");
      Require((world.agents[0].position - targetPosition).length() >= CqcConfig::MinimumGripDistance - 0.1f, "La aproximación atraviesa el cuerpo del objetivo");
      if (world.agents[0].cqcApproachGuard != 0) { approached = true; Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "La aproximación CQC no se sincroniza al cliente"); }
    }
    Require(approached && world.guards[0].heldBy == 1 && world.agents[0].cqcHoldingGuard == 9, "Mantener F no alcanza físicamente el contacto radial de un objetivo con otra orientación");
    Require((world.agents[0].position - sf::Vector2f{242.f, 300.f}).length() <= CqcConfig::ContactTolerance && world.agents[0].facing == sf::Vector2f{1.f, 0.f} && world.guards[0].facing == world.agents[0].facing, "El contacto radial no deja al objetivo delante y de espaldas al jugador");
    Fixture(simulation); world.guards[0].state = GuardState::Alert; world.guards[0].suspicion = 1.f;
    world.agents[0].cqcPressPending = true; world.agents[0].cqcInputDown = true; world.agents[0].cqcHoldTime = CqcConfig::AcquireHold;
    simulation.Step(0.1f, {{1, Held()}});
    Require(world.guards[0].heldBy == 1, "Se rechaza un agarre físicamente válido solo por el estado de alerta previo");
    Fixture(simulation); world.phase = GamePhase::Base; world.mode = SimulationMode::Training; world.guards[0].trainingDummy = true; world.guards[0].facing = {-1.f, 0.f};
    world.guards[0].position = {270.f, 300.f};
    world.obstacles.push_back({{{190.f, 315.f}, {80.f, 25.f}}, CoverKind::High, true, "Pared que cierra la aproximación"});
    world.obstacles.push_back({{{190.f, 260.f}, {80.f, 25.f}}, CoverKind::High, true, "Pared que cierra la ruta alternativa"});
    Advance(simulation, Held(), 12);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].position == sf::Vector2f{270.f, 300.f} && std::abs(world.agents[0].position.y - 300.f) < 0.01f && !WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius), "El agarre intenta rodear al objetivo pese a tener libre el contacto radial entre las paredes");
  }
  void StableGripAndExtendedApproach() {
    Simulation simulation; auto &world = Fixture(simulation);
    Advance(simulation, Held(), 3);
    Advance(simulation, Held(), 60);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1 && world.agents[0].cqcProgress == 0,
      "La misma pulsación prolongada que inicia el agarre lo libera o se convierte en sometimiento automático");
    world.agents[0].stamina = 0.f;
    simulation.Step(0.1f, {});
    Require(world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0 && world.guards[0].cqcState == CqcState::None && world.guards[0].state != GuardState::Incapacitated,
      "Agotar la estamina no suelta recíprocamente al objetivo en el mismo turno");
    Fixture(simulation); Advance(simulation, Held(), 3); Advance(simulation, {}, 50);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1, "Una sujeción con estamina termina por un temporizador ordinario sin presión de F");
    Advance(simulation, Held(), 20);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.agents[0].cqcProgress == 1, "La presión continua escapa antes del umbral propio o suma pulsaciones automáticas");
    Advance(simulation, Held(), 4);
    Require(world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0 && world.guards[0].state != GuardState::Incapacitated,
      "Mantener F más allá del umbral no libera físicamente al objetivo consciente");
    for (const bool front : {false, true}) {
      Fixture(simulation); world.phase = GamePhase::Base; world.mode = SimulationMode::Training; world.guards[0].trainingDummy = true;
      world.guards[0].position = {270.f, 300.f}; world.guards[0].facing = front ? sf::Vector2f{-1.f, 0.f} : sf::Vector2f{1.f, 0.f};
      const auto target = world.guards[0].position;
      for (int frame = 0; frame < 30 && world.agents[0].cqcHoldingGuard == 0; ++frame) {
        const auto previous = world.agents[0].position;
        simulation.Step(0.1f, {{1, Held()}});
        Require((world.agents[0].position - previous).length() <= CqcConfig::ApproachSpeed * 0.1f + 0.1f && world.guards[0].position == target,
          "La aproximación ampliada salta posiciones o arrastra al objetivo");
      }
      Require(world.agents[0].cqcHoldingGuard == 9 && (target - world.agents[0].position).length() <= CqcConfig::GripReach,
        "El alcance de interacción ampliado no aproxima físicamente al objetivo hasta una sujeción válida");
    }
  }
  void CollapseCancelsGripWithoutRecovery() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    Advance(simulation, Held(), 3);
    Require(world.agents[0].cqcHoldingAgent == 2, "La regresión de caída requiere una sujeción amistosa ya establecida");
    world.agents[0].health = 0.f;
    simulation.Step(0.1f, {{1, Held()}});
    Require(world.agents[0].lifeState == LifeState::Incapacitated && world.agents[0].health == 0.f && world.agents[0].cqcHoldingAgent == 0 && world.agents[1].cqcHeldBy == 0,
      "Llegar a salud cero no cancela recíprocamente el agarre antes de fijar la incapacitación");
    Advance(simulation, Held(), 40);
    Require(world.agents[0].lifeState == LifeState::Incapacitated && world.agents[0].health == 0.f && world.agents[0].stamina == 0.f && world.agents[0].incapacityRemaining < GameplayConfig::IncapacityDuration - 3.f,
      "Las órdenes CQC o la regeneración de resistencia revierten una incapacitación confirmada");
    ActionCommand rescue; rescue.interact = true;
    simulation.Step(0.1f, {{2, rescue}});
    Require(world.agents[0].lifeState == LifeState::Alive && world.agents[0].health >= GameplayConfig::RevivedHealth && world.agents[0].cqcHoldingAgent == 0,
      "El rescate explícito no recupera al agente o restaura accidentalmente el agarre anterior");
  }

  void SharedTargetQueryMatchesThePush() {
    Simulation simulation; auto &world = Fixture(simulation);
    world.guards[0].position = {232.f, 300.f}; world.guards[0].patrol = {world.guards[0].position};
    Guard alternative; alternative.id = 10; alternative.position = {222.f, 325.5f}; alternative.facing = {1.f, 0.f};
    alternative.patrol = {alternative.position}; alternative.attackCooldown = 60.f; world.guards.push_back(alternative);
    auto target = Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Push);
    Require(target && target->guard == 9 && target->agent == 0, "El objetivo contextual no prioriza un personaje válido cercano y orientado");
    world.obstacles.push_back({{{248.f, 285.f}, {10.f, 30.f}}, CoverKind::High, true, "Respaldo que impide el primer empujón"});
    target = Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Push);
    Require(target && target->guard == 10, "La consulta anuncia un empujón bloqueado en vez del siguiente objetivo válido");
    const auto first = world.guards[0].position, second = world.guards[1].position;
    ActionCommand push; push.push = true; simulation.Step(0.1f, {{1, push}});
    Require((world.guards[0].position - first).length() <= GameplayConfig::GuardSpeed * 0.1f + 0.01f && world.guards[0].staggerRemaining == 0.f &&
      world.guards[1].position != second && world.agents[0].actionPose == ActionPose::Push &&
      std::ranges::any_of(world.events, [](const MissionEvent &event) { return event.kind == EventKind::Cqc && event.target == 10; }) &&
      !WorldGeometry::PositionBlocked(world, world.guards[1].position, GameplayConfig::AgentRadius),
      "El empujón autoritativo no coincide con la consulta contextual o atraviesa geometría");
    Require(!Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Push), "El hint mantiene un empujón durante su enfriamiento");
    Fixture(simulation, true);
    target = Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Push);
    Require(target && target->agent == 2, "El contrato compartido no anuncia el empujón amistoso válido");
    world.agents[0].reloadRemaining = 0.5f;
    Require(!Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Push), "Se anuncia un empujón durante una recarga incompatible");
    world.agents[0].reloadRemaining = 0.f; Advance(simulation, Held(), 3);
    const auto actorPosition = world.agents[0].position, partnerPosition = world.agents[1].position;
    const float stamina = world.agents[0].stamina;
    simulation.Step(0.1f, {{1, push}});
    Require(world.agents[0].cqcHoldingAgent == 2 && world.agents[1].cqcHeldBy == 1 && world.agents[0].position == actorPosition && world.agents[1].position == partnerPosition &&
      world.agents[0].stamina >= stamina && world.agents[1].health == GameplayConfig::MaxHealth && world.agents[0].actionPose != ActionPose::Push,
      "V durante una sujeción amistosa libera, empuja o daña al compañero");
  }

  void RadialApproachReservesAiAndCommitsAtContact() {
    const std::array<sf::Vector2f, 8> directions{{{1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}, {-1.f, 1.f}, {-1.f, 0.f}, {-1.f, -1.f}, {0.f, -1.f}, {1.f, -1.f}}};
    for (auto direction : directions) {
      Simulation simulation; auto &world = Fixture(simulation);
      world.guards[0].position = {250.f, 300.f}; world.guards[0].facing = direction.normalized(); world.guards[0].patrol = {world.guards[0].position};
      bool reserved = false;
      sf::Vector2f reservedPosition, reservedFacing;
      for (unsigned int step = 0; step < 35 && world.agents[0].cqcHoldingGuard == 0; ++step) {
        const auto previous = world.agents[0].position;
        simulation.Step(0.1f, {{1, Held()}});
        const auto &actor = world.agents[0]; const auto &guard = world.guards[0];
        Require((actor.position - previous).length() <= CqcConfig::ApproachSpeed * 0.1f + 0.1f &&
          !WorldGeometry::PositionBlocked(world, actor.position, GameplayConfig::AgentRadius) &&
          (actor.position - guard.position).length() >= CqcConfig::MinimumGripDistance - 0.1f,
          "La colocación radial atraviesa cuerpo o geometría o salta más allá de su velocidad física");
        Require(std::abs(actor.position.y - 300.f) < 0.01f, "El captor rodea al objetivo según su orientación anterior");
        if (reserved && actor.cqcHoldingGuard == 0) Require(guard.position == reservedPosition && guard.facing == reservedFacing && guard.attackTime == 0.f,
          "La IA mueve, gira o ataca con el objetivo reservado durante la aproximación CQC");
        if (actor.cqcApproachGuard != 0 && !reserved) { reserved = true; reservedPosition = guard.position; reservedFacing = guard.facing; }
        Require(actor.cqcHoldingGuard != 0 || (guard.heldBy == 0 && guard.cqcState == CqcState::None && actor.actionPose != ActionPose::Grab),
          "La animación o las referencias de agarre comienzan antes de alcanzar físicamente al objetivo");
      }
      const auto &actor = world.agents[0]; const auto &guard = world.guards[0];
      Require(actor.cqcHoldingGuard == guard.id && guard.heldBy == actor.id && guard.cqcState == CqcState::Grabbed && guard.health == CombatConfig::MaxGuardHealth,
        "Mantener F no completa una sujeción recíproca desde una orientación válida del objetivo");
      Require((actor.position - sf::Vector2f{222.f, 300.f}).length() <= CqcConfig::ContactTolerance && actor.facing == sf::Vector2f{1.f, 0.f} && guard.facing == actor.facing,
        "La reserva no confirma el contacto radial girando ambos cuerpos sobre el mismo eje");
      const auto actorPosition = actor.position, guardPosition = guard.position;
      Advance(simulation, {}, 10);
      Require(world.agents[0].position == actorPosition && world.guards[0].position == guardPosition && world.guards[0].heldBy == 1,
        "El agarre confirmado se separa o es sobrescrito por el movimiento normal de la IA");
    }
  }

  void RadialPlacementRejectsCharactersAndInterruptedReservations() {
    Simulation simulation; auto &world = Fixture(simulation, true);
    world.phase = GamePhase::Base; world.mode = SimulationMode::Training;
    Guard guard; guard.id = 9; guard.position = {270.f, 300.f}; guard.facing = {-1.f, 0.f}; guard.patrol = {guard.position}; guard.attackCooldown = 60.f; guard.trainingDummy = true; world.guards.push_back(guard);
    world.agents[1].position = {242.f, 300.f}; world.agents[1].stance = Stance::Prone;
    Require(!Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Grab), "El hint ignora a un tercero que ocupa el contacto radial");
    const auto actorPosition = world.agents[0].position;
    Advance(simulation, Held(), 10);
    Require(world.agents[0].position == actorPosition && world.agents[0].cqcApproachGuard == 0 && world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0,
      "Una colocación radial ocupada por un tercero inicia parcialmente el movimiento o la sujeción");
    Fixture(simulation); world.guards[0].position = {270.f, 300.f}; world.guards[0].facing = {-1.f, 0.f}; world.guards[0].patrol = {world.guards[0].position};
    Advance(simulation, Held(), 3);
    Require(world.agents[0].cqcApproachGuard == 9 && world.guards[0].heldBy == 0, "La prueba de interrupción necesita una aproximación reservada sin agarre final");
    world.agents[0].staggerRemaining = CqcConfig::PushStagger; simulation.Step(0.1f, {{1, Held()}});
    Require(world.agents[0].cqcApproachGuard == 0 && world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0,
      "Recibir daño deja una reserva de aproximación indefinida o confirma un agarre interrumpido");
    Fixture(simulation, true); world.agents[1].hidden = true; world.agents[1].position = {50.f, 700.f};
    guard.position = {270.f, 300.f}; guard.facing = {-1.f, 0.f}; guard.patrol = {guard.position}; guard.trainingDummy = false; world.guards.push_back(guard);
    Advance(simulation, Held(), 3);
    Require(world.agents[0].cqcApproachGuard == 9 && world.guards[0].heldBy == 0, "La amenaza externa requiere una aproximación pendiente real");
    world.agents[1].position = world.guards[0].position + sf::Vector2f{-60.f, -60.f}; world.agents[1].hidden = false;
    Require(simulation.CanSee(world.guards[0].position, world.guards[0].facing, world.agents[1]), "El tercero debe constituir una amenaza visual válida");
    simulation.Step(0.1f, {{1, Held()}});
    Require(world.agents[0].cqcApproachGuard == 0 && world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0,
      "La reserva de aproximación impide reaccionar a una amenaza visual externa");
  }
  void ContactRequiresPhysicalRadialPlacement() {
    const std::array<sf::Vector2f, 8> directions{{{1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}, {-1.f, 1.f}, {-1.f, 0.f}, {-1.f, -1.f}, {0.f, -1.f}, {1.f, -1.f}}};
    constexpr float dt = 1.f / 60.f;
    for (const bool partner : {false, true}) for (const float separation : {22.f, 37.f, 74.f}) for (std::size_t positionIndex = 0; positionIndex < directions.size(); ++positionIndex) for (std::size_t facingIndex = 0; facingIndex < directions.size(); ++facingIndex) {
      Simulation simulation, replica; auto &world = Fixture(simulation, partner);
      const auto direction = directions[positionIndex].normalized(), targetFacing = directions[facingIndex].normalized();
      const sf::Vector2f target{500.f, 500.f}, start = target - direction * separation;
      world.agents[0].position = start; world.agents[0].facing = directions[(positionIndex + facingIndex + 4) % directions.size()].normalized();
      if (partner) { world.agents[1].position = target; world.agents[1].facing = targetFacing; }
      else { world.guards[0].position = target; world.guards[0].facing = targetFacing; world.guards[0].patrol = {target}; }
      const auto contact = target - direction * CqcConfig::GripSeparation;
      bool held = false;
      for (unsigned int frame = 0; frame < 180 && !held; ++frame) {
        const auto previous = world.agents[0].position;
        simulation.Step(dt, {{1, Held()}});
        const auto &actor = world.agents[0]; held = actor.cqcHoldingGuard != 0 || actor.cqcHoldingAgent != 0;
        Require((actor.position - previous).length() <= CqcConfig::ApproachSpeed * dt + 0.01f, "Normalizar el contacto CQC teletransporta al captor");
        const auto displacement = actor.position - start;
        Require(std::abs(displacement.x * direction.y - displacement.y * direction.x) < 0.02f && (target - actor.position).dot(direction) >= CqcConfig::MinimumGripDistance - 0.1f,
          "La aproximación abandona el eje radial o atraviesa al objetivo para alcanzar su espalda anterior");
        Require((partner ? world.agents[1].position : world.guards[0].position) == target, "Normalizar el contacto CQC desplaza al objetivo");
        if (!held) Require(actor.actionPose != ActionPose::Grab && (partner ? world.agents[1].cqcHeldBy == 0 : world.guards[0].heldBy == 0), "La sujeción se confirma antes del contacto corporal real");
      }
      Require(held && (world.agents[0].position - contact).length() <= CqcConfig::ContactTolerance, "La adquisición CQC no termina en el contacto radial a 28 unidades");
      Require(std::abs((world.agents[0].position - target).length() - CqcConfig::GripSeparation) <= CqcConfig::ContactTolerance, "La sujeción conserva una separación arbitraria del rango de adquisición");
      Require(world.agents[0].facing.dot(direction) > 0.999f && (partner ? world.agents[1].facing : world.guards[0].facing).dot(direction) > 0.999f,
        "El agarre no termina con el objetivo delante del captor y ambos orientados sobre ese eje");
      std::string error;
      Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "El contacto CQC normalizado no llega íntegro a la réplica");
    }
  }
  void RadialAcquisitionRespectsItsRangeAndSupports() {
    for (const bool partner : {false, true}) {
      Simulation simulation, replica; auto &world = Fixture(simulation, partner);
      world.agents[0].facing = {-1.f, 0.f};
      const sf::Vector2f target{200.f + CqcConfig::AcquireDistance, 300.f};
      if (partner) world.agents[1].position = target;
      else { world.guards[0].position = target; world.guards[0].patrol = {target}; }
      const auto selected = Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Grab);
      Require(selected && (partner ? selected->agent == 2 : selected->guard == 9), "El límite exacto de alcance rechaza el agarre cuando el jugador mira en sentido opuesto");
      Advance(simulation, Held(), 12);
      Require(partner ? world.agents[0].cqcHoldingAgent == 2 : world.agents[0].cqcHoldingGuard == 9, "El agarre anunciado a 76 unidades no alcanza físicamente al objetivo");
      for (const bool stair : {false, true}) {
        Fixture(simulation, partner); world.phase = GamePhase::Base; world.mode = SimulationMode::Training; world.size = {1900.f, 1740.f};
        if (!partner) { world.guards[0].trainingDummy = true; world.guards[0].patrol.clear(); }
        if (stair) {
          world.walkSurfaces = {{20, 0, WalkSurfaceKind::Stair, {{200.f, 1340.f}, {80.f, 192.f}}, 96.f, {0.f, -0.5f}}};
          world.agents[0].position = {230.f, 1400.f}; world.agents[0].elevation = 66.f; world.agents[0].supportId = 20;
        } else {
          world.walkSurfaces = {{1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f},
            {10, 1, WalkSurfaceKind::Floor, {{100.f, 200.f}, {116.f, 200.f}}, 96.f},
            {11, 1, WalkSurfaceKind::Floor, {{254.f, 200.f}, {96.f, 200.f}}, 96.f}};
          world.agents[0].elevation = 96.f; world.agents[0].supportId = 10;
        }
        const auto position = stair ? sf::Vector2f{242.f, 1416.f} : sf::Vector2f{270.f, 300.f};
        const float elevation = stair ? 58.f : 96.f; const std::uint32_t support = stair ? 20u : 11u;
        if (partner) { world.agents[1].position = position; world.agents[1].elevation = elevation; world.agents[1].supportId = support; }
        else { world.guards[0].position = position; world.guards[0].elevation = elevation; world.guards[0].supportId = support; }
        const auto original = WorldGeometry::Feet(world.agents[0]);
        Require(!Simulation::FindCqcTarget(world, world.agents[0], Simulation::CqcAction::Grab), "El hint anuncia contacto sobre un hueco o con una diferencia final de altura inválida en la escalera");
        Advance(simulation, Held(), 8);
        Require(WorldGeometry::Feet(world.agents[0]) == original && world.agents[0].cqcHoldingAgent == 0 && world.agents[0].cqcHoldingGuard == 0 && world.agents[0].cqcApproachAgent == 0 && world.agents[0].cqcApproachGuard == 0,
          "El agarre mueve al captor a un contacto sin apoyo o demasiado alto sobre el objetivo");
        std::string error;
        Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "Rechazar el contacto radial sin apoyo válido rompe el snapshot espacial");
      }
    }
  }
  void RequireReleasedGripSnapshot(Simulation &simulation, bool partner) {
    const auto &world = simulation.GetState(); const auto &actor = world.agents[0];
    Require(actor.cqcHoldingGuard == 0 && actor.cqcHoldingAgent == 0 && actor.cqcApproachGuard == 0 && actor.cqcApproachAgent == 0 && actor.cqcProgress == 0 &&
      (partner ? world.agents[1].cqcHeldBy == 0 : world.guards[0].heldBy == 0 && world.guards[0].cqcState == CqcState::None),
      "Soltar por estamina deja progreso o referencias CQC en uno de los participantes");
    Simulation replica; std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error) && replica.GetState().agents[0].cqcHoldingAgent == 0 && replica.GetState().agents[0].cqcHoldingGuard == 0,
      "La liberación por estamina no llega íntegra al snapshot completo");
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "La liberación por estamina produce referencias huérfanas en el snapshot privado");
    if (partner) {
      Require(replica.LoadSnapshot(simulation.SnapshotFor(2, true), error), "La liberación del compañero no llega a su snapshot privado");
      const auto recipient = std::ranges::find(replica.GetState().agents, 2u, &Agent::id);
      Require(recipient != replica.GetState().agents.end() && recipient->cqcHeldBy == 0, "El compañero liberado conserva un captor en su snapshot privado");
    }
  }
  void ExhaustionReleasesAtThePulseBoundary() {
    for (const bool partner : {false, true}) {
      Simulation simulation; auto &world = Fixture(simulation, partner);
      Advance(simulation, Held(), 3); world.agents[0].stamina = 0.f;
      simulation.Step(0.1f, {{1, Held()}});
      RequireReleasedGripSnapshot(simulation, partner);
      Require(world.agents[0].stamina == 0.f && world.agents[0].lifeState == LifeState::Alive, "Soltar por estamina incapacita al captor o regenera resistencia dentro del mismo turno");
      for (const float stamina : {1.f, GameplayConfig::CqcStaminaCost - 0.01f}) {
        Fixture(simulation, partner); Advance(simulation, Held(), 3); Advance(simulation, {}, 3);
        const int events = CqcEvents(world); world.agents[0].stamina = stamina;
        auto pulse = Held(); pulse.cqc = true; simulation.Step(0.1f, {{1, pulse}});
        RequireReleasedGripSnapshot(simulation, partner);
        Require(world.agents[0].stamina == stamina && CqcEvents(world) == events && (partner ? !world.agents[1].incapacitated : world.guards[0].stun == 0.f && world.guards[0].state != GuardState::Incapacitated),
          "Una pulsación sin el coste completo progresa, consume resistencia o incapacita antes de soltar");
      }
      Fixture(simulation, partner); Advance(simulation, Held(), 3); Advance(simulation, {}, 3);
      const int events = CqcEvents(world); world.agents[0].stamina = GameplayConfig::CqcStaminaCost * 2.f;
      auto pulse = Held(); pulse.cqc = true;
      simulation.Step(0.1f, {{1, pulse}});
      Require(world.agents[0].stamina == GameplayConfig::CqcStaminaCost && world.agents[0].cqcProgress == 1 && (partner ? world.agents[1].cqcHeldBy == 1 : world.guards[0].heldBy == 1),
        "La primera pulsación con saldo para dos fases no conserva la sujeción y el coste real");
      Advance(simulation, {}, 3); simulation.Step(0.1f, {{1, pulse}});
      RequireReleasedGripSnapshot(simulation, partner);
      Require(world.agents[0].stamina == 0.f && CqcEvents(world) == events + (partner ? 0 : 2) && (partner ? !world.agents[1].incapacitated : world.guards[0].state != GuardState::Incapacitated),
        "Gastar las últimas 12 unidades en una fase intermedia deja el agarre activo o completa una fase extra");
      Fixture(simulation, partner); Advance(simulation, Held(), 3);
      for (unsigned int progress = 1; progress < CqcConfig::RequiredPulses; ++progress) {
        Advance(simulation, {}, 3); simulation.Step(0.1f, {{1, pulse}});
        Require(world.agents[0].cqcProgress == progress, "La regresión del último pulso no alcanza cada fase con entradas reales");
      }
      Advance(simulation, {}, 3); world.agents[0].stamina = GameplayConfig::CqcStaminaCost;
      simulation.Step(0.1f, {{1, pulse}});
      RequireReleasedGripSnapshot(simulation, partner);
      Require(world.agents[0].stamina == 0.f && (partner ? world.agents[1].alive && world.agents[1].incapacitated && world.agents[1].friendlyIncapacitated && world.agents[1].health == GameplayConfig::MaxHealth : world.guards[0].state == GuardState::Incapacitated && world.guards[0].health == CombatConfig::MaxGuardHealth),
        "El último pulso con coste exacto no completa el sometimiento no letal liberando la pareja ese mismo turno");
    }
  }
}
int main() {
  try { AcquisitionAndSubdue(); RejectionAndCancellation(); PushAndCollisions(); ElevatedPushFallsAndKillsAtTheReceivingFloor(); ElevatedPushRespectsWallsAndTrainingProtection(); PhaseChangesDiscardAnUnfinishedPushFall(); PushIntoAVoidKeepsAValidTerminalState(); RisingCabinCatchesThePushedCharacter(); PushInterruptsPuzzle(); PunchKickAndRecovery(); ImpactValidationAndRadialApproach(); FriendlyGripAndCleanup(); TrainingCqcSafety(); ContextualWallAndHeldPriority(); PhysicalRadialAlignment(); StableGripAndExtendedApproach(); CollapseCancelsGripWithoutRecovery(); SharedTargetQueryMatchesThePush(); RadialApproachReservesAiAndCommitsAtContact(); ContactRequiresPhysicalRadialPlacement(); RadialPlacementRejectsCharactersAndInterruptedReservations(); RadialAcquisitionRespectsItsRangeAndSupports(); ExhaustionReleasesAtThePulseBoundary(); std::cout << "PASS CQC y empujones: " << checks << " comprobaciones\n"; return 0; }
  catch (const std::exception &error) { std::cerr << "FAIL CQC tras " << checks << ": " << error.what() << '\n'; return 1; }
}
