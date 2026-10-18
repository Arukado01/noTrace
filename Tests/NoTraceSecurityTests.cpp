#include "NoTraceSimulation.h"
#include "Config/NoTraceDevicesConfig.h"
#include "Config/NoTraceSecurityConfig.h"
#include "NoTracePerception.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceSpatialFixture.h"
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
    TestFixture::LegacyPlane(world);
    for (auto &agent : world.agents) agent.privateOrder = PrivateOrder::CopyArchive;
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

  void AlertKeepsEveryConfirmedObserver() {
    for (const std::size_t occluded : {std::size_t{0}, std::size_t{1}}) {
      Simulation simulation;
      auto &world = Fixture(simulation); world.securityEntries.clear();
      world.agents[0].position = {400.f, 400.f};
      Observer(world, 11, 1.f); Observer(world, 12, 1.f);
      const std::size_t remaining = 1 - occluded;
      world.guards[occluded].position = {500.f, 400.f}; world.guards[occluded].facing = {-1.f, 0.f};
      world.guards[remaining].position = {400.f, 500.f}; world.guards[remaining].facing = {0.f, -1.f};
      simulation.Step(0.1f, {});
      Require(world.guards[0].state == GuardState::Alert && world.guards[1].state == GuardState::Alert && world.securityAlert == SecurityAlert::Red,
        "La prueba multiguardia requiere dos observadores que hayan confirmado realmente al agente");
      const auto observedPosition = world.agents[0].position;
      world.obstacles.push_back({{{445.f, 300.f}, {10.f, 200.f}}, CoverKind::High, true, "Pared del primer observador"});
      for (unsigned int step = 0; step < 4; ++step) {
        Require(!simulation.CanSee(world.guards[occluded].position, world.guards[occluded].facing, world.agents[0], GameplayConfig::VisionDistance * GameplayConfig::AlarmVisionMultiplier) &&
          simulation.CanSee(world.guards[remaining].position, world.guards[remaining].facing, world.agents[0], GameplayConfig::VisionDistance * GameplayConfig::AlarmVisionMultiplier),
          "La pared debe ocultar al jugador sólo para uno de los observadores");
        simulation.Step(0.1f, {});
        Require(world.guards[occluded].state == GuardState::Search && world.guards[remaining].state == GuardState::Alert &&
          world.securityAlert == SecurityAlert::Red && world.securityRemaining == 0.f && world.alarm && world.agents[0].detectionConfirmed,
          "Perder un observador cancela la roja aunque otro todavía conserve contacto confirmado");
      }
      const auto lastKnown = world.guards[remaining].lastKnown;
      world.obstacles.push_back({{{300.f, 425.f}, {130.f, 10.f}}, CoverKind::High, true, "Pared del último observador"});
      for (const auto &guard : world.guards) Require(!WorldGeometry::PositionBlocked(world, guard.position, GameplayConfig::AgentRadius) &&
        !simulation.CanSee(guard.position, guard.facing, world.agents[0], GameplayConfig::VisionDistance * GameplayConfig::AlarmVisionMultiplier),
        "La segunda pared debe cortar el último contacto sin encerrar un guardia dentro de un sólido");
      simulation.Step(0.1f, {});
      Require(world.agents[0].position == observedPosition && world.securityAlert == SecurityAlert::Orange &&
        std::abs(world.securityRemaining - SecurityConfig::SearchDuration) < 0.001f && world.guards[remaining].state == GuardState::Search &&
        world.guards[0].lastKnown == lastKnown && world.guards[1].lastKnown == lastKnown,
        "Sólo perder todos los contactos inicia treinta segundos completos de naranja y conserva la última pista observada");
    }
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
    Require(!world.securityActive && !world.alarm && world.securityAlert == SecurityAlert::Yellow, "Tras treinta segundos de búsqueda comienza la vigilancia amarilla");
    const auto waves = world.reinforcementWaves;
    Require(std::ranges::all_of(world.guards, [](const Guard &guard) { return guard.reinforcement ? guard.state == GuardState::Retiring : guard.state == GuardState::Vigilance; }), "La amarilla mantiene a los guardias vigilando y ordena retirarse inmediatamente a los refuerzos");
    Require(std::ranges::any_of(world.guards, [](const Guard &guard) { return guard.reinforcement && guard.state == GuardState::Retiring; }), "Los refuerzos comienzan a caminar a su salida al entrar en amarillo");
    Advance(simulation, SecurityConfig::WithdrawalDelay + 0.2f);
    Require(world.securityAlert == SecurityAlert::Calm && world.reinforcementWaves == waves && std::ranges::all_of(world.guards, [](const Guard &guard) { return guard.reinforcement ? guard.state == GuardState::Retiring : guard.state == GuardState::Patrol; }), "Tras doce segundos de vigilancia vuelven a patrulla los originales sin interrumpir la retirada");
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
    Advance(simulation, SecurityConfig::PatrolRecoveryDelay + 0.1f);
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

  void BodyInvestigationIsPhysicalAndAnonymous() {
    for (unsigned int variant = 0; variant < 4; ++variant) {
      Simulation simulation;
      auto &world = Fixture(simulation, variant >= 2); world.securityEntries.clear();
      world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f};
      Observer(world, 11); Observer(world, 12);
      world.guards[0].position = {100.f, 300.f}; world.guards[0].facing = {1.f, 0.f}; world.guards[0].patrol = {{100.f, 300.f}};
      world.guards[1].position = {120.f, 350.f}; world.guards[1].facing = {1.f, 0.f}; world.guards[1].patrol = {{120.f, 350.f}};
      const sf::Vector2f bodyPosition{220.f, 300.f};
      if (variant < 2) {
        Guard body; body.id = 91; body.position = bodyPosition; body.state = variant == 0 ? GuardState::Dead : GuardState::Incapacitated;
        body.health = variant == 0 ? 0.f : CombatConfig::MaxGuardHealth; body.incapacityRemaining = 0.f; world.guards.push_back(body);
      } else {
        auto &body = world.agents[1]; body.position = bodyPosition; body.hidden = false;
        body.alive = variant == 3; body.incapacitated = variant == 3; body.lifeState = variant == 3 ? LifeState::Incapacitated : LifeState::Dead;
        body.health = 0.f; body.incapacityRemaining = GameplayConfig::IncapacityDuration;
      }
      world.obstacles.push_back({{{148.f, 280.f}, {30.f, 40.f}}, CoverKind::Low, true, "Banco entre patrulla y cuerpo"});
      world.obstacles.back().height = 16.f;
      simulation.Step(0.1f, {});
      Require(world.guards[0].state == GuardState::ApproachBody && world.guards[0].bodyTargetId != 0 && !world.alarm,
        "El hallazgo visual reserva un único investigador y comienza un acercamiento sin alarma roja");
      Require(std::ranges::count_if(world.guards, [](const Guard &guard) { return guard.bodyTargetKind != BodyTargetKind::None; }) == 1,
        "Dos patrullas no reservan simultáneamente el mismo cuerpo");
      Require(world.guards[0].position != sf::Vector2f{100.f, 300.f} && (world.guards[0].position - sf::Vector2f{100.f, 300.f}).length() <= GameplayConfig::GuardSpeed * 0.1f + 0.01f,
        "El investigador avanza físicamente sin teletransportarse");
      bool detour = false, inspected = false;
      float inspectionStart = 0.f;
      for (unsigned int step = 0; step < 120; ++step) {
        const auto before = world.guards[0].position;
        simulation.Step(0.1f, {});
        const auto &guard = world.guards[0];
        Require(!WorldGeometry::PositionBlocked(world, guard.position, GameplayConfig::AgentRadius) && (guard.position - before).length() <= GameplayConfig::GuardSpeed * 0.1f + 0.01f,
          "La navegación hacia el cuerpo atraviesa un sólido o produce un salto");
        detour |= std::abs(guard.position.y - 300.f) > 10.f;
        if (guard.state == GuardState::InspectBody) {
          if (!inspected) inspectionStart = world.elapsed - 0.1f;
          inspected = true;
          Require((guard.position - bodyPosition).length() <= SecurityConfig::BodyInspectionReach && guard.stepTime == 0.f && guard.bodyInspectionTime > 0.f,
            "La inspección exige detenerse junto al cuerpo y conservar progreso real");
          Require(world.securityAlert != SecurityAlert::Red, "Inspeccionar un cuerpo no equivale a observar al agente");
        }
        if (world.securityAlert == SecurityAlert::Orange) break;
      }
      Require(detour && inspected && world.securityAlert == SecurityAlert::Orange && std::abs(world.securityRemaining - 30.f) < 0.001f,
        "El investigador debe rodear la geometría, detenerse a inspeccionar y comunicar una naranja completa de treinta segundos");
      Require(world.elapsed - inspectionStart >= SecurityConfig::BodyInspectionDuration - 0.01f,
        "El aviso del cuerpo se produce antes de completar la inspección física");
      Require(world.alarmCount == 1 && std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::BodyFound && event.actor == 0; }) == 1,
        "Cada cuerpo genera un único aviso anónimo y una alarma del incidente");
      Require(world.agents[0].detectionProgress == 0.f && !world.agents[0].detectionConfirmed && world.guards[0].observedAgent == 0 && world.guards[1].observedAgent == 0,
        "El cuerpo comunica una ubicación sin revelar identidad ni posición del jugador");
      simulation.Step(0.1f, {});
      Require(world.guards[0].lastKnown == bodyPosition && world.guards[1].lastKnown == bodyPosition && world.guards[0].navigationTarget != world.guards[1].navigationTarget,
        "La central comparte el cuerpo como pista y distribuye las rutas alrededor de la zona");
      world.securityEntries = {{{982.f, 110.f}, {910.f, 110.f}}};
      Advance(simulation, 2.7f);
      Require(world.reinforcementWaves == 0 && world.securityRemaining < 28.f,
        "Volver a ver el mismo cuerpo reinicia la naranja o genera una oleada prematura");
      Advance(simulation, 0.5f);
      Require(world.reinforcementWaves == 1 && Reinforcements(world) == 1 && std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::BodyFound; }) == 1,
        "El aviso solicita una sola oleada programada sin duplicar el descubrimiento");
    }
  }

  void BodyOcclusionAndReservationRelease() {
    Simulation simulation;
    auto &world = Fixture(simulation); world.securityEntries.clear(); world.agents[0].hidden = true;
    Observer(world); world.guards[0].position = {100.f, 300.f}; world.guards[0].facing = {1.f, 0.f}; world.guards[0].patrol = {{100.f, 300.f}};
    Guard body; body.id = 91; body.position = {220.f, 300.f}; body.state = GuardState::Dead; body.health = 0.f; body.bodyHidden = true; world.guards.push_back(body);
    Advance(simulation, 0.5f);
    Require(world.guards[0].bodyTargetKind == BodyTargetKind::None && !world.alarm, "Un cuerpo oculto no revela su existencia a la patrulla");
    world.guards[1].bodyHidden = false;
    world.obstacles.push_back({{{150.f, 200.f}, {20.f, 200.f}}, CoverKind::High, true, "Pared de oclusión"});
    Advance(simulation, 0.5f);
    Require(world.guards[0].bodyTargetKind == BodyTargetKind::None && !world.alarm, "La visión del cuerpo debe respetar una pared opaca");
    world.obstacles.clear(); simulation.Step(0.1f, {});
    Require(world.guards[0].bodyTargetId == 91, "La patrulla no adquiere el cuerpo al recuperar una línea de visión válida");
    world.guards[1].bodyHidden = true; simulation.Step(0.1f, {});
    Require(world.guards[0].bodyTargetKind == BodyTargetKind::None && !world.alarm && !world.guards[1].bodyReported,
      "Ocultar el cuerpo antes de inspeccionarlo cancela la reserva sin inventar un aviso");
  }

  void NewBodyRefreshesSearchWithoutRepeatingTheAlertCue() {
    Simulation simulation;
    auto &world = Fixture(simulation); world.securityEntries.clear(); TestFixture::LegacyPlane(world); Observer(world, 11, 1.f);
    simulation.Step(.1f, {});
    world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f}; simulation.Step(.1f, {});
    Advance(simulation, 4.f);
    const float before = world.securityRemaining;
    auto &observer = world.guards[0]; observer.position = {200.f, 300.f}; observer.facing = {1.f, 0.f};
    observer.state = GuardState::InspectBody; observer.bodyTargetKind = BodyTargetKind::Guard; observer.bodyTargetId = 91;
    observer.bodyInspectionTime = SecurityConfig::BodyInspectionDuration - .05f;
    Guard body; body.id = 91; body.position = {220.f, 300.f}; body.state = GuardState::Dead; body.health = 0.f;
    world.guards.push_back(body); world.sounds.clear(); world.events.clear();
    simulation.Step(.1f, {});
    Require(before < SecurityConfig::SearchDuration - 3.f && world.securityAlert == SecurityAlert::Orange &&
      std::abs(world.securityRemaining - SecurityConfig::SearchDuration) < .001f && world.guards[0].lastKnown == body.position && world.guards[1].bodyReported,
      "Un cuerpo nuevo comunicado durante naranja no renueva la búsqueda completa ni actualiza la pista");
    Require(world.alarmCount == 1 && std::ranges::count(world.events, EventKind::BodyFound, &MissionEvent::kind) == 1 &&
      std::ranges::none_of(world.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::SearchStart; }),
      "Un cuerpo nuevo duplica la transición sonora o el incidente de alarma ya activo");
    Advance(simulation, 1.f);
    Require(std::abs(world.securityRemaining - (SecurityConfig::SearchDuration - 1.f)) < .002f &&
      std::ranges::count(world.events, EventKind::BodyFound, &MissionEvent::kind) == 1 && world.guards[1].state == GuardState::Dead,
      "Reobservar el cuerpo comunicado renueva el reloj, duplica el aviso o despierta un cadáver");
  }

  void GuardAssistancePreservesTheOngoingSearch() {
    Simulation simulation, replica;
    auto &world = Fixture(simulation); world.securityEntries.clear(); Observer(world, 1, 1.f);
    simulation.Step(0.1f, {});
    world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f}; simulation.Step(0.1f, {});
    Advance(simulation, 4.f);
    world.guards.clear(); world.noises.clear(); world.events.clear();
    Observer(world, 11); Observer(world, 12);
    world.guards[0].position = {100.f, 300.f}; world.guards[0].facing = {1.f, 0.f};
    world.guards[1].position = {120.f, 350.f}; world.guards[1].facing = {1.f, 0.f};
    for (auto &guard : world.guards) { guard.state = GuardState::Search; guard.lastKnown = {200.f, 300.f}; }
    Guard body; body.id = 91; body.position = {220.f, 300.f}; body.state = GuardState::Incapacitated;
    body.health = 37.f; body.stun = CombatConfig::MaxGuardStun; body.incapacityRemaining = 80.f; body.bodyReported = true;
    world.guards.push_back(body);
    world.obstacles.push_back({{{148.f, 280.f}, {30.f, 40.f}}, CoverKind::Low, true, "Banco durante la asistencia"});
    world.obstacles.back().height = 16.f;
    const auto searchingBefore = world.guards[1].position;
    const float orangeBefore = world.securityRemaining;
    bool detour = false, inspected = false, synchronized = false;
    unsigned int steps = 0;
    for (; steps < 120 && world.guards[2].state == GuardState::Incapacitated; ++steps) {
      const auto previous = world.guards[0].position;
      const float remaining = world.securityRemaining;
      simulation.Step(0.1f, {});
      Require(world.securityAlert == SecurityAlert::Orange && std::abs(world.securityRemaining - (remaining - 0.1f)) < 0.001f,
        "Acercarse, asistir o despertar a un guardia reinicia o cancela la naranja existente");
      Require((world.guards[0].position - previous).length() <= GameplayConfig::GuardSpeed * 0.1f + 0.01f &&
        !WorldGeometry::PositionBlocked(world, world.guards[0].position, GameplayConfig::AgentRadius), "La asistencia salta o atraviesa la geometría");
      detour |= std::abs(world.guards[0].position.y - 300.f) > 10.f;
      if (world.guards[2].state == GuardState::Incapacitated) Require(std::ranges::count_if(world.guards, [](const Guard &guard) {
        return guard.bodyTargetKind == BodyTargetKind::Guard && guard.bodyTargetId == 91;
      }) == 1, "Dos guardias asisten al mismo compañero o pierden una reserva válida");
      if (world.guards[0].state == GuardState::InspectBody) {
        inspected = true;
        Require(world.guards[0].position == previous && world.guards[0].stepTime == 0.f, "La asistencia no detiene al guardia durante la interacción");
        if (!synchronized) {
          std::string error;
          Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), "La instantánea completa rechaza una asistencia a un guardia ya comunicado");
          Require(replica.GetState().guards[0].bodyTargetId == 91 && replica.GetState().guards[0].bodyInspectionTime == world.guards[0].bodyInspectionTime,
            "La réplica pierde la reserva o el progreso autoritativo de asistencia");
          synchronized = true;
        }
      }
    }
    Require(detour && inspected && synchronized && steps < 120 && world.guards[2].state == GuardState::Search,
      "El investigador no rodea el banco, inspecciona y devuelve al compañero a la búsqueda");
    const auto &awake = world.guards[2];
    Require(awake.health == 37.f && awake.stun == 0.f && awake.incapacityRemaining == 0.f && awake.cqcState == CqcState::None &&
      awake.heldBy == 0 && awake.attackTime == 0.f && awake.observedAgent == 0 && awake.suspicion == 0.f &&
      (awake.position - body.position).length() <= GameplayConfig::GuardSpeed * 0.1f + 0.01f,
      "Despertar cambia vida o posición de forma incoherente, conserva aturdimiento o inventa una detección");
    Require(awake.wakeRemaining > 0.f && awake.wakeRemaining <= GameplayConfig::StanceTransitionDuration,
      "La asistencia física salta de inconsciente a búsqueda sin incorporación");
    Require(world.guards[1].state == GuardState::Search && world.guards[1].position != searchingBefore &&
      std::abs(world.securityRemaining - (orangeBefore - static_cast<float>(steps) * 0.1f)) < 0.002f && world.alarmCount == 1 && world.events.empty(),
      "La asistencia detiene la búsqueda ajena, renueva el incidente o duplica su aviso");
    const auto wakePosition = awake.position;
    std::string wakeError;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), wakeError) && replica.GetState().guards[2].wakeRemaining == awake.wakeRemaining,
      "La réplica pierde la incorporación adquirida mediante asistencia real");
    for (unsigned int frame = 0; frame < 20 && world.guards[2].wakeRemaining > 0.f; ++frame) {
      const float before = world.guards[2].wakeRemaining, orange = world.securityRemaining;
      simulation.Step(.02f, {});
      Require(std::abs(world.guards[2].wakeRemaining - std::max(0.f, before - .02f)) < .000001f &&
        world.guards[2].position == wakePosition && world.guards[2].stepTime == 0.f && world.guards[2].attackTime == 0.f && world.shots.empty(),
        "El asistido consume dos veces su incorporación, se mueve o ataca antes de completarla");
      Require(world.securityAlert == SecurityAlert::Orange && std::abs(world.securityRemaining - (orange - .02f)) < .001f && world.alarmCount == 1 && world.events.empty(),
        "La pose de incorporación del asistido reinicia la naranja de treinta segundos o duplica el incidente");
    }
    Require(world.guards[2].wakeRemaining == 0.f, "El asistido no termina de incorporarse dentro de la duración configurada");
    Advance(simulation, 0.5f);
    Require(world.guards[2].state == GuardState::Search && world.guards[2].stun == 0.f && world.guards[2].incapacityRemaining == 0.f,
      "Una transición residual vuelve a incapacitar inmediatamente al guardia asistido");
  }

  void AssistanceRespectsWallsDeathAndThreats() {
    for (const auto state : {GuardState::Dead, GuardState::Incapacitated}) {
      Simulation simulation;
      auto &world = Fixture(simulation); world.securityEntries.clear(); world.agents[0].hidden = true;
      Observer(world, 11); world.guards[0].position = {200.f, 300.f}; world.guards[0].facing = {1.f, 0.f};
      Guard body; body.id = 91; body.position = {228.f, 300.f}; body.state = state; body.health = 0.f; world.guards.push_back(body);
      Advance(simulation, 3.f);
      Require(world.guards[1].state == state && world.guards[1].health == 0.f && world.guards[1].position == body.position,
        "La asistencia resucita un cadáver o un estado inconsistente sin vida");
    }
    {
      Simulation simulation;
      auto &world = Fixture(simulation); world.securityEntries.clear(); world.agents[0].hidden = true;
      Observer(world, 11); world.guards[0].position = {200.f, 300.f}; world.guards[0].facing = {1.f, 0.f};
      Guard body; body.id = 91; body.position = {228.f, 300.f}; body.state = GuardState::Incapacitated;
      body.incapacityRemaining = 80.f; body.bodyReported = true; body.stun = CombatConfig::MaxGuardStun; world.guards.push_back(body);
      Advance(simulation, 0.5f);
      Require(world.guards[0].state == GuardState::InspectBody && world.guards[0].bodyInspectionTime >= 0.49f,
        "La prueba no alcanza una interacción real de asistencia");
      world.agents[0].hidden = false; world.agents[0].position = {210.f, 300.f}; world.guards[0].suspicion = 1.f;
      simulation.Step(0.1f, {});
      Require(world.guards[0].state == GuardState::Alert && world.guards[0].bodyTargetKind == BodyTargetKind::None &&
        world.guards[0].bodyInspectionTime == 0.f && world.guards[1].state == GuardState::Incapacitated && world.securityAlert == SecurityAlert::Red,
        "Una amenaza visual confirmada no interrumpe la asistencia con prioridad de combate");
      world.agents[0].hidden = true; simulation.Step(0.1f, {});
      Require(world.guards[0].bodyTargetId == 91 && world.guards[0].bodyInspectionTime <= 0.1001f,
        "La asistencia no puede reanudarse o conserva progreso de antes de la interrupción");
      world.guards[0].staggerRemaining = 0.6f; simulation.Step(0.1f, {});
      Require(world.guards[0].bodyTargetKind == BodyTargetKind::None && world.guards[0].bodyInspectionTime == 0.f &&
        world.guards[1].state == GuardState::Incapacitated, "Recibir una reacción de daño no libera la reserva ni interrumpe la asistencia");
      Observer(world, 12); world.guards.back().position = {228.f, 328.f}; world.guards.back().facing = {0.f, -1.f};
      simulation.Step(0.1f, {});
      Require(world.guards.back().bodyTargetId == 91 && std::ranges::count_if(world.guards, [](const Guard &guard) { return guard.bodyTargetId == 91; }) == 1,
        "Otro guardia no puede asumir de forma exclusiva una asistencia interrumpida");
    }
  }

  void UnreachableAssistanceYieldsToNormalAi() {
    for (const bool closedAccess : {true, false}) {
      Simulation simulation;
      auto &world = Fixture(simulation); world.securityEntries.clear(); Observer(world, 11, 1.f);
      simulation.Step(0.1f, {});
      world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f}; simulation.Step(0.1f, {});
      auto &helper = world.guards[0]; helper.position = {100.f, 300.f}; helper.facing = {1.f, 0.f}; helper.patrol = {{100.f, 300.f}, {100.f, 380.f}};
      Guard body; body.id = 91; body.position = {220.f, 300.f}; body.state = GuardState::Incapacitated;
      body.stun = CombatConfig::MaxGuardStun; body.incapacityRemaining = 80.f; body.bodyReported = true; world.guards.push_back(body);
      if (closedAccess) world.obstacles = {{{{200.f, 280.f}, {40.f, 4.f}}, CoverKind::Low, true, "Cierre superior"},
        {{{200.f, 316.f}, {40.f, 4.f}}, CoverKind::Low, true, "Cierre inferior"},
        {{{200.f, 280.f}, {4.f, 40.f}}, CoverKind::Low, true, "Cierre izquierdo"},
        {{{236.f, 280.f}, {4.f, 40.f}}, CoverKind::Low, true, "Cierre derecho"}};
      else world.obstacles = {{{{150.f, 0.f}, {20.f, world.size.y}}, CoverKind::Low, true, "Separación sin ruta al compañero"}};
      for (auto &obstacle : world.obstacles) obstacle.height = 8.f;
      bool attempted = false, released = false;
      for (unsigned int step = 0; step < 150 && !released; ++step) {
        const auto previous = world.guards[0].position;
        const auto remaining = world.securityRemaining;
        simulation.Step(0.1f, {});
        const auto &guard = world.guards[0];
        attempted |= guard.bodyTargetId == 91 || guard.navigationRejectTime > 0.f;
        released = attempted && guard.bodyTargetKind == BodyTargetKind::None && guard.navigationRejectTime > 0.f;
        Require(world.guards[1].state == GuardState::Incapacitated && world.guards[1].position == body.position &&
          guard.bodyInspectionTime == 0.f && (guard.position - previous).length() <= GameplayConfig::GuardSpeed * 0.1f + 0.01f &&
          !WorldGeometry::PositionBlocked(world, guard.position, GameplayConfig::AgentRadius), "La asistencia inaccesible despierta, salta o atraviesa sólidos");
        Require(world.securityAlert == SecurityAlert::Orange && std::abs(world.securityRemaining - (remaining - 0.1f)) < 0.001f,
          "Rechazar una asistencia inaccesible altera el reloj naranja");
      }
      Require(released && world.guards[0].state == GuardState::Search, "La falta de acceso o el rechazo de navegación no libera la reserva para continuar buscando");
      for (unsigned int step = 0; step < 5; ++step) {
        world.guards[0].facing = {1.f, 0.f}; simulation.Step(0.1f, {});
        Require(world.guards[0].bodyTargetKind == BodyTargetKind::None && world.guards[0].state == GuardState::Search && world.guards[0].navigationRejectTime > 0.f,
          "El guardia vuelve a reservar el mismo cuerpo cada tick en vez de respetar la espera de navegación");
      }
      Require(world.guards[0].stateTime >= 0.49f, "La asistencia rechazada reinicia la IA de búsqueda durante la espera");
      if (closedAccess) {
        Advance(simulation, world.securityRemaining + 0.1f);
        Require(world.securityAlert == SecurityAlert::Yellow && world.guards[0].state == GuardState::Vigilance && world.guards[0].bodyTargetKind == BodyTargetKind::None,
          "La asistencia inaccesible impide pasar de búsqueda a vigilancia");
        Advance(simulation, world.securityRemaining + 0.1f);
        Require(world.securityAlert == SecurityAlert::Calm && world.guards[0].state == GuardState::Patrol && world.guards[0].bodyTargetKind == BodyTargetKind::None &&
          world.guards[1].state == GuardState::Incapacitated && world.guards[1].position == body.position,
          "La asistencia inaccesible conserva una reserva o bloquea la patrulla al acabar la alerta");
      } else {
        world.obstacles.clear();
        const float retry = world.guards[0].navigationRejectTime;
        for (unsigned int step = 0; step < static_cast<unsigned int>(std::ceil((retry + 4.f) / 0.1f)) && world.guards[1].state == GuardState::Incapacitated; ++step) {
          world.guards[0].facing = {1.f, 0.f}; simulation.Step(0.1f, {});
        }
        Require(world.guards[1].state == GuardState::Search && world.guards[1].stun == 0.f && world.guards[1].incapacityRemaining == 0.f,
          "Abrir una ruta válida después de la espera no permite reanudar y completar la asistencia");
      }
    }
  }

  void AlertPhasesUseExactAuthoritativeDurations() {
    Simulation simulation, replica;
    auto &world = Fixture(simulation); world.securityEntries.clear(); Observer(world, 1, 1.f);
    simulation.Step(0.1f, {});
    Require(world.securityAlert == SecurityAlert::Red && world.securityRemaining == 0.f, "El contacto confirmado comienza una roja sin cuenta atrás artificial");
    Advance(simulation, 5.f);
    Require(world.securityAlert == SecurityAlert::Red, "La roja caduca mientras todavía existe contacto visual confirmado");
    const auto lastKnown = world.guards[0].lastKnown;
    world.agents[0].hidden = true; world.agents[0].position = {50.f, 700.f}; simulation.Step(0.1f, {});
    Require(world.securityAlert == SecurityAlert::Orange && std::abs(world.securityRemaining - 30.f) < 0.001f && world.guards[0].lastKnown == lastKnown,
      "Perder el último contacto cambia inmediatamente a naranja y conserva únicamente la pista observada");
    Advance(simulation, 29.9f);
    Require(world.securityAlert == SecurityAlert::Orange && world.securityRemaining > 0.f && world.securityRemaining < 0.11f,
      "La naranja termina antes de sus treinta segundos completos");
    simulation.Step(0.1f, {});
    Require(world.securityAlert == SecurityAlert::Yellow && std::abs(world.securityRemaining - 12.f) < 0.001f && world.guards[0].state == GuardState::Vigilance,
      "Al segundo treinta comienza una vigilancia amarilla completa de doce segundos");
    std::string error;
    Require(replica.LoadSnapshot(simulation.SnapshotFor(1), error), (std::string("La réplica rechaza el estado amarillo: ") + error).c_str());
    Require(replica.GetState().securityAlert == SecurityAlert::Yellow && replica.GetState().securityRemaining == world.securityRemaining && replica.GetState().guards[0].state == GuardState::Vigilance,
      "La réplica no conserva la fase y el plazo calculados por el anfitrión");
    Advance(simulation, 11.9f);
    Require(world.securityAlert == SecurityAlert::Yellow && world.guards[0].state == GuardState::Vigilance,
      "Los guardias regresan a patrulla antes de los doce segundos de vigilancia");
    simulation.Step(0.1f, {});
    Require(world.securityAlert == SecurityAlert::Calm && world.guards[0].state == GuardState::Patrol && !world.alarm,
      "La vigilancia no termina en patrulla al cumplir sus doce segundos");
    world.agents[0].hidden = false; world.agents[0].position = world.guards[0].position + sf::Vector2f{50.f, 0.f};
    world.guards[0].facing = {1.f, 0.f}; world.guards[0].suspicion = 1.f; simulation.Step(0.1f, {});
    Require(world.securityAlert == SecurityAlert::Red, "Un nuevo contacto confirmado reactiva inmediatamente la roja");
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
    IncapacitationEndsThreat(); DetectionUsesRealObservers(); AlertKeepsEveryConfirmedObserver(); EnemyDamageProducesCollapse(); WavesAreFiniteAndSpatiallyValid(); ContactLossPatrolAndWithdrawal(); VisibleExitDoesNotEraseGuard(); BodyInvestigationIsPhysicalAndAnonymous(); BodyOcclusionAndReservationRelease(); NewBodyRefreshesSearchWithoutRepeatingTheAlertCue(); GuardAssistancePreservesTheOngoingSearch(); AssistanceRespectsWallsDeathAndThreats(); UnreachableAssistanceYieldsToNormalAi(); AlertPhasesUseExactAuthoritativeDurations(); PatrolPausesAndInterruptions(); DetectionPaceAndOcclusionMemory();
    std::cout << "NoTraceSecurityTests: " << checks << " comprobaciones correctas\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "NoTraceSecurityTests: " << error.what() << " (" << checks << ")\n";
    return 1;
  }
}
