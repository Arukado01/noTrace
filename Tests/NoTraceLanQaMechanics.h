#pragma once

#include "NoTraceLanQaPilot.h"
#include "NoTraceSimulation.h"
#include "NoTraceTraversal.h"
#include "NoTraceInteraction.h"
#include "NoTraceForcedFalls.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceVisibility.h"
#include "NoTraceDistractionGeometry.h"
#include "NoTraceCoordination.h"
#include "Config/NoTraceTrainingConfig.h"
#include "Config/NoTraceCqcConfig.h"
#include "Config/NoTraceNavigationConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace NoTrace::Qa {
  class MechanicsPilot {
  public:
    void Begin(WorldState &world) { BeginPhase(world, false); }
    void BeginMission(WorldState &world) { BeginPhase(world, true); }
    void BeginInspection(WorldState &world) { BeginPhase(world, true, true); }
    void BeginPipe(WorldState &world) { BeginPhase(world, false, false, true); }
    bool Done() const { return done_ || failed_; }
    bool Failed() const { return failed_; }
    std::string Stage() const { return stage_; }
    std::string Failure() const { return failure_; }
    nlohmann::json Coverage() const {
      return {{"scope", "Preparaciones iniciales explícitas sobre geometría real; efectos comprobados tras ActionCommand y ticks autoritativos. No sustituye revisión visual ni LAN entre dos equipos."},
        {"done", done_}, {"failed", failed_}, {"failure", failure_}, {"stage", stage_}, {"requestedPhases", requestedPhases_}, {"cases", records_}, {"setups", setups_}, {"gaps", gaps_}, {"inspectionObservations", inspectionObservations_},
        {"verifiedPuzzleFamilies", {{"arrows", familiesSeen_[0]}, {"wires", familiesSeen_[1]}, {"levers", familiesSeen_[2]}, {"coordination", familiesSeen_[3]}}},
        {"verifiedTrainingDevices", {{"camera", devicesSeen_[0]}, {"panel", devicesSeen_[1]}, {"trap", devicesSeen_[2]}}}};
    }

    ActionCommand Update(WorldState &world, PlayerId hostId, float dt) {
      if (Done() || cases_.empty()) return {};
      auto *agent = FindAgent(world, hostId);
      if (!agent || !agent->connected || !agent->alive) { Fail("El anfitrión no está operativo"); return {}; }
      if (hostId != host_) { Fail("Cambió el dueño autoritativo del piloto"); return {}; }
      if ((mission_ && world.phase != GamePhase::Mission) || (!mission_ && !IsTraining(world))) { Fail("Cambió la fase durante el caso mecánico"); return {}; }
      dt_ = std::isfinite(dt) && dt > 0.f ? std::min(dt, 0.1f) : 0.f;
      elapsed_ += dt_; phaseTime_ += dt_;
      if (elapsed_ > 90.f) { Fail("Tiempo agotado en " + stage_); return {}; }
      if (agent->incapacitated && Current() != Case::CaptureRescue) return {};
      if (!prepared_) {
        if (agent->stance == Stance::Prone) { ActionCommand stand; stand.prone = true; return stand; }
        if (agent->stance == Stance::Crouched) { ActionCommand stand; stand.crouch = true; return stand; }
        if (agent->transitionTime > 0.f || agent->staggerRemaining > 0.f || agent->actionCooldown > 0.f || agent->actionPoseTime > 0.f) return {};
        if (!Prepare(world, *agent)) return {};
        prepared_ = true; Phase(0, "inicio");
        if (Current() == Case::FatalPush) { ActionCommand push; push.push = true; issued_ = true; return push; }
        return {};
      }
      switch (Current()) {
      case Case::Grips: return Grips(world, *agent);
      case Case::Pipe: return Pipe(world, *agent);
      case Case::Stairs: return Stairs(world, *agent);
      case Case::Elevator: return Elevator(world, *agent);
      case Case::Closet: case Case::Container: case Case::Table: return Hideout(world, *agent);
      case Case::Drag: return Drag(world, *agent);
      case Case::Cqc: return Cqc(world, *agent);
      case Case::Weapons: return Weapons(world, *agent);
      case Case::Cover: return Cover(world, *agent);
      case Case::Postures: return Postures(world, *agent);
      case Case::Decoy: return Decoy(world, *agent);
      case Case::Camera: return Camera(world, *agent);
      case Case::Panel: case Case::Trap: case Case::Door: return Puzzle(world, *agent);
      case Case::Computer: return Levers(world, *agent);
      case Case::Coordination: return Cooperate(world, *agent);
      case Case::StoreBody: return StoreBody(world, *agent);
      case Case::PushWest: case Case::PushEast: case Case::PushRepeat: case Case::FatalPush: return Push(world, *agent);
      case Case::Reset: return Reset(world, *agent);
      case Case::CaptureRescue: return CaptureRescue(world, *agent);
      case Case::Puzzle: return Puzzle(world, *agent);
      case Case::Inspection: return Inspection(world, *agent);
      }
      return {};
    }

    ActionCommand ClientUpdate(const WorldState &world, PlayerId clientId) {
      const auto *client = FindAgent(world, clientId);
      if (!client || !client->connected || !client->alive || client->incapacitated || client->hidden) return {};
      for (const auto &agent : world.agents) if (agent.id != clientId && agent.connected && agent.alive && agent.incapacitated && agent.incapacityRemaining > 0.f) {
        auto command = clientRoute_.TravelTo(world, clientId, agent.position, agent.supportId, true);
        if (std::abs(agent.elevation - client->elevation) <= LevelConfig::MaximumStepHeight &&
            WorldGeometry::SpatialDistance(WorldGeometry::Feet(*client), WorldGeometry::Feet(agent)) < GameplayConfig::InteractionDistance * 0.9f &&
            !WorldGeometry::RayOccluded(world, {client->position.x, client->position.y, client->elevation + CombatConfig::ProneHeight}, {agent.position.x, agent.position.y, agent.elevation + CombatConfig::ProneHeight})) {
          command = {}; command.interact = true;
        }
        return command;
      }
      if (client->puzzle.target && client->puzzle.family == PuzzleFamily::Coordination) {
        ActionCommand hold; hold.puzzleTarget = client->puzzle.target; hold.puzzleHeld = true; return hold;
      }
      for (const auto &object : world.objects) if (object.kind == ObjectKind::CoopSwitch && object.active && object.puzzle.state == PuzzleState::InUse && object.puzzle.requiredAgents == 2) {
        if (coordinationClientTarget_ != object.id) { coordinationClientTarget_ = object.id; coordinationClientTick_ = world.tick; }
        if (world.tick - coordinationClientTick_ < static_cast<std::uint64_t>(GameplayConfig::SimulationHz * 0.8f)) return {};
        if (std::abs(client->elevation - object.elevation) <= LevelConfig::MaximumStepHeight && WorldGeometry::DistanceToBounds(client->position, WorldGeometry::ObjectFootprint(object)) < GameplayConfig::InteractionDistance * 0.9f) {
          ActionCommand join; join.interact = true; return join;
        }
        return clientRoute_.TravelTo(world, clientId, object.position, object.supportId, false);
      }
      ActionCommand command;
      for (const auto &agent : world.agents) if (agent.id != clientId && agent.connected && agent.alive) {
        const auto delta = agent.position - client->position;
        command.aimX = delta.x; command.aimY = delta.y; command.orientBodyToAim = true;
        break;
      }
      return command;
    }

  private:
    enum class Case { Grips, Pipe, Stairs, Elevator, Closet, Container, Table, Drag, Cqc, Weapons, Cover, Postures, Decoy, Camera, Panel, Trap, Door, Computer, Coordination, StoreBody, PushWest, PushEast, Reset, PushRepeat, CaptureRescue, FatalPush, Puzzle, Inspection };
    using Json = nlohmann::json;
    std::vector<Case> cases_;
    std::size_t index_ = 0;
    PlayerId host_ = 0;
    bool mission_ = false, prepared_ = false, done_ = false, failed_ = false, issued_ = false, seen_ = false, stairSeen_ = false, bridgeSeen_ = false;
    unsigned int phase_ = 0;
    std::uint32_t target_ = 0, inspector_ = 0, lastHits_ = 0;
    float elapsed_ = 0.f, phaseTime_ = 0.f, dt_ = 1.f / 60.f, initialStamina_ = 0.f;
    float inspectionSampleTime_ = 0.f;
    float pipeHoldStartedAt_ = 0.f, pipeHoldMinimumStamina_ = 0.f;
    std::uint64_t pipeHoldStartedTick_ = 0, pipeHoldSampleTick_ = 0;
    unsigned int pipeHoldSamples_ = 0;
    sf::Vector3f pipeHoldFeet_;
    sf::Vector2f initialPosition_, initialFacing_;
    std::string stage_ = "mecanicas_sin_iniciar", failure_;
    Json records_ = Json::array(), setups_ = Json::array(), gaps_ = Json::array();
    Json requestedPhases_ = Json::array(), inspectionObservations_ = Json::array();
    MissionPilot clientRoute_;
    MissionPilot hostRoute_;
    std::vector<std::uint8_t> leverOrder_;
    sf::Vector2f routeTarget_;
    std::uint32_t node_ = 0, coordinationClientTarget_ = 0;
    std::uint64_t coordinationClientTick_ = 0;
    std::array<bool, 4> familiesSeen_{};
    std::array<bool, 3> devicesSeen_{};
    PuzzleFamily family_ = PuzzleFamily::Automatic;

    Case Current() const { return cases_[index_]; }
    static Agent *FindAgent(WorldState &world, PlayerId id) { const auto found = std::ranges::find(world.agents, id, &Agent::id); return found == world.agents.end() ? nullptr : &*found; }
    static const Agent *FindAgent(const WorldState &world, PlayerId id) { const auto found = std::ranges::find(world.agents, id, &Agent::id); return found == world.agents.end() ? nullptr : &*found; }
    static Guard *FindGuard(WorldState &world, std::uint32_t id) { const auto found = std::ranges::find(world.guards, id, &Guard::id); return found == world.guards.end() ? nullptr : &*found; }
    static Interactable *FindObject(WorldState &world, std::uint32_t id) { const auto found = std::ranges::find(world.objects, id, &Interactable::id); return found == world.objects.end() ? nullptr : &*found; }
    static sf::Vector2f Unit(sf::Vector2f value) { return value.lengthSquared() > 0.0001f ? value.normalized() : sf::Vector2f{1.f, 0.f}; }
    static bool FreeGuard(const Guard &guard) { return !guard.trainingDummy && !guard.bodyHidden && guard.state != GuardState::Dead && guard.state != GuardState::Incapacitated && guard.heldBy == 0 && !guard.forcedFall.active; }
    static std::string Name(Case value) {
      switch (value) {
      case Case::Grips: return "training_pared_saliente_tubo_soltar";
      case Case::Pipe: return "training_tubo_primera_entrada";
      case Case::Stairs: return "training_escalera_puente";
      case Case::Elevator: return "training_ascensor_ida_vuelta";
      case Case::Closet: return "training_armario";
      case Case::Container: return "training_contenedor";
      case Case::Table: return "training_mesa_raton_salida";
      case Case::Drag: return "training_arrastre";
      case Case::Cqc: return "training_cqc_estamina";
      case Case::Weapons: return "training_armas_recarga";
      case Case::Cover: return "training_cobertura_asomado_golpe";
      case Case::Postures: return "training_posturas_rodada_zambullida";
      case Case::Decoy: return "mission_senuelo_oido_ia";
      case Case::Camera: return "training_camara_detecta";
      case Case::Panel: return "training_panel_camara_cables";
      case Case::Trap: return "training_trampa_desactivar";
      case Case::Door: return "training_cerradura_flechas";
      case Case::Computer: return "training_palancas_distribuidas";
      case Case::Coordination: return "training_coordinacion_dos_jugadores";
      case Case::StoreBody: return "mission_guardar_cuerpo_real";
      case Case::PushWest: return "training_empujon_oeste";
      case Case::PushEast: return "training_empujon_este";
      case Case::Reset: return "training_rearmar";
      case Case::PushRepeat: return "training_repetir_empujon";
      case Case::CaptureRescue: return "mission_captura_rescate";
      case Case::FatalPush: return "mission_caida_mortal";
      case Case::Puzzle: return "mission_puzle";
      case Case::Inspection: return "mission_mesa_inspeccion";
      }
      return "mecanica_desconocida";
    }
    void BeginPhase(WorldState &world, bool mission, bool inspectionOnly = false, bool pipeOnly = false) {
      mission_ = mission; done_ = failed_ = prepared_ = issued_ = seen_ = stairSeen_ = bridgeSeen_ = false; failure_.clear(); index_ = 0; phase_ = target_ = inspector_ = lastHits_ = 0; elapsed_ = phaseTime_ = 0.f;
      if (world.agents.empty()) { Fail("No hay anfitrión en el mundo"); return; }
      host_ = world.agents.front().id;
      if (inspectionOnly && world.phase != GamePhase::Mission) { Fail("La inspección aislada requiere una misión desplegada real"); return; }
      if (pipeOnly && !IsTraining(world)) { Fail("El tubo aislado requiere el entrenamiento real abierto desde la base"); return; }
      if (inspectionOnly) cases_ = {Case::Inspection};
      else if (mission) cases_ = {Case::Decoy, Case::CaptureRescue, Case::FatalPush, Case::StoreBody, Case::Puzzle, Case::Inspection};
      else {
        familiesSeen_ = {}; devicesSeen_ = {};
        records_ = Json::array(); setups_ = Json::array(); requestedPhases_ = Json::array(); inspectionObservations_ = Json::array();
        gaps_ = {{{"group", "cobertura_y_asomarse"}, {"reason", "Pendiente de escenario específico en una ampliación del piloto"}},
          {{"group", "señuelos_golpes"}, {"reason", "Pendiente de comprobar reacción perceptiva con comandos específicos"}},
          {{"group", "agachado_gateo_maniobras"}, {"reason", "Los desplazamientos de este guion no certifican todas las posturas ni maniobras"}},
          {{"group", "guardar_cuerpo"}, {"reason", "Arrastrar diana viva no demuestra ocultar un cuerpo en misión"}},
          {{"group", "trampas_camaras_paneles"}, {"reason", "Pendiente de interacción y respuesta completas de esos dispositivos"}},
          {{"group", "familias_puzle"}, {"reason", "Un circuito resuelto no certifica todas las familias, palancas distribuidas ni coordinación"}}};
        cases_ = {Case::Grips, Case::Stairs, Case::Elevator, Case::Closet, Case::Container, Case::Table, Case::Cover, Case::Postures, Case::Drag, Case::Cqc, Case::Weapons, Case::Camera, Case::Panel, Case::Trap, Case::Door, Case::Computer, Case::Coordination, Case::PushWest, Case::PushEast, Case::Reset, Case::PushRepeat};
        if (pipeOnly) cases_ = {Case::Pipe};
      }
      auto requested = Json::array(); for (const auto value : cases_) requested.push_back(Name(value));
      requestedPhases_.push_back({{"phase", pipeOnly ? "pipe_only" : inspectionOnly ? "inspection_only" : mission ? "mission" : "training"}, {"tick", world.tick}, {"cases", std::move(requested)}});
      stage_ = Name(Current()) + "/preparacion";
    }
    void Phase(unsigned int value, const std::string &label) { phase_ = value; phaseTime_ = 0.f; issued_ = false; stage_ = Name(Current()) + "/" + label; }
    void Fail(const std::string &message) { failed_ = true; failure_ = message; records_.push_back({{"case", stage_}, {"status", "FAIL"}, {"reason", message}}); }
    void Finish(WorldState &world, const std::string &evidence) {
      records_.push_back({{"case", Name(Current())}, {"status", "PASS"}, {"tick", world.tick}, {"seconds", elapsed_}, {"evidence", evidence}});
      const auto group = Current() == Case::Cover ? "cobertura_y_asomarse" : Current() == Case::Postures ? "agachado_gateo_maniobras" : Current() == Case::Decoy ? "señuelos_golpes" : Current() == Case::StoreBody ? "guardar_cuerpo" : "";
      if (*group) RemoveGap(group);
      if (Current() == Case::Camera) devicesSeen_[0] = true;
      if (Current() == Case::Panel) { devicesSeen_[1] = true; familiesSeen_[1] = true; }
      if (Current() == Case::Trap) { devicesSeen_[2] = true; familiesSeen_[1] = true; }
      if (Current() == Case::Door) familiesSeen_[0] = true;
      if (Current() == Case::Computer) familiesSeen_[2] = true;
      if (Current() == Case::Coordination) familiesSeen_[3] = true;
      if (std::ranges::all_of(devicesSeen_, [](bool value) { return value; })) RemoveGap("trampas_camaras_paneles");
      if (std::ranges::all_of(familiesSeen_, [](bool value) { return value; })) RemoveGap("familias_puzle");
      AdvanceCase();
    }
    void RemoveGap(const std::string &group) { auto remaining = Json::array(); for (const auto &entry : gaps_) if (entry.value("group", std::string{}) != group) remaining.push_back(entry); gaps_ = std::move(remaining); }
    void Gap(const std::string &reason) { gaps_.push_back({{"case", Name(Current())}, {"reason", reason}}); AdvanceCase(); }
    void AdvanceCase() {
      ++index_; prepared_ = false; elapsed_ = phaseTime_ = 0.f; target_ = inspector_ = lastHits_ = 0; seen_ = stairSeen_ = bridgeSeen_ = false; family_ = PuzzleFamily::Automatic;
      if (index_ == cases_.size()) { done_ = true; stage_ = mission_ ? "mecanicas_mision_completadas" : "mecanicas_training_completadas"; }
      else stage_ = Name(Current()) + "/preparacion";
    }
    bool PositionFree(const WorldState &world, const Agent &agent, sf::Vector2f position, float elevation, std::uint32_t support, PlayerId ignore = 0) const {
      const auto found = WorldGeometry::FindSupport(world, position, elevation, support, GameplayConfig::AgentRadius);
      if (!found || found->id != support || std::abs(found->elevation - elevation) > LevelConfig::SupportTolerance ||
          WorldGeometry::PositionBlocked(world, position, GameplayConfig::AgentRadius, elevation, CombatConfig::StandingHeight, support)) return false;
      for (const auto &other : world.agents) if (other.id != agent.id && other.id != ignore && other.connected && !other.hidden && std::abs(other.elevation - elevation) < CombatConfig::StandingHeight && (other.position - position).length() < GameplayConfig::AgentRadius * 2.f) return false;
      for (const auto &guard : world.guards) if (!guard.bodyHidden && std::abs(guard.elevation - elevation) < CombatConfig::StandingHeight && (guard.position - position).length() < GameplayConfig::AgentRadius * 2.f) return false;
      return true;
    }
    bool Place(WorldState &world, Agent &agent, sf::Vector2f position, float elevation, std::uint32_t support, sf::Vector2f facing, const std::string &reason, bool positionObserver = true) {
      if (agent.hidden || agent.cqcHoldingGuard || agent.cqcHoldingAgent || agent.cqcHeldBy || agent.draggingGuard >= 0 || agent.forcedFall.active || agent.traversal.mode != TraversalMode::Grounded || agent.puzzle.target) { Fail("La preparación intentó interrumpir un mecanismo activo"); return false; }
      if (!PositionFree(world, agent, position, elevation, support)) { Fail("La posición inicial no tiene apoyo libre: " + Name(Current())); return false; }
      setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"actor", agent.id}, {"reason", reason},
        {"from", {agent.position.x, agent.position.y, agent.elevation}}, {"to", {position.x, position.y, elevation}}, {"support", support}, {"physicalSupportChecked", true}});
      agent.position = position; agent.elevation = elevation; agent.supportId = support; agent.facing = Unit(facing);
      if (!positionObserver) return true;
      // El observador se coloca una sola vez al comienzo y fuera del eje de la acción.
      for (auto &observer : world.agents) if (observer.id != agent.id && observer.connected && observer.alive && !observer.incapacitated && !observer.hidden && observer.traversal.mode == TraversalMode::Grounded && !observer.forcedFall.active) {
        const sf::Vector2f perpendicular{-agent.facing.y, agent.facing.x};
        const auto offsets = Current() == Case::Grips || Current() == Case::Cover ? std::array<sf::Vector2f, 6>{{{64.f, 96.f}, {-64.f, 96.f}, {0.f, 96.f}, {96.f, 96.f}, {-96.f, 96.f}, {0.f, 128.f}}} :
          std::array<sf::Vector2f, 6>{perpendicular * 72.f, perpendicular * -72.f, perpendicular * 96.f, perpendicular * -96.f, -agent.facing * 96.f + perpendicular * 64.f, -agent.facing * 96.f - perpendicular * 64.f};
        bool observerPlaced = false;
        for (const auto offset : offsets) {
          const auto candidate = position + offset;
          const auto point = WorldGeometry::FindSupport(world, candidate, elevation, support, GameplayConfig::AgentRadius);
          if (!point || std::abs(point->elevation - elevation) > LevelConfig::SupportTolerance || !PositionFree(world, observer, candidate, point->elevation, point->id)) continue;
          setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"actor", observer.id}, {"reason", "Observador cercano fuera del eje de agarre/empujón"}, {"from", {observer.position.x, observer.position.y, observer.elevation}}, {"to", {candidate.x, candidate.y, point->elevation}}, {"support", point->id}, {"physicalSupportChecked", true}});
          observer.position = candidate; observer.elevation = point->elevation; observer.supportId = point->id; observer.facing = Unit(position - candidate);
          observerPlaced = true;
          break;
        }
        if (!observerPlaced) gaps_.push_back({{"case", Name(Current())}, {"reason", "No se encontró un punto libre cercano para recolocar al observador; no se presume que su cámara vea este caso"}});
        break;
      }
      return true;
    }
    ActionCommand Walk(const Agent &agent, sf::Vector2f target, float speed = GameplayConfig::WalkSpeed) const {
      const auto delta = target - agent.position;
      ActionCommand command;
      if (delta.lengthSquared() > 0.0001f) { const auto direction = Unit(delta) * std::min(1.f, delta.length() / (speed * std::max(dt_, 1.f / GameplayConfig::CommandHz))); command.moveX = direction.x; command.moveY = direction.y; }
      return command;
    }
    ActionCommand Once(ActionCommand command) { if (issued_) return {}; issued_ = true; return command; }
    ActionCommand Use() { ActionCommand command; command.interact = true; return Once(command); }
    bool At(const Agent &agent, sf::Vector2f point, std::uint32_t support) const { return agent.supportId == support && (agent.position - point).length() < 0.3f; }
    bool Prepare(WorldState &world, Agent &agent) {
      switch (Current()) {
      case Case::Grips: return Place(world, agent, {340.f, 1366.f}, 0.f, 5, {0.f, -1.f}, "Inicio del circuito de agarres, sin imponer estados de recorrido");
      case Case::Pipe: {
        const auto link = std::ranges::find_if(world.verticalLinks, [](const VerticalLink &candidate) { return candidate.kind == VerticalLinkKind::Climb && candidate.bidirectional && candidate.fromGrip == 43 && candidate.toSupport == 11; });
        if (link == world.verticalLinks.end()) { Fail("Falta el acceso físico desde plataforma11 al tubo43"); return false; }
        auto probe = agent; probe.position = WorldGeometry::Ground(link->to); probe.elevation = link->to.z; probe.supportId = link->toSupport;
        const auto entry = Traversal::Find(world, probe);
        if (!entry.link || entry.link->id != link->id || !entry.reversed || Traversal::ToGrip(entry) != 43) { Fail("El inicio de tubo no ofrece un agarre B físicamente admisible"); return false; }
        return Place(world, agent, probe.position, probe.elevation, probe.supportId, WorldGeometry::Ground(link->from - link->to), "Inicio único en el extremo real de plataforma11; B debe ejecutar la transición al tubo43, sin recorrer muro, escalera ni ascensor ni imponer Hang");
      }
      case Case::Stairs: return Place(world, agent, {240.f, 1544.f}, 0.f, 5, {0.f, -1.f}, "Pie de la escalera física del entrenamiento");
      case Case::Elevator: return Place(world, agent, TrainingConfig::TraversalCourse::ElevatorPanelPosition, 96.f, 15, {1.f, 0.f}, "Panel superior para llamar y usar la cabina real");
      case Case::Closet: case Case::Container: case Case::Table: {
        target_ = Current() == Case::Closet ? 105u : Current() == Case::Container ? 106u : 107u;
        auto *object = FindObject(world, target_); if (!object) { Fail("Falta el escondite configurado"); return false; }
        const auto point = WorldGeometry::HideoutFrontPosition(*object, GameplayConfig::AgentRadius);
        return Place(world, agent, point, object->elevation, object->supportId, object->position - point, "Acceso físico del escondite real");
      }
      case Case::Drag: case Case::Cqc: case Case::Weapons: {
        target_ = TrainingConfig::FirstDummyId + (Current() == Case::Drag ? 0u : Current() == Case::Cqc ? 1u : 2u);
        auto *guard = FindGuard(world, target_); if (!guard || !guard->trainingDummy) { Fail("Falta la diana de prácticas"); return false; }
        initialPosition_ = guard->position; lastHits_ = guard->trainingHits;
        return Place(world, agent, guard->position - sf::Vector2f{28.f, 0.f}, guard->elevation, guard->supportId, {1.f, 0.f}, "Contacto inicial con diana; CQC, arrastre y disparos usarán comandos");
      }
      case Case::PushWest: case Case::PushEast: case Case::PushRepeat: {
        const auto index = Current() == Case::PushEast ? 4u : 3u; const auto &spawn = TrainingConfig::DummySpawns[index];
        target_ = TrainingConfig::FirstDummyId + index;
        return Place(world, agent, spawn.position - spawn.facing * CqcConfig::GripSeparation, spawn.elevation, spawn.supportId, spawn.facing, "Contacto de empuje sobre la diana elevada configurada");
      }
      case Case::Reset: return Place(world, agent, TrainingConfig::ResetPosition - sf::Vector2f{40.f, 0.f}, 0.f, 1, {1.f, 0.f}, "Acceso a la consola para restaurar las dianas mediante E");
      case Case::Cover: return Place(world, agent, {1164.f, 314.02f}, 0.f, 1, {0.f, -1.f}, "Cara exterior del parapeto alto real, cerca de su extremo izquierdo");
      case Case::Postures: return Place(world, agent, {900.f, 980.f}, 0.f, 1, {1.f, 0.f}, "Zona libre para carrera, agachado, gateo, rodada y zambullida");
      case Case::Decoy: return PrepareDecoy(world, agent);
      case Case::Camera: return PrepareCamera(world, agent);
      case Case::Panel: case Case::Trap: case Case::Door: case Case::Computer: {
        target_ = Current() == Case::Panel ? 101u : Current() == Case::Trap ? 102u : Current() == Case::Door ? 103u : 104u;
        auto *object = FindObject(world, target_); if (!object) { Fail("Falta el dispositivo de entrenamiento"); return false; }
        return NearObject(world, agent, *object, "Acceso al dispositivo real de prácticas, sin modificar circuito ni temporizador");
      }
      case Case::Coordination: return PrepareCooperation(world, agent);
      case Case::StoreBody: return PrepareStoreBody(world, agent);
      case Case::CaptureRescue: return PrepareCapture(world, agent);
      case Case::FatalPush: return PrepareFatalPush(world, agent);
      case Case::Puzzle: return PreparePuzzle(world, agent);
      case Case::Inspection: return PrepareInspection(world, agent);
      }
      return false;
    }

    void StartPipeHold(const WorldState &world, const Agent &agent, unsigned int phase) {
      pipeHoldStartedAt_ = world.elapsed; pipeHoldStartedTick_ = pipeHoldSampleTick_ = world.tick; pipeHoldSamples_ = 1; pipeHoldMinimumStamina_ = agent.stamina; pipeHoldFeet_ = WorldGeometry::Feet(agent); seen_ = false;
      Phase(phase, "tubo_colgado_quieto");
    }
    bool AwaitPipeHold(WorldState &world, const Agent &agent) {
      if (agent.traversal.mode != TraversalMode::Hang || agent.traversal.surfaceId != 43 || agent.forcedFall.active || agent.stamina <= 0.f || WorldGeometry::SpatialDistance(WorldGeometry::Feet(agent), pipeHoldFeet_) > 0.1f || (world.tick > pipeHoldStartedTick_ && agent.moving)) { Fail("La espera sin entradas perdió el agarre del tubo, cayó, agotó estamina o movió el cuerpo"); return false; }
      if (world.tick != pipeHoldSampleTick_) { pipeHoldSampleTick_ = world.tick; ++pipeHoldSamples_; pipeHoldMinimumStamina_ = std::min(pipeHoldMinimumStamina_, agent.stamina); }
      if (world.elapsed - pipeHoldStartedAt_ < 5.f) return false;
      setups_.push_back({{"case", Name(Current())}, {"reason", "Observación de cinco segundos de simulación colgado del tubo43 sin enviar entradas ni modificar el mundo"}, {"fromTick", pipeHoldStartedTick_}, {"tick", world.tick}, {"simulationSeconds", world.elapsed - pipeHoldStartedAt_}, {"observedSimulationSamples", pipeHoldSamples_}, {"minimumStamina", pipeHoldMinimumStamina_}, {"feet", {pipeHoldFeet_.x, pipeHoldFeet_.y, pipeHoldFeet_.z}}, {"grip", 43}, {"remainedHanging", true}, {"forcedFall", false}, {"positionUnchanged", true}});
      seen_ = true; return true;
    }
    ActionCommand Pipe(WorldState &world, Agent &agent) {
      const auto hanging = [&] { return agent.traversal.mode == TraversalMode::Hang && agent.traversal.surfaceId == 43; };
      const auto traverse = [&](bool complete, const std::string &label, bool drop = false) {
        if (complete) { Phase(phase_ + 1, label); return ActionCommand{}; }
        ActionCommand command; command.traverse = !drop; command.drop = drop; return Once(command);
      };
      const auto shimmy = [&](float x, const std::string &label) {
        if (std::abs(agent.position.x - x) < 0.1f && hanging()) { Phase(phase_ + 1, label); return ActionCommand{}; }
        if (agent.forcedFall.active || agent.traversal.surfaceId != 43 || (agent.traversal.mode != TraversalMode::Hang && agent.traversal.mode != TraversalMode::Shimmy)) { Fail("El recorrido aislado perdió el tubo antes de alcanzar su extremo"); return ActionCommand{}; }
        ActionCommand command; command.moveX = std::clamp((x - agent.position.x) / (TraversalConfig::ShimmySpeed * std::max(dt_, 1.f / GameplayConfig::CommandHz)), -1.f, 1.f); return command;
      };
      switch (phase_) {
      case 0:
        if (hanging()) { StartPipeHold(world, agent, 1); return {}; }
        return traverse(false, "tubo_colgado_quieto");
      case 1:
        if (AwaitPipeHold(world, agent)) Phase(2, "tubo_hacia600");
        return {};
      case 2: return shimmy(600.f, "tubo_retorno820");
      case 3: return shimmy(820.f, "subir_plataforma11");
      case 4: return traverse(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 11, "plataforma11_subida");
      case 5: return traverse(hanging(), "retorno_tubo_para_soltar");
      case 6: return shimmy(700.f, "soltar_sobre_suelo");
      case 7: return traverse(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 5 && agent.elevation == 0.f, "aterrizaje_real", true);
      default: Finish(world, "Primer B al tubo43 desde plataforma11, cinco segundos quieto sin entradas, recorrido600→820, subida B, retorno y Mayús+B hasta apoyo5 Z0 mediante comandos reales"); return {};
      }
    }
    ActionCommand Grips(WorldState &world, Agent &agent) {
      const auto hanging = [&](std::uint32_t grip) { return agent.traversal.mode == TraversalMode::Hang && agent.traversal.surfaceId == grip; };
      const auto traverse = [&](bool complete, const std::string &label, bool drop = false) {
        if (complete) { Phase(phase_ + 1, label); return ActionCommand{}; }
        ActionCommand command; command.traverse = !drop; command.drop = drop; return Once(command);
      };
      const auto shimmy = [&](float x, const std::string &label) {
        if (std::abs(agent.position.x - x) < 0.1f && agent.traversal.mode == TraversalMode::Hang) { Phase(phase_ + 1, label); return ActionCommand{}; }
        ActionCommand command; command.moveX = std::clamp((x - agent.position.x) / (TraversalConfig::ShimmySpeed * std::max(dt_, 1.f / GameplayConfig::CommandHz)), -1.f, 1.f); return command;
      };
      switch (phase_) {
      case 0: return traverse(hanging(41), "muro_agarrado");
      case 1: return shimmy(500.f, "enlace_saliente");
      case 2: return traverse(hanging(42), "saliente_agarrado");
      case 3: return traverse(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 10, "plataforma_oeste");
      case 4: return traverse(hanging(42), "retorno_saliente");
      case 5: return shimmy(600.f, "enlace_tubo");
      case 6:
        if (!hanging(43)) return traverse(false, "tubo_colgado_quieto");
        StartPipeHold(world, agent, 7); return {};
      case 7:
        if (!seen_) {
          if (AwaitPipeHold(world, agent)) Phase(7, "tubo_agarrado");
          return {};
        }
        return shimmy(820.f, "extremo_tubo");
      case 8: return traverse(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 11, "plataforma_este");
      case 9: return traverse(hanging(43), "retorno_tubo");
      case 10: return shimmy(700.f, "soltar_sobre_suelo");
      case 11: return traverse(agent.traversal.mode == TraversalMode::Grounded && agent.supportId == 5 && agent.elevation == 0.f, "aterrizaje_real", true);
      default: Finish(world, "B, cinco segundos quieto colgado del tubo43, desplazamiento lateral, transferencias, subida por ambos extremos y Mayús+B terminaron sobre apoyo5 Z0"); return {};
      }
    }
    ActionCommand Stairs(WorldState &world, Agent &agent) {
      stairSeen_ = stairSeen_ || agent.supportId == 20; bridgeSeen_ = bridgeSeen_ || agent.supportId == 12;
      const std::array<sf::Vector2f, 3> points{{{240.f, 1328.f}, {240.f, 1240.f}, {840.f, 1240.f}}};
      if (phase_ < points.size()) { const auto point = points[phase_]; if ((point - agent.position).length() < 0.3f) Phase(phase_ + 1, "tramo_" + std::to_string(phase_ + 1)); else return Walk(agent, point); }
      else if (agent.supportId == 11 && agent.elevation == 96.f && stairSeen_ && bridgeSeen_) Finish(world, "Se observaron apoyos20 y12 durante el desplazamiento a pie hasta plataforma11 Z96");
      else Fail("La escalera o el puente no fueron atravesados físicamente");
      return {};
    }
    ActionCommand Elevator(WorldState &world, Agent &agent) {
      const auto found = std::ranges::find(world.elevators, TrainingConfig::TraversalCourse::ElevatorId, &ElevatorState::id);
      if (found == world.elevators.end()) { Fail("Falta el ascensor de entrenamiento"); return {}; }
      const auto &lift = *found;
      const auto docked = [&](float z) { return lift.phase == ElevatorPhase::Docked && !lift.requested && std::abs(lift.elevation - z) < LevelConfig::SupportTolerance; };
      const auto cabin = TrainingConfig::TraversalCourse::ElevatorPosition, panel = TrainingConfig::TraversalCourse::ElevatorPanelPosition;
      if ((phase_ == 4 || phase_ == 8) && (agent.supportId != 90 || std::abs(agent.elevation - lift.elevation) > LevelConfig::SupportTolerance)) { Fail("La cabina dejó atrás al ocupante"); return {}; }
      switch (phase_) {
      case 0: if (docked(96.f)) Phase(2, "embarcar_arriba"); else { Phase(1, "llamar_arriba"); return Use(); } break;
      case 1: if (docked(96.f)) Phase(2, "embarcar_arriba"); break;
      case 2: if (At(agent, cabin, 90)) Phase(3, "pedir_descenso"); else return Walk(agent, cabin); break;
      case 3: Phase(4, "descendiendo"); return Use();
      case 4: if (docked(0.f)) Phase(5, "salir_abajo"); break;
      case 5: if (At(agent, panel, 5)) Phase(6, "embarcar_abajo"); else return Walk(agent, panel); break;
      case 6: if (At(agent, cabin, 90)) Phase(7, "pedir_ascenso"); else return Walk(agent, cabin); break;
      case 7: Phase(8, "ascendiendo"); return Use();
      case 8: if (docked(96.f)) Phase(9, "salir_arriba"); break;
      case 9: if (At(agent, panel, 15)) Finish(world, "Llamada superior, descenso y ascenso con ocupante y salida sobre muelle15 Z96"); else return Walk(agent, panel); break;
      }
      return {};
    }
    ActionCommand Hideout(WorldState &world, Agent &agent) {
      auto *object = FindObject(world, target_); if (!object) { Fail("Desapareció el escondite"); return {}; }
      if (phase_ == 0) { if (agent.hideoutId == target_ && agent.hidden) { initialPosition_ = agent.position; initialFacing_ = agent.facing; Phase(1, "interior"); } else return Use(); }
      else if (phase_ == 1) {
        if (Current() == Case::Table) {
          if (phaseTime_ > 0.3f) { if (agent.position != initialPosition_ || agent.facing != initialFacing_ || agent.weaponAim.y > -0.9f) { Fail("El ratón no orientó la visión manteniendo el cuerpo bajo la mesa"); return {}; } Phase(2, "salida_por_raton"); }
          ActionCommand look; look.aimY = -1.f; look.moveX = -1.f; return look;
        }
        if (phaseTime_ > 0.4f) Phase(2, "salida");
      } else if (phase_ == 2) {
        if (!agent.hidden && agent.hideoutId == 0 && object->occupantAgent == 0) {
          if (Current() == Case::Table && agent.position.x <= object->position.x) Fail("E no salió por el lado señalado por el ratón");
          else Finish(world, "Entrada y salida reales sin ocupación residual; en mesa se comprobó ratón y cuerpo inmóvil");
        } else { ActionCommand leave; leave.interact = true; leave.aimX = 1.f; leave.moveY = -1.f; return Once(leave); }
      }
      return {};
    }
    ActionCommand Drag(WorldState &world, Agent &agent) {
      auto *guard = FindGuard(world, target_); if (!guard) { Fail("Desapareció la diana arrastrable"); return {}; }
      if (phase_ == 0) {
        if (phaseTime_ > 0.6f && agent.draggingGuard == static_cast<int>(target_) && (guard->position - initialPosition_).length() > 12.f) Phase(1, "soltar_arrastre");
        else { ActionCommand drag; drag.dragBody = true; drag.moveY = -1.f; return drag; }
      } else if (phaseTime_ > 0.3f && agent.draggingGuard < 0 && guard->state != GuardState::Incapacitated && !guard->bodyHidden) Finish(world, "E sostenida movió una diana viva; soltar E terminó el arrastre protegido");
      return {};
    }
    ActionCommand Cqc(WorldState &world, Agent &agent) {
      auto *guard = FindGuard(world, target_); if (!guard) { Fail("Desapareció la diana CQC"); return {}; }
      if (phase_ == 0) {
        if (agent.cqcHoldingGuard == target_ && guard->heldBy == host_) {
          setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"reason", "Inicio de subcaso de recurso: dos pulsos sostenidos, sin regeneración durante la sujeción"}, {"staminaBefore", agent.stamina}, {"staminaInitial", GameplayConfig::CqcStaminaCost * 2.f}});
          agent.stamina = GameplayConfig::CqcStaminaCost * 2.f; initialStamina_ = agent.stamina; Phase(1, "liberar_tecla_antes_pulso");
        } else { ActionCommand hold; hold.cqcHeld = true; return hold; }
      } else if (phase_ == 1 && phaseTime_ >= CqcConfig::PulseInterval + 0.1f) { Phase(2, "primer_pulso"); ActionCommand pulse; pulse.cqc = pulse.cqcHeld = true; return Once(pulse); }
      else if (phase_ == 2) {
        if (agent.cqcProgress == 1 && guard->heldBy == host_ && std::abs(agent.stamina - (initialStamina_ - GameplayConfig::CqcStaminaCost)) < 0.01f) Phase(3, "liberar_antes_agotamiento");
      } else if (phase_ == 3 && phaseTime_ >= CqcConfig::PulseInterval + 0.1f) { Phase(4, "pulso_agota_estamina"); ActionCommand pulse; pulse.cqc = pulse.cqcHeld = true; return Once(pulse); }
      else if (phase_ == 4 && agent.cqcHoldingGuard == 0 && guard->heldBy == 0 && guard->cqcState == CqcState::None && agent.cqcProgress == 0) {
        if (agent.stamina > GameplayConfig::StaminaRecoveryPerSecond * dt_ * 3.f) Fail("La sujeción no terminó en la frontera de estamina");
        else Finish(world, "Agarre recíproco y dos pulsos F consumieron24; el segundo liberó sin incapacitar la diana");
      }
      return {};
    }
    ActionCommand Weapons(WorldState &world, Agent &agent) {
      auto *guard = FindGuard(world, target_); if (!guard) { Fail("Desapareció la diana de tiro"); return {}; }
      if (phase_ == 0) {
        if (agent.lethalMagazine == 0 && guard->trainingHits > lastHits_) Phase(1, "recarga_real");
        else { ActionCommand shot; shot.weaponChoice = static_cast<int>(WeaponKind::Pistol); shot.fireSelected = true; shot.aimX = 1.f; return shot; }
      } else if (phase_ == 1) { if (agent.reloadRemaining > 0.f) seen_ = true; if (seen_ && agent.reloadRemaining <= 0.f && agent.lethalMagazine == WeaponConfig::PistolCapacity) { lastHits_ = guard->trainingHits; Phase(2, "arma_no_letal"); } else { ActionCommand reload; reload.reloadWeapon = true; return Once(reload); } }
      else if (phase_ == 2) { if (guard->trainingHits > lastHits_ && guard->health >= TrainingConfig::DummyMinimumHealth && guard->state != GuardState::Dead) Finish(world, "Disparos reales agotaron el cargador, recarga completada y otro impacto no letal protegido"); else { ActionCommand shot; shot.weaponChoice = static_cast<int>(WeaponKind::NonLethal); shot.fireSelected = true; shot.aimX = 1.f; return shot; } }
      return {};
    }
    ActionCommand Cover(WorldState &world, Agent &agent) {
      if (phase_ == 0) { if (agent.cover.active) Phase(1, "asomarse_extremo"); else { ActionCommand enter; enter.coverToggle = true; return Once(enter); } }
      else if (phase_ == 1) {
        if (agent.cover.peekSide == PeekSide::Left && agent.position.x < 1150.f) Phase(2, "volver_a_cubierto");
        else { ActionCommand peek; peek.peekLeft = true; return peek; }
      } else if (phase_ == 2) {
        if (agent.cover.active && agent.cover.anchor.x > 1240.f && agent.cover.peekSide == PeekSide::None && (agent.position - agent.cover.anchor).length() < 0.3f) Phase(3, "golpe_contextual_F");
        else { ActionCommand inward; inward.moveX = 1.f; return inward; }
      } else if (phase_ == 3) {
        if (std::ranges::any_of(world.noises, [&](const NoiseEvent &noise) { return noise.kind == SoundKind::Knock && (noise.position - agent.position).length() <= DistractionConfig::KnockDistance; }) && !agent.cqcPressPending && agent.cqcHoldingGuard == 0) Phase(4, "salir_cobertura");
        else { ActionCommand knock; knock.cqc = knock.cqcHeld = true; return Once(knock); }
      } else if (phase_ == 4) {
        if (!agent.cover.active) Finish(world, "Cobertura real, asomado lateral físico, retorno protegido y F contextual emitieron un golpe de pared sin CQC");
        else { ActionCommand leave; leave.coverToggle = true; return Once(leave); }
      }
      return {};
    }
    ActionCommand Postures(WorldState &world, Agent &agent) {
      if (phase_ == 0) {
        if (phaseTime_ > 0.3f && agent.running) Phase(1, "agacharse");
        else { ActionCommand run; run.run = true; run.moveX = 1.f; return run; }
      } else if (phase_ == 1) {
        if (agent.stance == Stance::Crouched) { initialPosition_ = agent.position; Phase(2, "desplazamiento_agachado"); }
        else { ActionCommand crouch; crouch.crouch = true; return Once(crouch); }
      } else if (phase_ == 2) {
        if ((agent.position - initialPosition_).length() > 20.f) { Phase(3, "rodada"); seen_ = stairSeen_ = bridgeSeen_ = false; }
        else { ActionCommand move; move.moveY = 1.f; return move; }
      } else if (phase_ == 3) {
        if (agent.maneuver.kind == ManeuverKind::Roll) { seen_ = true; stairSeen_ = stairSeen_ || agent.maneuver.phase == ManeuverPhase::Travel; bridgeSeen_ = bridgeSeen_ || agent.maneuver.phase == ManeuverPhase::Recover; }
        if (seen_ && agent.maneuver.kind == ManeuverKind::None) {
          if (!stairSeen_ || !bridgeSeen_ || agent.stance != Stance::Crouched) Fail("La rodada no mostró sus fases o no conservó la postura");
          else Phase(4, "tumbarse");
        } else { ActionCommand roll; roll.roll = true; roll.moveY = 1.f; return Once(roll); }
      } else if (phase_ == 4) {
        if (agent.stance == Stance::Prone) { initialPosition_ = agent.position; Phase(5, "gateo"); }
        else { ActionCommand prone; prone.prone = true; return Once(prone); }
      } else if (phase_ == 5) {
        if ((agent.position - initialPosition_).length() > 18.f && agent.stance == Stance::Prone) Phase(6, "levantarse");
        else { ActionCommand crawl; crawl.moveX = -1.f; return crawl; }
      } else if (phase_ == 6) {
        if (agent.stance == Stance::Standing && agent.maneuver.cooldown <= 0.f) Phase(7, "carrera_previa_zambullida");
        else if (agent.stance == Stance::Prone) { ActionCommand stand; stand.prone = true; return Once(stand); }
      } else if (phase_ == 7) {
        if (phaseTime_ > 0.3f && agent.running) { Phase(8, "zambullida"); seen_ = stairSeen_ = bridgeSeen_ = false; }
        ActionCommand run; run.run = true; run.moveX = 1.f; return run;
      } else if (phase_ == 8) {
        if (agent.maneuver.kind == ManeuverKind::Dive) { seen_ = true; stairSeen_ = stairSeen_ || agent.maneuver.phase == ManeuverPhase::Travel; bridgeSeen_ = bridgeSeen_ || agent.maneuver.phase == ManeuverPhase::Recover; }
        if (seen_ && agent.maneuver.kind == ManeuverKind::None) {
          if (!stairSeen_ || !bridgeSeen_ || agent.stance != Stance::Prone) Fail("La zambullida no mostró sus fases o no terminó tumbado");
          else Finish(world, "Carrera, desplazamiento agachado, gateo, rodada y zambullida ejecutados con fases y posturas autoritativas");
        } else { ActionCommand dive; dive.dive = dive.run = true; dive.moveX = 1.f; return Once(dive); }
      }
      return {};
    }
    bool PrepareDecoy(WorldState &world, Agent &agent) {
      const auto observer = std::ranges::find_if(world.agents, [&](const Agent &other) { return other.id != agent.id && other.connected && other.alive && !other.incapacitated && !other.hidden; });
      if (observer == world.agents.end()) { Gap("No hay observador operativo para el señuelo"); return false; }
      for (auto &guard : world.guards) if (FreeGuard(guard) && guard.state == GuardState::Patrol && guard.suspicion < 0.25f) for (const auto direction : std::array<sf::Vector2f, 4>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}}}) {
        const auto position = observer->position + direction * 80.f, enemy = position + direction * 220.f;
        const auto start = WorldGeometry::FindSupport(world, position, observer->elevation, observer->supportId, GameplayConfig::AgentRadius);
        if (!start || !PositionFree(world, agent, position, start->elevation, start->id)) continue;
        const auto support = WorldGeometry::FindSupport(world, enemy, start->elevation, start->id, GameplayConfig::AgentRadius);
        if (!support || !PositionFree(world, agent, enemy, support->elevation, support->id)) continue;
        const auto path = DistractionGeometry::TraceDecoy(world, position, direction, 140.f, start->elevation);
        if (!path.valid || path.travelled < 80.f) continue;
        if (!Place(world, agent, position, start->elevation, start->id, direction, "Lanzador detrás de patrulla real para comprobar investigación acústica, sin forzar su estado")) return false;
        setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"guard", guard.id}, {"reason", "Patrulla real de espaldas al emisor con ruta inicial de un punto; investigar dependerá del impacto audible"}, {"from", {guard.position.x, guard.position.y, guard.elevation}}, {"to", {enemy.x, enemy.y, support->elevation}}, {"support", support->id}, {"patrolPointsBefore", guard.patrol.size()}, {"initialPatrol", {{enemy.x, enemy.y, support->elevation}}}});
        guard.position = enemy; guard.elevation = support->elevation; guard.supportId = support->id; guard.facing = direction; target_ = guard.id; initialPosition_ = path.to;
        guard.patrol = {enemy}; guard.patrolElevations = {support->elevation}; guard.waypoint = 0; guard.navigation.clear();
        return true;
      }
      Gap("No hay disposición libre de emisor y patrulla calmada para separar oído y visión"); return false;
    }
    ActionCommand Decoy(WorldState &world, Agent &agent) {
      auto *guard = FindGuard(world, target_); if (!guard) { Fail("Desapareció la patrulla oyente"); return {}; }
      if (!world.decoys.empty()) {
        seen_ = true; initialPosition_ = world.decoys.back().to;
        if (std::ranges::any_of(world.noises, [&](const NoiseEvent &noise) { return noise.kind == SoundKind::Decoy && (noise.position - initialPosition_).length() < 1.f; })) { Fail("El señuelo generó ruido de impacto antes de terminar su vuelo"); return {}; }
        if (phase_ == 0) Phase(1, "vuelo_real");
      }
      bridgeSeen_ = bridgeSeen_ || std::ranges::any_of(world.noises, [&](const NoiseEvent &noise) { return noise.kind == SoundKind::Decoy && (noise.position - initialPosition_).length() < 1.f; });
      stairSeen_ = stairSeen_ || (bridgeSeen_ && guard->state == GuardState::Investigate && (guard->lastKnown - initialPosition_).length() < 1.f);
      if (seen_ && bridgeSeen_ && stairSeen_ && world.decoys.empty()) { Finish(world, "Se observó vuelo sin ruido anticipado, impacto en destino real y patrulla investigando su origen acústico"); return {}; }
      if (phase_ == 0) { ActionCommand throwDisc; throwDisc.throwDecoy = true; throwDisc.aimX = agent.facing.x; throwDisc.aimY = agent.facing.y; throwDisc.decoyDistance = 140.f; return Once(throwDisc); }
      return {};
    }
    ActionCommand Push(WorldState &world, Agent &agent) {
      auto *guard = FindGuard(world, target_); if (!guard) { Fail("Desapareció el objetivo del empujón"); return {}; }
      if (phase_ == 0) {
        if (guard->forcedFall.active) { seen_ = true; initialStamina_ = guard->forcedFall.from.z - guard->forcedFall.to.z; Phase(1, "caida_animada"); }
        else { ActionCommand push; push.push = true; return Once(push); }
      } else if (!guard->forcedFall.active && seen_) {
        const bool lethal = Current() == Case::FatalPush;
        if (lethal && (guard->state != GuardState::Dead || initialStamina_ < CqcConfig::FatalPushFallHeight || std::ranges::none_of(world.events, [&](const MissionEvent &event) { return event.kind == EventKind::GuardKilled && event.actor == host_ && event.target == target_; }))) Fail("La caída de misión no confirmó la muerte y su autor al aterrizar");
        else if (!lethal && (guard->state == GuardState::Dead || guard->state == GuardState::Incapacitated || guard->supportId != 1 || guard->elevation != 0.f)) Fail("La caída de prácticas perdió su apoyo o protección");
        else Finish(world, lethal ? "V precipitó una patrulla por un borde real, se observó el estado en aire y la muerte registrada al aterrizar" : "V produjo caída animada hasta apoyo1 Z0; la diana permaneció operativa");
      }
      return {};
    }
    ActionCommand Reset(WorldState &world, Agent &) {
      if (!issued_) return Use();
      const bool reset = std::ranges::all_of(TrainingConfig::DummySpawns, [&](const TrainingConfig::DummySpawn &spawn) {
        const auto index = static_cast<std::uint32_t>(&spawn - TrainingConfig::DummySpawns.data()); const auto *guard = FindGuard(world, TrainingConfig::FirstDummyId + index);
        return guard && guard->position == spawn.position && guard->supportId == spawn.supportId && guard->elevation == spawn.elevation && !guard->forcedFall.active;
      });
      if (reset) Finish(world, "E en REARMAR devolvió todas las dianas configuradas a posición, piso y apoyo iniciales");
      return {};
    }

    std::optional<sf::Vector2f> ObjectApproach(const WorldState &world, const Agent &agent, const Interactable &object) const {
      const auto bounds = WorldGeometry::ObjectFootprint(object);
      const auto end = bounds.position + bounds.size;
      const float margin = GameplayConfig::AgentRadius + 8.f;
      for (const auto point : std::array<sf::Vector2f, 4>{{{bounds.position.x - margin, object.position.y}, {end.x + margin, object.position.y}, {object.position.x, bounds.position.y - margin}, {object.position.x, end.y + margin}}}) {
        if (!PositionFree(world, agent, point, object.elevation, object.supportId)) continue;
        auto probe = agent; probe.position = point; probe.elevation = object.elevation; probe.supportId = object.supportId;
        const auto *nearest = Interaction::FindObject(world, probe);
        if (nearest && nearest->id == object.id) return point;
      }
      return std::nullopt;
    }
    bool NearObject(WorldState &world, Agent &agent, const Interactable &object, const std::string &reason) {
      const auto point = ObjectApproach(world, agent, object);
      return point && Place(world, agent, *point, object.elevation, object.supportId, object.position - *point, reason);
    }
    bool PlaceObserverAt(WorldState &world, const Agent &host, sf::Vector2f position, float elevation, std::uint32_t support, const std::string &reason) {
      for (auto &observer : world.agents) if (observer.id != host.id && observer.connected && observer.alive && !observer.incapacitated && !observer.hidden && !observer.puzzle.target && !observer.cqcHeldBy && observer.traversal.mode == TraversalMode::Grounded && !observer.forcedFall.active) {
        if (!PositionFree(world, observer, position, elevation, support)) return false;
        setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"actor", observer.id}, {"reason", reason}, {"from", {observer.position.x, observer.position.y, observer.elevation}}, {"to", {position.x, position.y, elevation}}, {"support", support}, {"physicalSupportChecked", true}});
        observer.position = position; observer.elevation = elevation; observer.supportId = support; observer.facing = Unit(host.position - position);
        return true;
      }
      return false;
    }
    bool PrepareCamera(WorldState &world, Agent &agent) {
      auto *camera = FindObject(world, 100); if (!camera || camera->camera.state != CameraState::Operating) { Gap("La cámara de prácticas no está operativa antes del caso"); return false; }
      target_ = camera->id;
      const sf::Vector2f direction{std::cos(camera->camera.baseAngle), std::sin(camera->camera.baseAngle)};
      if (!Place(world, agent, camera->position + direction * 120.f, camera->elevation, camera->supportId, -direction, "Exposición inicial dentro del barrido real de la cámara", false)) return false;
      const sf::Vector2f side{-direction.y, direction.x};
      for (const auto offset : std::array<sf::Vector2f, 3>{-direction * 80.f, -direction * 80.f + side * 64.f, -direction * 80.f - side * 64.f}) {
        if (PlaceObserverAt(world, agent, camera->position + offset, camera->elevation, camera->supportId, "Observador detrás de la cámara para que no suplante al objetivo de detección")) return true;
      }
      Gap("No hay espacio físico para un observador detrás de la cámara"); return false;
    }
    ActionCommand Camera(WorldState &world, Agent &agent) {
      auto *camera = FindObject(world, target_); if (!camera) { Fail("Desapareció la cámara de prácticas"); return {}; }
      if (camera->camera.trackedAgent == host_ && camera->progress > 0.f) { seen_ = true; stage_ = Name(Current()) + "/deteccion_gradual"; }
      if (seen_ && agent.detectionConfirmed && agent.detectionProgress == 1.f) {
        if (world.alarm || world.securityActive) Fail("La detección de prácticas activó respuesta de misión");
        else Finish(world, "La cámara adquirió al host y completó sospecha/detección reales sin alarmar la sala de prácticas");
      }
      return {};
    }
    bool PrepareCooperation(WorldState &world, Agent &agent) {
      const auto relay = std::ranges::find_if(world.objects, [](const Interactable &object) { return object.kind == ObjectKind::CoopSwitch && object.active && !object.completed && object.puzzle.requiredAgents == 2; });
      if (relay == world.objects.end()) { Gap("No hay relé cooperativo pendiente para dos agentes"); return false; }
      target_ = relay->id;
      const auto hostPosition = relay->position + sf::Vector2f{-32.f, -24.f}, clientPosition = relay->position + sf::Vector2f{-32.f, 24.f};
      if (!Place(world, agent, hostPosition, relay->elevation, relay->supportId, relay->position - hostPosition, "Dos operadores junto al relé real; la asignación de controles la hará BeginPuzzle", false)) return false;
      if (!PlaceObserverAt(world, agent, clientPosition, relay->elevation, relay->supportId, "Segundo operador en un acceso libre y distinto; deberá enviar E y mantener su control por LAN")) { Gap("No se pudo situar un segundo operador en un acceso físico libre al relé"); return false; }
      return true;
    }
    ActionCommand Cooperate(WorldState &world, Agent &agent) {
      auto *relay = FindObject(world, target_); if (!relay) { Fail("Desapareció el relé cooperativo"); return {}; }
      if (relay->completed && !relay->active && agent.puzzle.target == 0) {
        if (!seen_ || !stairSeen_ || !bridgeSeen_ || std::ranges::any_of(world.agents, [&](const Agent &other) { return other.puzzle.target == target_; })) Fail("El relé no demostró bloqueo individual, controles distintos y progreso simultáneo");
        else Finish(world, "Un operador no progresó; el cliente entró mediante E, ambos mantuvieron controles distintos y el circuito liberó las dos sesiones");
        return {};
      }
      if (phase_ == 0) { if (agent.puzzle.target == target_ && agent.puzzle.family == PuzzleFamily::Coordination) Phase(1, "un_operador_sin_progreso"); else return Use(); }
      else {
        if (phase_ == 1 && phaseTime_ >= 0.55f && relay->puzzle.coordinationProgress == 0.f) { seen_ = true; Phase(2, "esperar_control_del_cliente"); }
        stairSeen_ = stairSeen_ || relay->puzzle.coordinationProgress > 0.f;
        bridgeSeen_ = bridgeSeen_ || std::ranges::any_of(world.agents, [&](const Agent &other) { return other.id != host_ && other.puzzle.target == target_ && other.puzzle.holding && other.puzzle.coordinationButton != agent.puzzle.coordinationButton; });
        ActionCommand hold; hold.puzzleTarget = target_; hold.puzzleHeld = true; return hold;
      }
      return {};
    }
    ActionCommand Levers(WorldState &world, Agent &agent) {
      auto *computer = FindObject(world, target_); if (!computer) { Fail("Desapareció el computador de palancas"); return {}; }
      if (computer->completed && computer->puzzle.state == PuzzleState::Solved) {
        if (leverOrder_.empty() || computer->puzzle.sequenceProgress != leverOrder_.size()) Fail("El computador se resolvió sin completar todas las palancas físicas");
        else Finish(world, "Se leyó el orden real, se cerró el panel y se caminó hasta cada palanca para accionarla mediante E");
        return {};
      }
      if (phase_ == 0) {
        if (agent.puzzle.target == target_ && agent.puzzle.family == PuzzleFamily::Levers) { leverOrder_ = agent.puzzle.sequence; Phase(1, "cerrar_panel_para_caminar"); }
        else return Use();
      } else if (phase_ == 1) {
        if (!agent.puzzle.target) Phase(2, "buscar_palanca_ordenada");
        else { ActionCommand cancel; cancel.puzzleTarget = target_; cancel.puzzleInput = 0; return cancel; }
      } else if (phase_ == 2) {
        if (computer->puzzle.sequenceProgress >= leverOrder_.size()) { Fail("El circuito no confirmó el final de la secuencia de palancas"); return {}; }
        const auto expected = leverOrder_[computer->puzzle.sequenceProgress];
        const auto node = std::ranges::find_if(world.objects, [&](const Interactable &object) { return object.puzzle.parentDevice == target_ && object.puzzle.nodeIndex == expected; });
        if (node == world.objects.end()) { Fail("Falta una palanca física del orden comunicado por el panel"); return {}; }
        const auto point = ObjectApproach(world, agent, *node);
        if (!point) { Fail("No hay acceso físico libre a la siguiente palanca"); return {}; }
        node_ = node->id; routeTarget_ = *point; lastHits_ = computer->puzzle.sequenceProgress; hostRoute_.Reset(); Phase(3, "caminar_a_palanca_" + std::to_string(expected));
      } else if (phase_ == 3) {
        auto *node = FindObject(world, node_); if (!node) { Fail("Desapareció la palanca de destino"); return {}; }
        if (At(agent, routeTarget_, node->supportId)) Phase(4, "accionar_palanca");
        else if (agent.supportId == node->supportId && (agent.position - routeTarget_).length() < 24.f && WorldGeometry::ClearBodySegment(world, WorldGeometry::Feet(agent), {routeTarget_.x, routeTarget_.y, node->elevation}, GameplayConfig::AgentRadius, WorldGeometry::ActorHeight(agent), agent.supportId)) return Walk(agent, routeTarget_, agent.stance == Stance::Crouched ? GameplayConfig::CrouchSpeed : GameplayConfig::WalkSpeed);
        else return hostRoute_.TravelTo(world, host_, routeTarget_, node->supportId, false);
      } else if (phase_ == 4) {
        if (computer->puzzle.sequenceProgress > lastHits_) Phase(2, "siguiente_palanca"); else return Use();
      }
      return {};
    }
    bool PrepareStoreBody(WorldState &world, Agent &agent) {
      for (auto &body : world.guards) if (!body.trainingDummy && !body.bodyHidden && !body.forcedFall.active && body.storedIn == 0 && (body.state == GuardState::Dead || body.state == GuardState::Incapacitated) &&
          std::ranges::any_of(world.events, [&](const MissionEvent &event) { return event.target == body.id && (event.kind == EventKind::GuardKilled || event.kind == EventKind::GuardIncapacitated); })) {
        for (const auto &object : world.objects) if (WorldGeometry::HideoutAllowsBody(object) && !WorldGeometry::HideoutOccupied(object)) {
          const auto front = WorldGeometry::HideoutFrontPosition(object, GameplayConfig::AgentRadius);
          const auto outward = Unit(front - object.position), position = front + outward * 28.f;
          if (!PositionFree(world, agent, position, object.elevation, object.supportId) || WorldGeometry::PositionBlocked(world, front, GameplayConfig::AgentRadius, object.elevation, CombatConfig::ProneHeight, object.supportId) ||
              !WorldGeometry::AtHideoutEntrance(object, position, GameplayConfig::AgentRadius) || !Interaction::HideoutPathClear(world, object, front, object.position)) continue;
          auto probe = agent; probe.position = position; probe.elevation = object.elevation; probe.supportId = object.supportId;
          const auto *selected = Interaction::FindObject(world, probe); if (!selected || selected->id != object.id) continue;
          if (!Place(world, agent, position, object.elevation, object.supportId, -outward, "Acceso inicial con cuerpo generado por muerte/incapacitación registrada; arrastre y almacenamiento usarán E")) return false;
          setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"guard", body.id}, {"reason", "Acercamiento inicial de cuerpo real al acceso; se conservan salud, estado y temporizador"}, {"bodyState", static_cast<int>(body.state)}, {"health", body.health}, {"from", {body.position.x, body.position.y, body.elevation}}, {"to", {front.x, front.y, object.elevation}}, {"support", object.supportId}});
          body.position = front; body.elevation = object.elevation; body.supportId = object.supportId; target_ = body.id; inspector_ = object.id;
          return true;
        }
      }
      Gap("No hay cuerpo con muerte/incapacitación registrada y contenedor compatible libre"); return false;
    }
    ActionCommand StoreBody(WorldState &world, Agent &agent) {
      auto *body = FindGuard(world, target_); auto *object = FindObject(world, inspector_);
      if (!body || !object) { Fail("Desapareció el cuerpo o su contenedor"); return {}; }
      if (body->bodyHidden && body->storedIn == object->id && object->occupantGuard == body->id && object->occupied && !agent.hidden && agent.draggingGuard < 0) { Finish(world, "E sostenida adquirió el cuerpo real y E contextual lo almacenó sin ocultar al agente"); return {}; }
      ActionCommand command; command.dragBody = true;
      if (phase_ == 0) { if (agent.draggingGuard == static_cast<int>(target_)) Phase(1, "almacenar_con_E"); }
      else command.interact = true;
      return command;
    }
    bool PrepareCapture(WorldState &world, Agent &agent) {
      const auto observer = std::ranges::find_if(world.agents, [&](const Agent &other) { return other.id != agent.id && other.connected && other.alive && !other.incapacitated && !other.hidden; });
      if (observer == world.agents.end()) { Gap("No hay segundo agente operativo para rescate cooperativo"); return false; }
      for (auto &guard : world.guards) if (FreeGuard(guard)) for (const auto direction : std::array<sf::Vector2f, 4>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}}}) {
        const auto position = observer->position + direction * 80.f, enemy = position + direction * 65.f;
        if (!PositionFree(world, agent, position, observer->elevation, observer->supportId)) continue;
        const auto support = WorldGeometry::FindSupport(world, enemy, observer->elevation, observer->supportId, GameplayConfig::AgentRadius);
        if (!support || WorldGeometry::PositionBlocked(world, enemy, GameplayConfig::AgentRadius, support->elevation, CombatConfig::StandingHeight, support->id) ||
            WorldGeometry::RayOccluded(world, {position.x, position.y, observer->elevation + CombatConfig::StandingWeaponHeight}, {enemy.x, enemy.y, support->elevation + CombatConfig::StandingWeaponHeight})) continue;
        if (!Place(world, agent, position, observer->elevation, observer->supportId, -direction, "Exposición inicial a una patrulla real, con rescatador cercano")) return false;
        setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"guard", guard.id}, {"reason", "Patrulla real colocada con línea visual; no se fuerza alerta, daño ni incapacitación"}, {"from", {guard.position.x, guard.position.y, guard.elevation}}, {"to", {enemy.x, enemy.y, support->elevation}}, {"support", support->id}});
        guard.position = enemy; guard.elevation = support->elevation; guard.supportId = support->id; guard.facing = -direction; target_ = guard.id;
        return true;
      }
      Gap("No se encontró un encuentro de captura/rescate con línea visual y apoyo libre"); return false;
    }
    ActionCommand CaptureRescue(WorldState &world, Agent &agent) {
      if (agent.incapacitated) { seen_ = true; stage_ = Name(Current()) + "/rescate_por_cliente"; }
      else if (seen_) {
        const bool revived = std::ranges::any_of(world.events, [&](const MissionEvent &event) { return event.kind == EventKind::AgentRevived && event.target == host_ && event.actor != host_ && event.actor != 0; });
        if (revived && agent.lifeState == LifeState::Alive) Finish(world, "La IA causó incapacitación real y otro jugador aplicó E para reanimar por el transporte LAN");
      }
      return {};
    }
    bool PrepareFatalPush(WorldState &world, Agent &agent) {
      for (const auto &surface : world.walkSurfaces) if (surface.enabled && surface.slope.lengthSquared() == 0.f && surface.elevation >= CqcConfig::FatalPushFallHeight && surface.kind != WalkSurfaceKind::ElevatorCabin) {
        const auto center = surface.bounds.position + surface.bounds.size * 0.5f;
        const auto end = surface.bounds.position + surface.bounds.size;
        for (const auto direction : std::array<sf::Vector2f, 4>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}}}) {
          auto enemy = center;
          if (direction.x) enemy.x = direction.x > 0.f ? end.x - 16.f : surface.bounds.position.x + 16.f;
          else enemy.y = direction.y > 0.f ? end.y - 16.f : surface.bounds.position.y + 16.f;
          const auto position = enemy - direction * CqcConfig::GripSeparation, falling = enemy + direction * CqcConfig::PushDistance;
          const auto landing = ForcedFalls::Landing(world, falling, surface.elevation);
          if (!landing.id || surface.elevation - landing.elevation < CqcConfig::FatalPushFallHeight || !PositionFree(world, agent, position, surface.elevation, surface.id)) continue;
          for (auto &guard : world.guards) if (FreeGuard(guard)) {
            auto probeWorld = world; auto &probe = *FindAgent(probeWorld, agent.id); auto &target = *FindGuard(probeWorld, guard.id);
            probe.position = position; probe.elevation = surface.elevation; probe.supportId = surface.id; probe.facing = direction;
            target.position = enemy; target.elevation = surface.elevation; target.supportId = surface.id;
            const auto selected = Simulation::FindCqcTarget(probeWorld, probe, Simulation::CqcAction::Push);
            if (!selected || selected->guard != target.id) continue;
            if (!Place(world, agent, position, surface.elevation, surface.id, direction, "Contacto sobre borde real que ofrece caída mortal y barrido físico válido")) return false;
            setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"guard", guard.id}, {"reason", "Patrulla real situada en el borde; caída y muerte se producirán sólo por V y ticks"}, {"from", {guard.position.x, guard.position.y, guard.elevation}}, {"to", {enemy.x, enemy.y, surface.elevation}}, {"support", surface.id}});
            guard.position = enemy; guard.elevation = surface.elevation; guard.supportId = surface.id; guard.facing = direction; target_ = guard.id;
            return true;
          }
        }
      }
      Gap("No hay una patrulla y borde de misión disponibles con barrido de empuje válido hacia un piso inferior"); return false;
    }
    bool PreparePuzzle(WorldState &world, Agent &agent) {
      for (const auto &object : world.objects) if (object.active && !object.completed && object.puzzle.parentDevice == 0 && object.puzzle.kind != PuzzleKind::None && object.puzzle.kind != PuzzleKind::Explosive && object.kind != ObjectKind::Computer && object.kind != ObjectKind::CoopSwitch && object.puzzle.family != PuzzleFamily::Levers && object.puzzle.family != PuzzleFamily::Coordination && (object.kind != ObjectKind::Door || object.locked) && object.puzzle.cooldown <= 0.f && object.puzzle.state != PuzzleState::Solved) {
        if (NearObject(world, agent, object, "Acceso físico a un dispositivo de la misión desplegada real")) { target_ = object.id; return true; }
      }
      Gap("No hay un dispositivo de misión accesible y pendiente para comprobar el puzle"); return false;
    }
    ActionCommand Puzzle(WorldState &world, Agent &agent) {
      auto *object = FindObject(world, target_); if (!object) { Fail("Desapareció el dispositivo del puzle"); return {}; }
      if (Current() == Case::Trap) seen_ = seen_ || object->trap.remaining > 0.f;
      if (object->completed || object->puzzle.state == PuzzleState::Solved) {
        if (Current() == Case::Panel) {
          const auto *camera = FindObject(world, 100);
          if (family_ != PuzzleFamily::Wires || !camera || camera->active || camera->camera.state != CameraState::Disabled || camera->camera.reactivationRemaining <= 0.f) Fail("El panel resuelto no desactivó temporalmente la cámara real");
          else Finish(world, "El cableado del panel desactivó la cámara y programó su reactivación real");
        } else if (Current() == Case::Trap) {
          if (family_ != PuzzleFamily::Wires || !seen_ || object->trap.state != TrapState::Disarmed || object->active || object->trap.remaining >= 0.f) Fail("El cableado no aisló el detonador después de observar su cuenta regresiva");
          else Finish(world, "El cableado detuvo la cuenta regresiva real y dejó la trampa desarmada");
        } else if (Current() == Case::Door) {
          if (family_ != PuzzleFamily::Arrows || !object->open || object->locked || WorldGeometry::BlocksMovement(*object)) Fail("La cerradura resuelta no abrió el paso físico de la puerta");
          else Finish(world, "La secuencia de flechas desbloqueó y abrió la puerta física real");
        } else Finish(world, "El dispositivo real completó su circuito con entradas de puzle autoritativas");
        return {};
      }
      if (phase_ == 0) {
        if (agent.puzzle.target == target_) { family_ = agent.puzzle.family; Phase(1, "circuito_activo"); } else return Use();
      } else if (phase_ == 1) {
        if (agent.puzzle.family == PuzzleFamily::Levers || agent.puzzle.family == PuzzleFamily::Coordination) {
          ActionCommand cancel; cancel.puzzleTarget = target_; cancel.puzzleInput = 0;
          if (agent.puzzle.target) return cancel;
        } else if (agent.puzzle.target == target_) {
          ActionCommand command; command.puzzleTarget = target_;
          if (phaseTime_ < 0.15f) return {};
          phaseTime_ = 0.f;
          if (agent.puzzle.family == PuzzleFamily::Arrows && agent.puzzle.stage < agent.puzzle.sequence.size()) command.puzzleInput = agent.puzzle.sequence[agent.puzzle.stage];
          else if (agent.puzzle.family == PuzzleFamily::Wires) for (unsigned int source = 1; source <= 4; ++source) if (!(agent.puzzle.connectedSources & (1u << (source - 1)))) { command.puzzleInput = WireInput(source, MatchingWirePort(agent.puzzle, source)); break; }
          return command;
        }
        if (agent.puzzle.target == 0 && !object->completed) { Gap("El dispositivo elegido requiere palancas distribuidas/coordinación; no se declara resuelto por este piloto"); }
      }
      return {};
    }
    bool PrepareInspection(WorldState &world, Agent &agent) {
      for (const auto &object : world.objects) if (WorldGeometry::HideoutType(object) == HideoutKind::UnderTable && !object.overturned && !object.lifted && object.occupantAgent == 0) {
        for (const auto direction : std::array<sf::Vector2f, 4>{{{1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f}}}) {
          const auto bounds = WorldGeometry::ObjectFootprint(object);
          const sf::Vector2f position = object.position + sf::Vector2f{direction.x * (bounds.size.x * 0.5f + 22.f), direction.y * (bounds.size.y * 0.5f + 22.f)};
          const auto enemy = position + direction * 80.f;
          if (!PositionFree(world, agent, position, object.elevation, object.supportId) || WorldGeometry::PositionBlocked(world, enemy, GameplayConfig::AgentRadius, object.elevation, CombatConfig::StandingHeight, object.supportId)) continue;
          auto probe = agent; probe.position = position; probe.elevation = object.elevation; probe.supportId = object.supportId;
          if (!Interaction::CanEnterHideout(world, probe, object)) continue;
          const auto *selected = Interaction::FindObject(world, probe);
          if (!selected || selected->id != object.id || Interaction::FindBody(world, probe)) continue;
          for (auto &guard : world.guards) if (FreeGuard(guard)) {
            const auto support = WorldGeometry::FindSupport(world, enemy, object.elevation, object.supportId, GameplayConfig::AgentRadius);
            if (!support || WorldGeometry::RayOccluded(world, {position.x, position.y, object.elevation + CombatConfig::StandingWeaponHeight}, {enemy.x, enemy.y, support->elevation + CombatConfig::StandingWeaponHeight})) continue;
            const auto observer = std::ranges::find_if(world.agents, [&](const Agent &other) { return other.id != agent.id && other.connected && other.alive && !other.incapacitated && !other.hidden; });
            if (observer == world.agents.end()) continue;
            const sf::Vector2f perpendicular{-direction.y, direction.x};
            std::optional<sf::Vector2f> observerPosition;
            std::uint32_t observerSupport = 0;
            for (const auto offset : std::array<sf::Vector2f, 5>{direction * 160.f, direction * 160.f + perpendicular * 64.f, direction * 160.f - perpendicular * 64.f, direction * 200.f + perpendicular * 64.f, direction * 200.f - perpendicular * 64.f}) {
              const auto candidate = position + offset;
              const auto floor = WorldGeometry::FindSupport(world, candidate, object.elevation, object.supportId, GameplayConfig::AgentRadius);
              if (!floor || std::abs(floor->elevation - object.elevation) > LevelConfig::SupportTolerance || (candidate - enemy).length() <= GameplayConfig::CloseVisionDistance || (-direction).dot(Unit(candidate - enemy)) >= 0.f || !PositionFree(world, *observer, candidate, floor->elevation, floor->id, agent.id)) continue;
              observerPosition = candidate; observerSupport = floor->id; break;
            }
            if (!observerPosition) continue;
            if (!Place(world, agent, position, object.elevation, object.supportId, -direction, "Acceso bajo mesa ante una patrulla real con visión inicial libre", false)) return false;
            setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"guard", guard.id}, {"reason", "Testigo colocado con patrulla inicial de un punto; detección y memoria de entrada dependen de su IA"}, {"from", {guard.position.x, guard.position.y, guard.elevation}}, {"to", {enemy.x, enemy.y, support->elevation}}, {"support", support->id}, {"patrolPointsBefore", guard.patrol.size()}, {"initialPatrol", {{enemy.x, enemy.y, support->elevation}}}, {"entryObjectChecked", object.id}, {"entryBodyAbsent", true}});
            setups_.push_back({{"case", Name(Current())}, {"tick", world.tick}, {"actor", observer->id}, {"reason", "Observador detrás del testigo y fuera de su cono; no se altera percepción ni se oculta al aliado"}, {"from", {observer->position.x, observer->position.y, observer->elevation}}, {"to", {observerPosition->x, observerPosition->y, object.elevation}}, {"support", observerSupport}, {"observerOutsideWitnessCone", true}});
            observer->position = *observerPosition; observer->elevation = object.elevation; observer->supportId = observerSupport; observer->facing = Unit(object.position - *observerPosition);
            guard.position = enemy; guard.elevation = support->elevation; guard.supportId = support->id; guard.facing = -direction;
            guard.patrol = {enemy}; guard.patrolElevations = {support->elevation}; guard.waypoint = 0; guard.navigation.clear();
            target_ = object.id; inspector_ = guard.id; return true;
          }
        }
      }
      Gap("No hay mesa y patrulla disponibles con acceso, apoyo y línea visual libres para inspección"); return false;
    }
    ActionCommand Inspection(WorldState &world, Agent &agent) {
      auto *object = FindObject(world, target_); auto *guard = FindGuard(world, inspector_);
      if (!object || !guard) { Fail("Desapareció la mesa o la patrulla testigo"); return {}; }
      const auto *selected = Interaction::FindObject(world, agent);
      const auto *body = Interaction::FindBody(world, agent);
      const auto sightUnit = [](sf::Vector2f value) { const float length = std::hypot(value.x, value.y); return length > 0.0001f ? value / length : sf::Vector2f{}; };
      const auto delta = agent.position - guard->position;
      const float distance = std::hypot(std::hypot(delta.x, delta.y), agent.elevation - guard->elevation);
      const float postureFactor = agent.stance == Stance::Prone ? GameplayConfig::ProneVisionFactor : agent.stance == Stance::Crouched ? GameplayConfig::CrouchedVisionFactor : 1.f;
      const float range = GameplayConfig::VisionDistance * (world.alarm ? GameplayConfig::AlarmVisionMultiplier : 1.f);
      const float cone = sightUnit(guard->facing).dot(sightUnit(delta));
      const bool withinCone = distance <= GameplayConfig::CloseVisionDistance || cone >= GameplayConfig::VisionCosHalfAngle;
      const bool exposed = Visibility::AgentExposed(world, guard->position, agent, 0, guard->elevation + CombatGeometry::HeadCenterHeight(Stance::Standing));
      const bool canSee = agent.alive && !agent.incapacitated && agent.connected && !agent.extracted && distance <= range * postureFactor && withinCone && exposed;
      const auto observe = [&](const std::string &reason) {
        auto observation = Json{{"tick", world.tick}, {"reason", reason}, {"guard", guard->id}, {"guardState", static_cast<int>(guard->state)}, {"observedAgent", guard->observedAgent}, {"knownHideout", guard->knownHideout},
          {"suspicion", guard->suspicion}, {"perceptionLostTime", guard->perceptionLostTime}, {"distance", distance}, {"visionRange", range}, {"postureFactor", postureFactor}, {"cone", cone}, {"withinCone", withinCone}, {"exposed", exposed}, {"canSee", canSee},
          {"guardPosition", {guard->position.x, guard->position.y, guard->elevation}}, {"guardFacing", {guard->facing.x, guard->facing.y}}, {"hostPosition", {agent.position.x, agent.position.y, agent.elevation}},
          {"hideout", agent.hideoutId}, {"hidden", agent.hidden}, {"selectedObject", selected ? selected->id : 0}, {"nearbyBody", body ? body->id : 0}, {"stagger", agent.staggerRemaining}, {"cooldown", agent.actionCooldown}, {"message", world.message}};
        if (phase_ == 2) {
          auto flipped = *object; flipped.overturned = true; flipped.lifted = false; flipped.footprint = {};
          const auto original = WorldGeometry::ObjectFootprint(*object), overturned = WorldGeometry::ObjectFootprint(flipped);
          const auto maximum = overturned.position + overturned.size;
          const auto rect = [](const sf::FloatRect &bounds) { return Json{{"position", {bounds.position.x, bounds.position.y}}, {"size", {bounds.size.x, bounds.size.y}}}; };
          auto navigation = Json::array(); for (const auto point : guard->navigation) navigation.push_back({point.x, point.y});
          observation["navigation"] = {{"path", std::move(navigation)}, {"target", {guard->navigationTarget.x, guard->navigationTarget.y}}, {"timer", guard->navigationTimer}, {"stepTime", guard->stepTime}, {"stateTime", guard->stateTime}, {"stuckTime", guard->navigationStuckTime}, {"failures", guard->navigationFailures}, {"rejectTime", guard->navigationRejectTime}, {"rejectedTarget", {guard->navigationRejectedTarget.x, guard->navigationRejectedTarget.y}}, {"recoveryTarget", {guard->navigationRecoveryTarget.x, guard->navigationRecoveryTarget.y}}};
          observation["inspection"] = {{"time", guard->hideoutInspectionTime}, {"progress", object->inspectionProgress}, {"originalFootprint", rect(original)}, {"overturnedFootprint", rect(overturned)}, {"guardOverlapsOverturned", WorldGeometry::CircleOverlaps(guard->position, GameplayConfig::AgentRadius, overturned)}, {"overturned", object->overturned}, {"lifted", object->lifted}, {"occupantAgent", object->occupantAgent}, {"support", object->supportId}};
          auto candidates = Json::array();
          const float radius = GameplayConfig::AgentRadius + SpatialConfig::CollisionSkin + NavigationConfig::WaypointTolerance;
          for (const auto point : std::array<sf::Vector2f, 4>{{{overturned.position.x - radius, object->position.y}, {maximum.x + radius, object->position.y}, {object->position.x, overturned.position.y - radius}, {object->position.x, maximum.y + radius}}})
            candidates.push_back({{"position", {point.x, point.y}}, {"distance", (point - guard->position).length()}, {"blocked", WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius, object->elevation, CombatConfig::StandingHeight, object->supportId)}, {"pathClear", Interaction::HideoutPathClear(world, *object, point, object->position)}});
          observation["inspectionCandidates"] = std::move(candidates);
          auto corners = Json::array();
          const auto *surface = WorldGeometry::FindSurface(world, object->supportId);
          for (const auto point : {overturned.position, maximum, sf::Vector2f{maximum.x, overturned.position.y}, sf::Vector2f{overturned.position.x, maximum.y}})
            corners.push_back({{"position", {point.x, point.y}}, {"onTableSupport", surface && WorldGeometry::ContainsSupport(*surface, point) && std::abs(WorldGeometry::SurfaceHeight(*surface, point) - object->elevation) <= LevelConfig::SupportTolerance}});
          observation["overturnedSupportCorners"] = std::move(corners);
          const auto overlapsHeight = [&](float elevation, float height) { return WorldGeometry::HeightOverlap(object->elevation, object->elevation + WorldGeometry::ObjectHeight(flipped), elevation, elevation + height); };
          auto blockers = Json::array();
          for (const auto &wall : world.obstacles) if (WorldGeometry::BlocksMovement(wall) && overlapsHeight(wall.elevation, WorldGeometry::ObstacleHeight(wall)) && overturned.findIntersection(wall.bounds)) blockers.push_back({{"kind", "obstacle"}, {"bounds", rect(wall.bounds)}, {"elevation", wall.elevation}});
          for (const auto &other : world.objects) if (other.id != object->id && WorldGeometry::BlocksMovement(other) && overlapsHeight(other.elevation, WorldGeometry::ObjectHeight(other)) && overturned.findIntersection(WorldGeometry::ObjectFootprint(other))) blockers.push_back({{"kind", "object"}, {"id", other.id}, {"bounds", rect(WorldGeometry::ObjectFootprint(other))}, {"elevation", other.elevation}});
          observation["overturnedGeometryBlockers"] = std::move(blockers);
          auto others = Json::array();
          for (const auto &other : world.agents) if (other.id != host_ && other.connected && !other.extracted && (other.position - object->position).length() < 180.f)
            others.push_back({{"kind", "agent"}, {"id", other.id}, {"position", {other.position.x, other.position.y, other.elevation}}, {"overlapsOverturned", overlapsHeight(other.elevation, WorldGeometry::ActorHeight(other)) && WorldGeometry::CircleOverlaps(other.position, GameplayConfig::AgentRadius, overturned)}});
          for (const auto &other : world.guards) if (!other.bodyHidden && other.storedIn == 0 && (other.position - object->position).length() < 180.f)
            others.push_back({{"kind", "guard"}, {"id", other.id}, {"position", {other.position.x, other.position.y, other.elevation}}, {"state", static_cast<int>(other.state)}, {"overlapsOverturned", overlapsHeight(other.elevation, CombatConfig::StandingHeight) && WorldGeometry::CircleOverlaps(other.position, GameplayConfig::AgentRadius, overturned)}});
          observation["nearbyOthers"] = std::move(others);
        }
        inspectionObservations_.push_back(std::move(observation));
      };
      const bool witnessed = guard->state == GuardState::Alert && guard->observedAgent == host_ && canSee;
      // E se procesa antes del enfriamiento de ataque y no depende de la duración de su pose visual.
      const bool ready = !agent.hidden && agent.staggerRemaining <= 0.f &&
        agent.maneuver.kind == ManeuverKind::None && agent.cqcHeldBy == 0 && agent.cqcHoldingGuard == 0 && agent.cqcHoldingAgent == 0 && agent.cqcApproachGuard == 0 && agent.cqcApproachAgent == 0 && agent.cqcAttackTime <= 0.f &&
        !agent.puzzle.target && agent.draggingGuard < 0 && selected && selected->id == target_ && !body && Interaction::CanEnterHideout(world, agent, *object);
      if (phase_ < 2 && elapsed_ > 15.f) { observe("entrada_no_confirmada"); Fail("La inspección no obtuvo una entrada presenciada operativa; consulta inspectionObservations"); return {}; }
      if (phase_ == 0) {
        if (witnessed && ready) { Phase(1, "entrada_presenciada"); observe("pulso_E_con_testigo"); return Use(); }
      } else if (phase_ == 1) {
        if (agent.hideoutId == target_ && guard->knownHideout == target_) { observe("entrada_memorizada_por_testigo"); Phase(2, "inspeccion_ia"); inspectionSampleTime_ = 0.f; }
        else if (agent.hideoutId == target_ && phaseTime_ >= 0.35f) { observe("entrada_sin_memoria_del_testigo"); Fail("El agente entró bajo la mesa sin que el testigo registrara el escondite"); }
        else if (!agent.hidden && phaseTime_ >= 0.35f && witnessed && ready) { issued_ = false; phaseTime_ = 0.f; observe("reintento_E_tras_liberar"); return Use(); }
      } else if (phase_ == 2) {
        if (phaseTime_ >= inspectionSampleTime_) { observe("inspeccion_periodica"); inspectionSampleTime_ = phaseTime_ + 2.f; }
        if (object->inspectionProgress > 0.f) seen_ = true;
        if (seen_ && object->overturned && !agent.hidden && agent.hideoutId == 0 && object->occupantAgent == 0) Finish(world, "La patrulla detectó al host, presenció E, inspeccionó y volcó la mesa expulsando al ocupante");
      }
      return {};
    }
  };
}
