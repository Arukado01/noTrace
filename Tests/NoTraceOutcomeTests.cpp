#include "NoTraceSimulation.h"
#include "NoTraceOutcomes.h"
#include "NoTraceInteraction.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceSpatialFixture.h"
#include <iostream>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  unsigned int checks = 0;
  void Require(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
  WorldState &Prepare(Simulation &simulation, unsigned int players = 3, ContractId contract = ContractId::GhostArchive) {
    std::vector<PlayerProfile> profiles;
    for (unsigned int index = 0; index < players; ++index) profiles.push_back({index + 1, "Agente"});
    simulation.ResetBase(profiles); simulation.SetContract(contract); simulation.Deploy(14);
    auto &world = const_cast<WorldState &>(simulation.GetState()); TestFixture::LegacyPlane(world);
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear();
    for (std::size_t index = 0; index < world.agents.size(); ++index) {
      world.agents[index].privateOrder = index % 2 == 0 ? PrivateOrder::CopyArchive : PrivateOrder::RecoverDevice;
      world.agents[index].position = {100.f + static_cast<float>(index) * 200.f, 100.f};
    }
    return world;
  }
  void Advance(Simulation &simulation, unsigned int frames, PlayerId player = 1, const ActionCommand &command = {}, float dt = 0.1f) {
    for (unsigned int frame = 0; frame < frames; ++frame) simulation.Step(dt, {{player, command}});
  }
  void RequirePrivateResults(const Simulation &simulation) {
    const auto &world = simulation.GetState();
    Require(world.results.size() == world.agents.size(), "La autoridad no conserva el desenlace individual de cada participante");
    for (const auto &agent : world.agents) {
      const auto snapshot = simulation.SnapshotFor(agent.id);
      Require(snapshot.at("results").size() == 1 && snapshot.at("results")[0].at("id") == agent.id, "Un destinatario recibe resultados privados ajenos");
      for (const auto &value : snapshot.at("agents")) Require(value.contains("privateOrder") == (value.at("id") == agent.id), "El desenlace publica la orden privada de otro participante");
      Simulation replica; std::string error;
      if (!replica.LoadSnapshot(snapshot, error)) throw std::runtime_error("Resultado individual: " + error);
      Require(replica.GetState().results.size() == 1 && replica.GetState().results.at(agent.id).success == world.results.at(agent.id).success &&
        replica.GetState().results.at(agent.id).traitorVictory == world.results.at(agent.id).traitorVictory, "La réplica cambia el desenlace privado del destinatario");
    }
  }
  void PrivateAssignment() {
    for (const auto contract : {ContractId::GhostArchive, ContractId::SilentRelay, ContractId::DoubleCross}) for (unsigned int count = 1; count <= GameplayConfig::MaximumPlayers; ++count) {
      Simulation simulation; std::vector<PlayerProfile> profiles;
      for (unsigned int index = 0; index < count; ++index) profiles.push_back({index + 1, "Agente"});
      simulation.ResetBase(profiles); simulation.SetContract(contract); simulation.Deploy(17);
      const auto &world = simulation.GetState();
      const auto traitors = std::ranges::count(world.agents, PrivateOrder::Traitor, &Agent::privateOrder);
      Require(traitors == (count >= 2 ? 1 : 0), "El despliegue no asigna exactamente un traidor privado en cooperativo");
      for (const auto &agent : world.agents) {
        Require(agent.privateOrder != PrivateOrder::None, "Un participante desplegado carece de orden privada");
        const auto own = simulation.SnapshotFor(agent.id, true);
        for (const auto &value : own.at("agents")) Require(value.contains("privateOrder") == (value.at("id") == agent.id), "El snapshot publica una orden de otro participante");
        Simulation client; std::string error;
        if (!client.LoadSnapshot(own, error)) throw std::runtime_error("Orden privada válida: " + error);
      }
    }
  }
  void ExpireAndSpectateWithoutControllingSurvivors() {
    Simulation simulation; auto &world = Prepare(simulation); auto &owner = world.agents[0];
    owner.health = 0.f; simulation.Step(0.1f, {});
    Require(owner.lifeState == LifeState::Incapacitated && owner.incapacityRemaining == GameplayConfig::IncapacityDuration, "La caída no conserva los55segundos completos");
    Advance(simulation, 549);
    Require(owner.alive && owner.incapacitated && owner.incapacityRemaining > 0.f, "El plazo individual vence antes de55segundos");
    Advance(simulation, 2);
    Require(owner.lifeState == LifeState::Dead && owner.rescueExpired && world.phase == GamePhase::Mission, "La muerte individual termina la misión de los supervivientes");
    world.agents[1].position = {400.f, 300.f}; world.agents[1].facing = {1.f, 0.f};
    world.agents[2].position = {750.f, 300.f}; world.agents[2].facing = {-1.f, 0.f};
    Guard near; near.id = 10; near.position = {450.f, 300.f}; near.facing = {0.f, -1.f};
    Guard hidden = near; hidden.id = 11; hidden.position = {550.f, 300.f}; world.guards = {near, hidden};
    world.obstacles.push_back({{{500.f, 250.f}, {12.f, 100.f}}, CoverKind::High, true, "Pared del observador"});
    const auto initial = owner.position; const auto survivor = world.agents[1].position; const int ammunition = owner.loadout.lethalAmmo;
    ActionCommand forbidden; forbidden.moveX = 1.f; forbidden.run = true; forbidden.fireLethal = true; forbidden.interact = true; forbidden.traverse = true;
    simulation.Step(0.1f, {{1, forbidden}});
    Require(owner.position == initial && owner.loadout.lethalAmmo == ammunition && world.agents[1].position == survivor, "Los controles del espectador mueven o arman un personaje");
    auto snapshot = simulation.SnapshotFor(1, true);
    Require(snapshot.at("player") == 1 && snapshot.at("spectatorTarget") == 2 && snapshot.at("visibility").at("observer") == 2, "El espectador cambia su identidad privada por la del seguido");
    Require(snapshot.at("guards").size() == 1 && snapshot.at("guards")[0].at("id") == 10, "La vista de espectador atraviesa la pared del seguido");
    for (const auto &value : snapshot.at("agents")) {
      Require(value.contains("privateOrder") == (value.at("id") == 1), "El espectador recibe la orden privada del seguido");
      if (value.at("id") != 1) Require(!value.contains("puzzle"), "El espectador recibe el puzle privado del seguido");
    }
    Simulation replica; std::string error;
    if (!replica.LoadSnapshot(snapshot, error)) throw std::runtime_error("Espectador válido: " + error);
    for (unsigned int mutation = 0; mutation < 3; ++mutation) {
      auto invalid = snapshot;
      if (mutation == 0) invalid["spectatorTarget"] = 3;
      if (mutation == 1) invalid["agents"][0]["alive"] = true;
      if (mutation == 2) for (auto &value : invalid["agents"]) if (value["id"] == 2) value["privateOrder"] = static_cast<int>(PrivateOrder::Traitor);
      Require(!replica.LoadSnapshot(invalid, error), "El parser acepta identidad o permisos de espectador manipulados");
    }
    ActionCommand next; next.spectateNext = true; simulation.Step(0.1f, {{1, next}});
    snapshot = simulation.SnapshotFor(1, true);
    Require(snapshot.at("spectatorTarget") == 3 && snapshot.at("visibility").at("observer") == 3, "El espectador no cambia de superviviente por comando autoritativo");
    Require(snapshot.at("guards").size() == 1 && snapshot.at("guards")[0].at("id") == 11, "Cambiar de seguido mezcla dos máscaras de visibilidad");
    if (!replica.LoadSnapshot(snapshot, error)) throw std::runtime_error("Cambio de espectador válido: " + error);
    simulation.DisconnectPlayer(3); simulation.Step(0.1f, {});
    Require(simulation.SnapshotFor(1, true).at("spectatorTarget") == 2, "Un seguido desconectado deja bloqueada la vista");
  }
  void RescueDoesNotRevealRoles() {
    for (const int role : {0, 1, 2}) {
      Simulation simulation; auto &world = Prepare(simulation, 2);
      world.agents[0].position = {100.f, 100.f}; world.agents[1].position = {140.f, 100.f};
      if (role == 1) world.agents[0].privateOrder = PrivateOrder::Traitor;
      if (role == 2) world.agents[1].privateOrder = PrivateOrder::Traitor;
      world.agents[1].health = 0.f; simulation.Step(0.1f, {});
      Require(world.agents[1].incapacitated && Interaction::Prompt(world, world.agents[0]) == "E · Reanimar", "El rol privado cambia la pista pública de rescate");
      Require(simulation.SnapshotFor(1, true).at("rescueSignals").size() == 1, "La señal de rescate permite deducir el rol ajeno");
      ActionCommand use; use.interact = true; simulation.Step(0.1f, {{1, use}});
      Require(world.agents[1].lifeState == LifeState::Alive && !world.agents[1].incapacitated && world.agents[1].health >= GameplayConfig::RevivedHealth, "La autoridad de rescate distingue roles secretos");
      world.agents[1].health = 0.f; simulation.Step(0.1f, {});
      world.obstacles.push_back({{{120.f, 80.f}, {5.f, 40.f}}, CoverKind::High, true, "Muro de rescate"});
      simulation.Step(0.1f, {{1, use}});
      Require(world.agents[1].incapacitated, "El rescate uniforme atraviesa una pared");
    }
  }
  void TraitorWeaponsWorkInEveryContract() {
    for (const auto contract : {ContractId::GhostArchive, ContractId::SilentRelay, ContractId::DoubleCross}) {
      Simulation simulation; auto &world = Prepare(simulation); world.contract = contract;
      auto &actor = world.agents[0]; auto &victim = world.agents[1]; actor.privateOrder = PrivateOrder::Traitor;
      actor.position = {100.f, 100.f}; victim.position = {180.f, 100.f};
      const auto head = CombatGeometry::ActorVolumes(victim.position, victim.facing, victim.stance).front().center;
      const auto offset = head - actor.position;
      ActionCommand fire; fire.fireLethal = true; fire.aimX = offset.x; fire.aimY = offset.y;
      fire.aimSlope = (CombatGeometry::HeadCenterHeight(victim.stance) + CombatConfig::HeadHeight * 0.25f - CombatGeometry::WeaponHeight(actor.stance)) / offset.length();
      simulation.Step(0.1f, {{1, fire}});
      Require(!world.shots.empty() && world.shots.back().targetKind == ShotTarget::Agent && world.shots.back().targetId == 2 && victim.incapacitated,
        "Un contrato asigna traidor pero su disparo atraviesa a los demás participantes");
      Require(world.phase == GamePhase::Mission && !world.results.contains(1), "Un impacto concede victoria antes de la eliminación real");
    }
  }
  void DisconnectionCannotCompleteTraitorEliminations() {
    Simulation simulation; auto &world = Prepare(simulation); world.agents[0].privateOrder = PrivateOrder::Traitor;
    simulation.DisconnectPlayer(3); world.agents[1].health = 0.f; simulation.Step(0.1f, {}); Advance(simulation, 551);
    Require(world.agents[1].lifeState == LifeState::Dead && world.agents[2].lifeState == LifeState::Alive && !world.agents[2].connected, "El fixture no conserva al desertor vivo");
    Require(world.phase == GamePhase::Results && !world.results.at(1).traitorVictory, "Desconectar a una víctima sustituye su eliminación real");
  }
  void SurvivorsExtractWithIndividualResults() {
    Simulation simulation; auto &world = Prepare(simulation); world.agents[0].health = 0.f; simulation.Step(0.1f, {}); Advance(simulation, 551);
    world.agents[1].position = {440.f, 300.f}; world.agents[2].position = {750.f, 300.f};
    Interactable terminal{11, ObjectKind::Terminal, {500.f, 300.f}, "Objetivo"}; terminal.solid = SpatialOverride::Disabled;
    Interactable exit{17, ObjectKind::Extraction, {550.f, 300.f}, "Extracción"}; world.objects = {terminal, exit};
    ActionCommand move; move.moveX = 1.f; Advance(simulation, 15, 2, move, 1.f / 60.f);
    ActionCommand use; use.interact = true; simulation.Step(0.1f, {{2, use}});
    Require(world.objectives == 1, "El superviviente no completa el objetivo por interacción real");
    Advance(simulation, 40, 2, move, 1.f / 60.f); simulation.Step(0.1f, {{2, use}});
    Require(world.agents[1].extracted && world.phase == GamePhase::Mission, "La primera extracción termina antes del último superviviente");
    move.moveX = -1.f; Advance(simulation, 110, 3, move, 1.f / 60.f); simulation.Step(0.1f, {{3, use}});
    Require(world.phase == GamePhase::Results && !world.results.at(1).success && world.results.at(2).success && world.results.at(3).success, "Los resultados individuales no distinguen baja y supervivientes extraídos");
    RequirePrivateResults(simulation);
  }
  void TraitorDeathStillAllowsSilentRelay() {
    Simulation simulation; auto &world = Prepare(simulation, 2, ContractId::SilentRelay);
    auto &traitor = world.agents[0]; auto &survivor = world.agents[1];
    traitor.privateOrder = PrivateOrder::Traitor; survivor.privateOrder = PrivateOrder::RecoverDevice;
    traitor.position = {370.f, 300.f}; survivor.position = {440.f, 300.f};
    Interactable terminal{11, ObjectKind::Terminal, {500.f, 300.f}, "Archivo"}; terminal.solid = SpatialOverride::Disabled;
    Interactable relay{13, ObjectKind::CoopSwitch, {650.f, 300.f}, "Repetidor"}; relay.solid = SpatialOverride::Disabled;
    relay.puzzle.kind = PuzzleKind::Coordination; relay.puzzle.family = PuzzleFamily::Coordination; relay.puzzle.requiredAgents = 2; relay.puzzle.soloAllowed = true;
    Interactable exit{17, ObjectKind::Extraction, {800.f, 300.f}, "Extracción"}; world.objects = {terminal, relay, exit};
    const auto head = CombatGeometry::ActorVolumes(traitor.position, traitor.facing, traitor.stance).front().center;
    const auto offset = head - survivor.position;
    ActionCommand fire; fire.fireLethal = true; fire.aimX = offset.x; fire.aimY = offset.y;
    fire.aimSlope = (CombatGeometry::HeadCenterHeight(traitor.stance) + CombatConfig::HeadHeight * 0.25f - CombatGeometry::WeaponHeight(survivor.stance)) / offset.length();
    simulation.Step(0.1f, {{survivor.id, fire}});
    Require(!world.shots.empty() && world.shots.back().targetKind == ShotTarget::Agent && world.shots.back().targetId == traitor.id &&
      world.shots.back().zone == HitZone::Head && traitor.incapacitated, "El traidor no cae por un impacto real de cabeza con rescate disponible");
    Advance(simulation, 551);
    Require(traitor.lifeState == LifeState::Dead && traitor.rescueExpired && traitor.connected && survivor.lifeState == LifeState::Alive &&
      world.phase == GamePhase::Mission && world.results.empty(), "La muerte definitiva del traidor impide continuar al normal operativo");
    ActionCommand move; move.moveX = 1.f; Advance(simulation, 15, survivor.id, move, 1.f / 60.f);
    ActionCommand use; use.interact = true; simulation.Step(0.1f, {{survivor.id, use}});
    Require(world.objectives == 1 && world.objects[0].completed && world.phase == GamePhase::Mission, "El normal no recupera el archivo por interacción tras morir el traidor");
    Advance(simulation, 95, survivor.id, move, 1.f / 60.f); simulation.Step(0.1f, {{survivor.id, use}});
    Require(survivor.puzzle.target == relay.id && survivor.puzzle.family == PuzzleFamily::Coordination, "El superviviente no accede físicamente al relé real");
    ActionCommand hold; hold.puzzleTarget = relay.id; hold.puzzleHeld = true; Advance(simulation, 30, survivor.id, hold);
    Require(world.objects[1].completed && world.objectives == GetContract(ContractId::SilentRelay).requiredObjectives && survivor.privateComplete &&
      world.phase == GamePhase::Mission, "El traidor Dead conectado bloquea el relé auxiliar del último normal");
    Advance(simulation, 80, survivor.id, move, 1.f / 60.f); simulation.Step(0.1f, {{survivor.id, use}});
    Require(world.phase == GamePhase::Results && world.alarmCount == 0 && survivor.extracted && world.results.at(survivor.id).success &&
      !world.results.at(traitor.id).success && !world.results.at(traitor.id).traitorVictory, "Relevo silencioso no concede éxito normal y fracaso privado al traidor muerto");
    RequirePrivateResults(simulation);
  }
  void AllDefinitivelyDeadEndsInPrivateFailure() {
    Simulation simulation; auto &world = Prepare(simulation); world.agents[0].privateOrder = PrivateOrder::Traitor;
    Interactable terminal{11, ObjectKind::Terminal, {520.f, 100.f}, "Archivo"}; terminal.solid = SpatialOverride::Disabled; world.objects = {terminal};
    ActionCommand use; use.interact = true; simulation.Step(0.1f, {{3, use}});
    Require(world.objectives == GetContract(world.contract).requiredObjectives && world.phase == GamePhase::Mission, "El fixture no completa el objetivo mediante E antes de las bajas");
    world.agents[0].health = 0.f; world.agents[1].health = 0.f; simulation.Step(0.1f, {});
    Require(world.agents[0].incapacitated && world.agents[1].incapacitated && world.agents[2].lifeState == LifeState::Alive, "Las primeras bajas no conservan el rescate del último operativo");
    Advance(simulation, 551);
    Require(world.agents[0].lifeState == LifeState::Dead && world.agents[1].lifeState == LifeState::Dead &&
      world.agents[0].rescueExpired && world.agents[1].rescueExpired && world.phase == GamePhase::Mission, "Las bajas definitivas terminan la operación con un normal todavía vivo");
    world.agents[2].health = 0.f; simulation.Step(0.1f, {});
    Require(std::ranges::all_of(world.agents, [](const Agent &agent) { return agent.lifeState == LifeState::Dead && !agent.alive && !agent.incapacitated && agent.health == 0.f; }) &&
      world.phase == GamePhase::Results, "La última baja sin rescatador no produce tres muertes definitivas y cierre de operación");
    Require(std::ranges::all_of(world.results, [](const auto &entry) { return !entry.second.success && !entry.second.traitorVictory; }) &&
      std::ranges::none_of(world.agents, &Agent::extracted), "Todos Dead concede éxito o victoria privada aunque nadie pudo extraerse");
    Require(std::ranges::count(world.events, EventKind::AgentKilled, &MissionEvent::kind) == 3, "La autoridad no registra exactamente una muerte por participante");
    RequirePrivateResults(simulation);
    const auto tick = world.tick; const auto events = world.events.size(); simulation.Step(0.1f, {{3, use}});
    Require(world.phase == GamePhase::Results && world.tick == tick && world.events.size() == events, "Los controles posteriores reactivan la misión definitivamente perdida");
  }
}

int main() {
  try {
    PrivateAssignment(); ExpireAndSpectateWithoutControllingSurvivors(); RescueDoesNotRevealRoles(); TraitorWeaponsWorkInEveryContract(); DisconnectionCannotCompleteTraitorEliminations(); SurvivorsExtractWithIndividualResults(); TraitorDeathStillAllowsSilentRelay(); AllDefinitivelyDeadEndsInPrivateFailure();
    std::cout << "PASS resultados: " << checks << " comprobaciones de roles, rescate, muerte individual y espectador privado\n"; return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL resultados: " << error.what() << '\n'; return 1; }
}
