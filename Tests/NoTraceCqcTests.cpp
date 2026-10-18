#include "NoTraceSimulation.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceVisibility.h"
#include "Config/NoTraceCqcConfig.h"
#include <algorithm>
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
  void ImpactValidationAndRearApproach() {
    Simulation simulation; auto &world = Fixture(simulation);
    world.guards[0].facing = {-1.f, 0.f};
    Advance(simulation, Held(), 5);
    Require(world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0 && CqcEvents(world) == 0, "Mantener F permite agarrar de frente o produce golpes automáticos");
    simulation.Step(0.1f, {}); Advance(simulation, {}, 3);
    Require(world.agents[0].cqcAttackTime == 0.f && CqcEvents(world) == 0, "Soltar un intento largo de agarre ejecuta un golpe accidental");
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
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1 && world.guards[0].position == sf::Vector2f{228.f, 300.f}, "La aproximación trasera válida no conserva el lugar del enemigo");
    Require(world.agents[0].position == sf::Vector2f{200.f, 300.f} && (guardPosition - playerPosition).dot(world.agents[0].facing) > 0.f && world.guards[0].facing == world.agents[0].facing, "El agarre teletransporta al dueño o coloca al enemigo detrás del agente");
    command.push = true; simulation.Step(0.1f, {{1, command}});
    Require(world.agents[0].cqcHoldingGuard == 0 && world.guards[0].heldBy == 0, "Una acción incompatible conserva las referencias del agarre");
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
    const auto victimSnapshot = simulation.SnapshotFor(2, true);
    Require(replica.LoadSnapshot(victimSnapshot, error), "La sujeción amistosa rechaza al receptor cuando el dueño queda detrás de su campo visual");
    Require(!Visibility::IsVisible(world, world.agents[1], world.agents[0].position), "La regresión necesita un captor fuera del campo visual ordinario del aliado sujetado");
    const auto captor = std::ranges::find_if(replica.GetState().agents, [](const Agent &entry) { return entry.id == 1; });
    const auto victim = std::ranges::find_if(replica.GetState().agents, [](const Agent &entry) { return entry.id == 2; });
    Require(captor != replica.GetState().agents.end() && victim != replica.GetState().agents.end() && captor->cqcHoldingAgent == 2 && victim->cqcHeldBy == 1, "El aliado sujetado a 37 unidades no recibe al captor real con las referencias recíprocas del agarre");
    auto invalid = simulation.SnapshotFor(1, true); invalid["agents"][1]["cqcHeldBy"] = 9999;
    Require(!replica.LoadSnapshot(invalid, error), "Se acepta un compañero retenido por un dueño inexistente");
    Agent observer; observer.id = 3; observer.name = "Observador"; observer.position = {200.f, 360.f}; observer.facing = {0.f, -1.f};
    world.agents.push_back(observer);
    world.obstacles.push_back({{{197.f, 320.f}, {7.f, 10.f}}, CoverKind::High, true, "Muro que oculta únicamente al dueño"});
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
    auto held = Held(); held.aimX = 1.f; Advance(simulation, held, 5);
    Require(world.agents[0].cqcHoldingGuard == 0, "Se agarra a un enemigo fuera de orientación");
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
  void PhysicalRearAlignment() {
    Simulation simulation, replica; auto &world = Fixture(simulation);
    world.phase = GamePhase::Base; world.mode = SimulationMode::Training; world.guards[0].trainingDummy = true; world.guards[0].facing = {-1.f, 0.f};
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
    Require(approached && world.guards[0].heldBy == 1 && world.agents[0].cqcHoldingGuard == 9, "Mantener F no alcanza físicamente la espalda de un objetivo con otra orientación");
    const auto rearOffset = targetPosition - world.agents[0].position;
    Require(sf::Vector2f{-1.f, 0.f}.dot(rearOffset / rearOffset.length()) >= CqcConfig::RearCosine, "El agarre se completa antes de llegar al lado trasero real");
    Fixture(simulation); world.guards[0].state = GuardState::Alert; world.guards[0].suspicion = 1.f;
    world.agents[0].cqcPressPending = true; world.agents[0].cqcInputDown = true; world.agents[0].cqcHoldTime = CqcConfig::AcquireHold;
    simulation.Step(0.1f, {{1, Held()}});
    Require(world.guards[0].heldBy == 1, "Se rechaza un agarre trasero físicamente válido solo por el estado de alerta previo");
    Fixture(simulation); world.phase = GamePhase::Base; world.mode = SimulationMode::Training; world.guards[0].trainingDummy = true; world.guards[0].facing = {-1.f, 0.f};
    world.obstacles.push_back({{{190.f, 315.f}, {80.f, 25.f}}, CoverKind::High, true, "Pared que cierra la aproximación"});
    Advance(simulation, Held(), 12);
    Require(world.agents[0].cqcHoldingGuard == 0 && !WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius) && (world.agents[0].position - world.guards[0].position).length() >= CqcConfig::MinimumGripDistance, "Una aproximación bloqueada atraviesa pared o cuerpo para imponer el agarre");
  }
  void StableGripAndExtendedApproach() {
    Simulation simulation; auto &world = Fixture(simulation);
    Advance(simulation, Held(), 3); world.agents[0].stamina = 0.f;
    Advance(simulation, {}, 500);
    Require(world.agents[0].cqcHoldingGuard == 9 && world.guards[0].heldBy == 1 && world.guards[0].state != GuardState::Incapacitated,
      "La sujeción válida termina por tiempo ordinario, animación, IA o resistencia agotada sin presión de F");
    world.agents[0].stamina = GameplayConfig::MaxStamina;
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
}
int main() {
  try { AcquisitionAndSubdue(); RejectionAndCancellation(); PushAndCollisions(); PushInterruptsPuzzle(); PunchKickAndRecovery(); ImpactValidationAndRearApproach(); FriendlyGripAndCleanup(); TrainingCqcSafety(); ContextualWallAndHeldPriority(); PhysicalRearAlignment(); StableGripAndExtendedApproach(); CollapseCancelsGripWithoutRecovery(); std::cout << "PASS CQC y empujones: " << checks << " comprobaciones\n"; return 0; }
  catch (const std::exception &error) { std::cerr << "FAIL CQC tras " << checks << ": " << error.what() << '\n'; return 1; }
}
