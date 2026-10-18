#include "NoTraceSimulation.h"
#include "Config/NoTraceDevicesConfig.h"
#include "Config/NoTraceSecurityConfig.h"
#include "NoTracePerception.h"
#include "NoTraceWorldGeometry.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }
  WorldState &Fixture(Simulation &simulation, bool cooperative = false) {
    simulation.ResetBase(cooperative ? std::vector<PlayerProfile>{{1, "Uno"}, {2, "Dos"}} : std::vector<PlayerProfile>{{1, "Uno"}});
    simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[0].facing = {1.f, 0.f};
    if (cooperative) { world.agents[1].position = {50.f, 700.f}; world.agents[1].hidden = true; }
    world.securityEntries = {{{982.f, 110.f}, {910.f, 110.f}}, {{982.f, 690.f}, {920.f, 690.f}}, {{315.f, 18.f}, {315.f, 95.f}}};
    return world;
  }
  Guard &Observer(WorldState &world, std::uint32_t id = 1, float suspicion = 0.f) {
    Guard guard; guard.id = id; guard.position = {300.f, 300.f}; guard.facing = {-1.f, 0.f};
    guard.patrol = {{300.f, 300.f}, {320.f, 300.f}}; guard.suspicion = suspicion; guard.attackCooldown = GameplayConfig::GuardAttackInterval;
    world.guards.push_back(guard); return world.guards.back();
  }
  void Advance(Simulation &simulation, float seconds) {
    for (int step = 0; step < static_cast<int>(std::ceil(seconds / 0.1f)); ++step) {
      auto &world = const_cast<WorldState &>(simulation.GetState());
      for (auto &guard : world.guards) guard.attackCooldown = GameplayConfig::GuardAttackInterval;
      simulation.Step(0.1f, {});
    }
  }
  std::size_t Reinforcements(const WorldState &world) {
    return static_cast<std::size_t>(std::ranges::count_if(world.guards, [](const Guard &guard) { return guard.reinforcement && guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated; }));
  }

  void IncapacitationEndsThreat() {
    Simulation simulation;
    auto &solo = Fixture(simulation); Observer(solo, 1, 1.f); solo.guards[0].state = GuardState::Alert;
    solo.agents[0].lifeState = LifeState::Incapacitated; solo.agents[0].incapacitated = true; solo.agents[0].incapacityRemaining = 0.01f;
    const auto position = solo.agents[0].position;
    ActionCommand forbidden; forbidden.moveX = 1.f; forbidden.interact = true; forbidden.fireLethal = true;
    simulation.Step(0.1f, {{1, forbidden}});
    Require(solo.phase == GamePhase::Results && !solo.results.at(1).success, "La operación individual termina al quedar incapacitado el agente");
    Require(solo.agents[0].incapacitated && solo.agents[0].position == position && solo.shots.empty(), "La derrota precede a una recuperación automática o acción incompatible");
    Require(solo.results.at(1).reason.find("operativos") != std::string::npos, "La derrota debe identificar la falta de agentes operativos");

    auto &coop = Fixture(simulation, true); Observer(coop, 1, 1.f); coop.guards[0].state = GuardState::Alert;
    coop.agents[0].lifeState = LifeState::Incapacitated; coop.agents[0].incapacitated = true; coop.agents[0].incapacityRemaining = GameplayConfig::IncapacityDuration;
    Advance(simulation, 2.f);
    Require(coop.phase == GamePhase::Mission && coop.agents[0].incapacitated, "Un compañero operativo mantiene la operación cooperativa");
    Require(coop.shots.empty() && coop.guards[0].ammo == GameplayConfig::GuardAmmo && coop.guards[0].state != GuardState::Alert, "Los guardias dejan de disparar al agente inconsciente y buscan o patrullan");
    coop.agents[1].lifeState = LifeState::Incapacitated; coop.agents[1].incapacitated = true; coop.agents[1].incapacityRemaining = GameplayConfig::IncapacityDuration;
    simulation.Step(0.1f, {});
    Require(coop.phase == GamePhase::Results && coop.results.size() == 2 && !coop.results.at(1).success && !coop.results.at(2).success, "La incapacitación de todo el equipo comunica la misma derrota a ambos participantes");
  }

  void DetectionUsesRealObservers() {
    Simulation simulation;
    auto &world = Fixture(simulation); Observer(world, 1, 0.2f); Observer(world, 2, 0.4f);
    world.guards[1].position.y += 20.f;
    simulation.Step(0.1f, {});
    const float expected = 0.4f + 0.1f * Perception::DetectionRate(world.agents[0], (world.guards[1].position - world.agents[0].position).length(), GameplayConfig::VisionDistance) / GameplayConfig::ConfirmationTime;
    Require(std::abs(world.agents[0].detectionProgress - expected) < 0.0001f && !world.agents[0].detectionConfirmed, "El progreso usa el máximo real de observadores sin sumar detecciones duplicadas");
    Interactable camera{90, ObjectKind::Camera, {300.f, 280.f}, "Cámara"}; camera.facing = {-1.f, 0.f}; camera.progress = camera.camera.confirmationTime - 0.2f; camera.observedAgent = 1;
    world.objects.push_back(camera);
    simulation.Step(0.1f, {});
    const float cameraProgress = (camera.camera.confirmationTime - 0.2f + 0.1f * Perception::DetectionRate(world.agents[0], (camera.position - world.agents[0].position).length(), camera.camera.range)) / camera.camera.confirmationTime;
    Require(std::abs(world.agents[0].detectionProgress - cameraProgress) < 0.0001f && !world.agents[0].detectionConfirmed && !world.alarm, "Las cámaras aportan progreso gradual autoritativo sin confirmar antes de su umbral propio");
    Advance(simulation, 0.3f);
    Require(world.agents[0].detectionProgress == 1.f && world.agents[0].detectionConfirmed && world.alarm && world.securityActive, "La confirmación del observador real activa alarma y respuesta de seguridad");
    const auto confirmed = std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection && event.target == 1090; });
    Require(confirmed == 1, "La cámara registra exactamente una confirmación al alcanzar el umbral");
    Advance(simulation, 0.3f);
    Require(std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::ConfirmedDetection && event.target == 1090; }) == confirmed, "Una cámara ya confirmada no repite el evento de detección cada tick");
    const float guardRate = Perception::DetectionRate(world.agents[0], (world.guards[0].position - world.agents[0].position).length(), GameplayConfig::VisionDistance);
    const float guardConfirmationRemaining = (1.f - world.guards[0].suspicion) * GameplayConfig::ConfirmationTime / guardRate;
    Advance(simulation, guardConfirmationRemaining + GameplayConfig::MaximumStep);
    Require(world.guards[0].suspicion == 1.f && world.guards[0].state == GuardState::Alert, "La prueba de pérdida de contacto requiere un guardia que haya confirmado realmente al agente");
    const auto clue = world.guards[0].lastKnown;
    world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f};
    simulation.Step(0.1f, {});
    Require(world.agents[0].detectionProgress == 1.f && world.agents[0].detectionConfirmed && world.objects[0].camera.indicatorProgress == 1.f, "La confirmación observada persiste brevemente tras perder la línea de visión");
    Require(world.guards[0].lastKnown == clue && world.guards[0].state == GuardState::Search, "La búsqueda conserva únicamente la posición observada antes de ocultarse el agente");
    const float indicatorDuration = std::max(GameplayConfig::ConfirmedIndicatorDuration, DevicesConfig::CameraConfirmedIndicatorDuration);
    Advance(simulation, indicatorDuration + 0.2f);
    Require(world.agents[0].detectionProgress == 0.f && !world.agents[0].detectionConfirmed && world.objects[0].camera.indicatorProgress == 0.f, "Los indicadores confirmados caducan si ningún observador vuelve a ver al agente");
    Require(world.alarm && world.securityActive, "Caducar el indicador visual no cancela antes de tiempo la respuesta de seguridad");
    Advance(simulation, SecurityConfig::PatrolRecoveryDelay + 0.2f);
    Require(!world.alarm && !world.securityActive, "La alarma termina al cumplirse el periodo sin nuevos contactos observados");
  }

  void EnemyDamageProducesCollapse() {
    Simulation simulation, replica;
    auto &world = Fixture(simulation, true); Observer(world, 1, 1.f);
    world.guards[0].attackCooldown = 0.f; world.guards[0].state = GuardState::Alert;
    world.agents[0].health = GameplayConfig::GuardShotDamage;
    ActionCommand aim; aim.aiming = true;
    simulation.Step(0.1f, {{1, aim}});
    Require(world.agents[0].health == 0.f && world.agents[0].alive && world.agents[0].incapacitated, "El disparo enemigo que agota la salud produce un desmayo real sin una eliminación definitiva");
    Require(world.agents[0].actionPose == ActionPose::Fall && world.agents[0].actionPoseTime > 0.f, "El desmayo omite el inicio de su transición de caída");
    Require(world.phase == GamePhase::Mission && world.agents[0].detectionProgress == 0.f, "El compañero operativo mantiene la misión y el agente caído deja de mostrar progreso de detección");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "El cliente acepta el estado autoritativo de desmayo con salud cero");
    Require(replica.GetState().agents[0].health == 0.f && replica.GetState().agents[0].alive && replica.GetState().agents[0].incapacitated, "La réplica conserva salud y desmayo sin convertirlo en muerte");
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "La sincronización filtrada también admite el desmayo con salud cero");
    const auto shots = std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::GuardShot; });
    const auto ammo = world.guards[0].ammo;
    Advance(simulation, 2.5f);
    Require(world.agents[0].actionPoseTime == 0.f, "La caída queda congelada durante la inconsciencia cooperativa");
    Require(world.guards[0].ammo == ammo && std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::GuardShot; }) == shots, "El guardia no consume más balas ni sigue disparando al agente desmayado");
    Require(world.agents[0].incapacitated && !world.agents[0].aiming && !world.agents[0].detectionConfirmed && world.agents[0].detectionProgress == 0.f && world.agents[0].confirmedIndicatorRemaining == 0.f && world.agents[0].detectionIndicatorRemaining == 0.f, "Los ticks cooperativos reactivan el apuntado o la confirmación visual del agente inconsciente");

    auto &solo = Fixture(simulation); Observer(solo, 1, 1.f);
    solo.agents[0].health = GameplayConfig::GuardShotDamage; solo.guards[0].attackCooldown = 0.f; solo.guards[0].state = GuardState::Alert;
    simulation.Step(0.1f, {});
    Require(solo.phase == GamePhase::Results && solo.agents[0].lifeState == LifeState::Dead && !solo.agents[0].alive && solo.agents[0].health == 0.f && !solo.results.at(1).success, "La salud cero del agente individual confirma muerte y derrota sin abrir un rescate imposible");
  }

  void WavesAreFiniteAndSpatiallyValid() {
    Simulation simulation;
    auto &world = Fixture(simulation); Observer(world, 1, 1.f);
    Advance(simulation, SecurityConfig::InitialArrivalDelay + 0.5f);
    Require(world.reinforcementWaves == 1 && Reinforcements(world) > 0, "Una alarma confirmada solicita una primera oleada tras el intervalo configurado");
    for (const auto &guard : world.guards) if (guard.reinforcement) {
      Require(!WorldGeometry::PositionBlocked(world, guard.position, GameplayConfig::AgentRadius), "Los refuerzos llegan en posiciones libres de sólidos");
      Require(std::ranges::any_of(world.securityEntries, [&guard](const SecurityEntry &entry) { return entry.position == guard.reinforcementExit; }), "Cada refuerzo conserva una entrada de escenario válida como salida");
      Require((guard.position - world.agents[0].position).length() >= SecurityConfig::MinimumSpawnAgentDistance - GameplayConfig::GuardSpeed, "Los refuerzos no aparecen encima del agente");
    }
    Advance(simulation, SecurityConfig::ReinforcementInterval * static_cast<float>(SecurityConfig::MaximumWaves) + 2.f);
    Require(world.reinforcementWaves == SecurityConfig::MaximumWaves, "Las alarmas sostenidas respetan el límite total de oleadas por misión");
    Require(Reinforcements(world) <= SecurityConfig::MaximumActiveReinforcements, "La concurrencia de refuerzos permanece acotada");
    const auto waves = world.reinforcementWaves;
    Advance(simulation, SecurityConfig::ReinforcementInterval * 2.f);
    Require(world.reinforcementWaves == waves, "Mantener el contacto no produce enemigos infinitos");

    auto &blocked = Fixture(simulation); Observer(blocked, 1, 1.f);
    blocked.securityEntries = {{{900.f, 100.f}, {850.f, 100.f}}};
    blocked.obstacles.push_back({{{875.f, 75.f}, {50.f, 50.f}}, CoverKind::High, true, "Acceso bloqueado"});
    Advance(simulation, SecurityConfig::InitialArrivalDelay + 2.f);
    Require(blocked.securityActive && blocked.reinforcementWaves == 0 && Reinforcements(blocked) == 0, "Una entrada bloqueada aplaza la llegada sin consumir oleadas ni insertar guardias en paredes");
    blocked.obstacles.clear();
    Advance(simulation, SecurityConfig::EntryRetryDelay + 0.2f);
    Require(blocked.reinforcementWaves == 1 && Reinforcements(blocked) == 1, "El anfitrión reintenta el acceso cuando vuelve a quedar libre");

    auto &visible = Fixture(simulation); Observer(visible, 1, 1.f);
    visible.securityEntries = {{{390.f, 300.f}, {420.f, 300.f}}};
    Advance(simulation, SecurityConfig::InitialArrivalDelay + 1.f);
    Require(visible.reinforcementWaves == 0 && Reinforcements(visible) == 0, "Un acceso observado por el agente no materializa enemigos a la vista aunque esté a distancia suficiente");
    visible.agents[0].hidden = true; visible.agents[0].facing = {-1.f, 0.f};
    Advance(simulation, SecurityConfig::EntryRetryDelay + 0.2f);
    Require(visible.reinforcementWaves == 1, "La oleada aplazada puede entrar cuando el acceso deja de estar observado");

    auto &full = Fixture(simulation); Observer(full, 1, 1.f);
    for (std::size_t index = 0; index < SecurityConfig::MaximumActiveReinforcements; ++index) {
      Guard guard; guard.id = static_cast<std::uint32_t>(index + 2); guard.position = {900.f, 100.f + static_cast<float>(index) * 75.f};
      guard.reinforcement = true; guard.reinforcementExit = {982.f, 110.f}; guard.attackCooldown = 10000.f;
      full.guards.push_back(guard);
    }
    Advance(simulation, SecurityConfig::InitialArrivalDelay + 0.2f);
    Require(full.reinforcementWaves == 0 && Reinforcements(full) == SecurityConfig::MaximumActiveReinforcements, "La concurrencia llena aplaza una oleada sin consumirla");
    full.guards[1].state = GuardState::Incapacitated; full.guards[1].bodyHidden = true;
    Advance(simulation, SecurityConfig::EntryRetryDelay + 0.2f);
    Require(full.reinforcementWaves == 1 && Reinforcements(full) == SecurityConfig::MaximumActiveReinforcements, "Una plaza liberada admite solo el refuerzo que cabe en el límite activo");
  }

  void ContactLossPatrolAndWithdrawal() {
    Simulation simulation;
    auto &world = Fixture(simulation); Observer(world, 1, 1.f);
    Advance(simulation, SecurityConfig::InitialArrivalDelay + 0.5f);
    Require(Reinforcements(world) > 0, "La retirada necesita refuerzos que hayan entrado realmente");
    world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f};
    Advance(simulation, SecurityConfig::PatrolRecoveryDelay + 0.2f);
    Require(!world.securityActive && !world.alarm, "El tiempo sin nueva observación recupera la vigilancia y apaga la alarma activa");
    const auto waves = world.reinforcementWaves;
    Require(std::ranges::all_of(world.guards, [](const Guard &guard) { return guard.state == GuardState::Patrol; }), "Los supervivientes vuelven a rutas de patrulla válidas");
    Advance(simulation, SecurityConfig::WithdrawalDelay + 0.2f);
    Require(world.reinforcementWaves == waves && std::ranges::any_of(world.guards, [](const Guard &guard) { return guard.reinforcement && guard.state == GuardState::Retiring; }), "Tras un tiempo adicional los refuerzos caminan hacia sus salidas sin nuevas oleadas");
    const auto remaining = Reinforcements(world);
    Advance(simulation, 25.f);
    Require(Reinforcements(world) < remaining && world.reinforcementWaves == waves, "Los refuerzos abandonan el área al llegar a una salida y los originales permanecen");
    Require(std::ranges::any_of(world.guards, [](const Guard &guard) { return !guard.reinforcement; }), "La recuperación conserva a los guardias originales del escenario");
  }

  void VisibleExitDoesNotEraseGuard() {
    Simulation simulation;
    auto &world = Fixture(simulation); Observer(world, 1, 1.f);
    Advance(simulation, SecurityConfig::InitialArrivalDelay + 0.2f);
    world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f};
    Advance(simulation, SecurityConfig::PatrolRecoveryDelay + SecurityConfig::WithdrawalDelay + 0.1f);
    auto departing = std::ranges::find_if(world.guards, [](const Guard &guard) { return guard.reinforcement; });
    Require(departing != world.guards.end(), "El escenario conserva un refuerzo antes de alcanzar la salida");
    const auto id = departing->id;
    for (auto &guard : world.guards) if (guard.id != id) { guard.state = GuardState::Incapacitated; guard.bodyHidden = true; guard.incapacityRemaining = 0.f; }
    departing->state = GuardState::Retiring; departing->position = departing->reinforcementExit; departing->facing = {1.f, 0.f};
    const Guard returning = *departing;
    world.agents[0].hidden = false; world.agents[0].position = departing->position - sf::Vector2f{80.f, 0.f}; world.agents[0].facing = {1.f, 0.f};
    simulation.Step(0.1f, {});
    Require(std::ranges::any_of(world.guards, [id](const Guard &guard) { return guard.id == id; }), "Un refuerzo visible permanece representado incluso al llegar al punto de salida");
    world.agents[0].hidden = true; world.agents[0].facing = {-1.f, 0.f};
    simulation.Step(0.1f, {});
    Require(std::ranges::none_of(world.guards, [id](const Guard &guard) { return guard.id == id; }), "La salida completa la retirada cuando el guardia ha dejado de estar a la vista");
    world.guards.push_back(returning); world.guards.back().facing = {-1.f, 0.f};
    world.agents[0].hidden = false; world.agents[0].facing = {1.f, 0.f};
    Advance(simulation, GameplayConfig::ConfirmationTime * 2.f);
    Require(world.securityActive && world.alarm && world.agents[0].detectionConfirmed && std::ranges::any_of(world.guards, [id](const Guard &guard) { return guard.id == id && guard.state == GuardState::Alert; }), "Un refuerzo en retirada conserva la percepción gradual y reanuda la respuesta si confirma un nuevo contacto");
  }

  void PatrolPausesAndInterruptions() {
    Simulation simulation, replica;
    auto &world = Fixture(simulation); world.securityEntries.clear(); world.agents[0].hidden = true;
    auto &guard = Observer(world); guard.patrol = {{300.f, 300.f}, {390.f, 300.f}};
    simulation.Step(0.1f, {});
    Require(guard.patrolPauseRemaining >= SecurityConfig::PatrolPauseMinimum && guard.patrolPauseRemaining <= SecurityConfig::PatrolPauseMaximum && guard.waypoint == 1,
      "Llegar al punto de patrulla no elige una única pausa entre diez y veinte segundos");
    const auto position = guard.position, facing = guard.facing;
    const float pause = guard.patrolPauseRemaining;
    Advance(simulation, 2.f);
    Require(guard.position == position && guard.facing == facing && std::abs(guard.patrolPauseRemaining - (pause - 2.f)) < 0.001f && guard.stepTime == 0.f,
      "La espera mueve al guardia, cambia la dirección de vigilancia o vuelve a sortear su duración");
    std::string error;
    const bool loaded = replica.LoadSnapshot(simulation.SnapshotFor(1), error);
    Require(loaded, (std::string("La réplica de pausa de patrulla rechaza el estado: ") + error).c_str());
    Require(replica.GetState().guards[0].patrolPauseRemaining == guard.patrolPauseRemaining,
      "El temporizador de espera no conserva la duración autoritativa en LAN");
    world.noises.push_back({position + sf::Vector2f{60.f, 0.f}, 100.f, 0.6f, 0, 0, SoundKind::Decoy, 0.6f});
    simulation.Step(0.1f, {});
    Require(guard.patrolPauseRemaining == 0.f && guard.state == GuardState::Investigate && guard.position != position,
      "Un ruido audible no interrumpe la pausa de vigilancia");
    world.noises.clear();
    Advance(simulation, GameplayConfig::InvestigationDuration + 3.f);
    Require(guard.state == GuardState::Patrol && guard.patrolPauseRemaining > 0.f, "Tras investigar no se recupera la ruta con nuevas pausas normales");
    world.alarm = true;
    simulation.Step(0.1f, {});
    Require(guard.patrolPauseRemaining == 0.f && guard.state == GuardState::Search, "La alarma no interrumpe una patrulla detenida");
  }

  void DetectionPaceAndOcclusionMemory() {
    Simulation simulation;
    auto &world = Fixture(simulation); world.securityEntries.clear(); Observer(world);
    Interactable camera{90, ObjectKind::Camera, {300.f, 300.f}, "Cámara de percepción"}; camera.facing = {-1.f, 0.f};
    camera.solid = SpatialOverride::Disabled; camera.blocksVision = SpatialOverride::Disabled; world.objects.push_back(camera);
    Advance(simulation, 0.5f);
    const float guardProgress = world.guards[0].suspicion, cameraProgress = world.objects[0].progress;
    Require(guardProgress > 0.f && guardProgress < 0.5f && cameraProgress < camera.camera.confirmationTime && !world.alarm,
      "Una observación breve todavía confirma demasiado deprisa al agente quieto");
    Require(cameraProgress / camera.camera.confirmationTime > guardProgress, "La cámara deja de confirmar más rápido que el guardia equivalente");
    const auto knownGuard = world.guards[0].lastKnown, knownCamera = world.objects[0].camera.lastKnown;
    world.obstacles.push_back({{{240.f, 250.f}, {20.f, 100.f}}, CoverKind::High, true, "Oclusión breve"});
    Advance(simulation, 0.3f);
    Require(world.guards[0].suspicion == guardProgress && world.objects[0].progress == cameraProgress && !world.alarm,
      "Una oclusión breve borra inmediatamente el progreso acumulado");
    Require(world.guards[0].lastKnown == knownGuard && world.objects[0].camera.lastKnown == knownCamera, "La oclusión revela una nueva posición sin observación real");
    Advance(simulation, 1.f);
    Require(world.guards[0].suspicion < guardProgress && world.objects[0].progress < cameraProgress, "La sospecha permanece indefinidamente tras perder al agente");
    world.obstacles.clear(); simulation.Step(0.1f, {});
    Require(world.guards[0].perceptionLostTime == 0.f && world.objects[0].camera.lostSightTime == 0.f && world.guards[0].suspicion > 0.f,
      "Recuperar la visión no reanuda los temporizadores reales de detección");
  }
}

int main() {
  try {
    IncapacitationEndsThreat(); DetectionUsesRealObservers(); EnemyDamageProducesCollapse(); WavesAreFiniteAndSpatiallyValid(); ContactLossPatrolAndWithdrawal(); VisibleExitDoesNotEraseGuard(); PatrolPausesAndInterruptions(); DetectionPaceAndOcclusionMemory();
    std::cout << "NoTraceSecurityTests: " << checks << " comprobaciones correctas\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NoTraceSecurityTests: " << error.what() << " (" << checks << ")\n";
    return 1;
  }
}
