#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include "NoTraceVisibility.h"
#include "NoTraceDistractionGeometry.h"
#include "NoTraceCoordination.h"
#include "Config/NoTraceAudioConfig.h"
#include <bit>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace NoTrace::TacticalState {
  using Json = nlohmann::json;
  inline float Bounded(const Json &j, const char *key, float initial, float maximum) {
    const float value = j.value(key, initial);
    if (!std::isfinite(value) || value < 0.f || value > maximum) throw std::runtime_error("Estado táctico fuera de rango");
    return value;
  }
  inline unsigned int Integer(const Json &j, const char *key, unsigned int initial, unsigned int maximum) {
    const auto value = j.value(key, Json(initial));
    if (!value.is_number_integer() || value < 0 || value > maximum) throw std::runtime_error("Entero táctico inválido");
    return value.get<unsigned int>();
  }
  inline sf::Vector2f Point(const Json &j) {
    if (!j.is_array() || j.size() != 2) throw std::runtime_error("Coordenada táctica inválida");
    sf::Vector2f point{j[0].get<float>(), j[1].get<float>()};
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 100000.f || std::abs(point.y) > 100000.f) throw std::runtime_error("Coordenada táctica no finita");
    return point;
  }
  inline Json Vec(sf::Vector2f p) { return {p.x, p.y}; }
  inline std::vector<std::uint8_t> Sequence(const Json &j) {
    if (!j.is_array() || j.size() > 8) throw std::runtime_error("Secuencia táctica inválida");
    std::vector<std::uint8_t> result;
    for (const auto &value : j) {
      if (!value.is_number_integer() || value < 1 || value > 4) throw std::runtime_error("Dirección o terminal inválido");
      result.push_back(value.get<std::uint8_t>());
    }
    return result;
  }
  inline Json Puzzle(const PuzzleSession &p) {
    return {{"target",p.target},{"kind",static_cast<int>(p.kind)},{"stage",p.stage},{"length",p.length},{"family",static_cast<int>(p.family)},
      {"sequence",p.sequence},{"wireLabels",p.wireLabels},{"connectedSources",p.connectedSources},{"connectedPorts",p.connectedPorts},{"holding",p.holding},{"coordinationProgress",p.coordinationProgress},{"coordinationButton",p.coordinationButton}};
  }
  inline void ReadPuzzle(const Json &j, PuzzleSession &p) {
    p.target = Integer(j,"target",0,1000000); p.kind = static_cast<PuzzleKind>(Integer(j,"kind",0,5));
    p.stage = Integer(j,"stage",0,8); p.length = Integer(j,"length",0,8); p.family = static_cast<PuzzleFamily>(Integer(j,"family",0,4));
    p.sequence = Sequence(j.value("sequence",Json::array()));
    const auto wires = j.value("wireLabels",Json::array({0,0,0,0}));
    if (!wires.is_array() || wires.size()!=4) throw std::runtime_error("Panel de cables inválido");
    unsigned int labels = 0;
    for (unsigned int n=0;n<4;++n) {
      if (!wires[n].is_number_integer() || wires[n]<0 || wires[n]>4) throw std::runtime_error("Etiqueta de cable inválida");
      p.wireLabels[n]=wires[n].get<std::uint8_t>(); if (p.wireLabels[n]) labels |= 1u << (p.wireLabels[n]-1u);
    }
    p.connectedSources=static_cast<std::uint8_t>(Integer(j,"connectedSources",0,15)); p.connectedPorts=static_cast<std::uint8_t>(Integer(j,"connectedPorts",0,15));
    p.holding=j.value("holding",false); p.coordinationProgress=Bounded(j,"coordinationProgress",0.f,10.f);
    p.coordinationButton=Integer(j,"coordinationButton",0,DevicesConfig::CoordinationMaximumButtons);
    if (!p.target) {
      if (p.kind!=PuzzleKind::None || p.stage || p.length || p.family!=PuzzleFamily::Automatic || !p.sequence.empty() || labels || p.connectedSources || p.connectedPorts || p.holding || p.coordinationProgress>0.f || p.coordinationButton) throw std::runtime_error("Puzle sin dispositivo");
      return;
    }
    if (p.kind==PuzzleKind::None || p.family==PuzzleFamily::Automatic || p.stage>p.length || !p.length) throw std::runtime_error("Puzle sin familia o etapas");
    if ((p.family==PuzzleFamily::Coordination) != (p.coordinationButton!=0)) throw std::runtime_error("Control de coordinación incompatible con su familia");
    if (p.family==PuzzleFamily::Wires && (p.length!=4 || labels!=15 || p.connectedSources!=p.connectedPorts)) {
      if (p.length!=4 || labels!=15 || std::popcount(static_cast<unsigned int>(p.connectedSources))!=std::popcount(static_cast<unsigned int>(p.connectedPorts))) throw std::runtime_error("Conexiones incompatibles");
    }
    if ((p.family==PuzzleFamily::Arrows || p.family==PuzzleFamily::Levers) && p.sequence.size()!=p.length) throw std::runtime_error("Secuencia incompleta");
  }
  inline void Write(Json &snapshot, const WorldState &world, PlayerId player, bool filtered) {
    const auto known = [&](const char *list, std::uint32_t id) { return !filtered || std::ranges::any_of(snapshot[list],[&](const Json &v){return v["id"]==id;}); };
    for (auto &j:snapshot["agents"]) {
      const auto &a=*std::ranges::find_if(world.agents,[&](const Agent &v){return v.id==j["id"];});
      j.update({{"lifeState",static_cast<int>(a.lifeState)},{"rescueExpired",a.rescueExpired},{"cqcHeldPressTime",a.cqcHeldPressTime},{"cqcLongPressArmed",a.cqcLongPressArmed},{"aiming",a.aiming},{"pistolSuppressor",a.pistolSuppressor},{"nonLethalSuppressor",a.nonLethalSuppressor},{"pistolSuppressorEquipped",a.pistolSuppressorEquipped},{"nonLethalSuppressorEquipped",a.nonLethalSuppressorEquipped},
        {"transitionFrom",static_cast<int>(a.transitionFrom)},{"transitionTime",a.transitionTime},{"cqcApproachGuard",known("guards",a.cqcApproachGuard)?a.cqcApproachGuard:0},{"cqcApproachAgent",known("agents",a.cqcApproachAgent)?a.cqcApproachAgent:0},{"cqcApproachTime",a.cqcApproachTime},{"cqcApproachSide",a.cqcApproachSide}});
      j["detectionIndicatorRemaining"]=a.id==player?a.detectionIndicatorRemaining:0.f; j["confirmedIndicatorRemaining"]=a.id==player?a.confirmedIndicatorRemaining:0.f;
      const auto &m=a.maneuver;
      j["maneuver"]={{"kind",static_cast<int>(m.kind)},{"phase",static_cast<int>(m.phase)},{"direction",Vec(m.direction)},{"time",m.time},{"cooldown",m.cooldown},{"travelled",m.travelled},{"blocked",m.blocked}};
    }
    for (auto &j:snapshot["guards"]) {
      const auto &g=*std::ranges::find_if(world.guards,[&](const Guard &v){return v.id==j["id"];});
      j["patrolPauseRemaining"]=g.patrolPauseRemaining; j["perceptionLostTime"]=g.perceptionLostTime;
      j["suspicionRemaining"]=g.suspicionRemaining; j["indicatorProgress"]=g.indicatorProgress;
    }
    for (auto &j:snapshot["objects"]) {
      const auto &o=*std::ranges::find_if(world.objects,[&](const Interactable &v){return v.id==j["id"];}); const auto &c=o.camera; const auto &p=o.puzzle;
      const bool clueKnown=!filtered || c.trackedAgent==player || known("agents",c.trackedAgent);
      j["camera"].update({{"lostSightTime",c.lostSightTime},{"perception",static_cast<int>(c.perception)},{"trackedAgent",clueKnown?c.trackedAgent:0},{"lastKnown",Vec(clueKnown?c.lastKnown:o.position)},{"trackingSpeed",c.trackingSpeed},{"searchRemaining",c.searchRemaining},{"suspicionRemaining",c.suspicionRemaining},{"confirmedRemaining",c.confirmedRemaining},{"indicatorProgress",c.indicatorProgress}});
      j["puzzleDevice"].update({{"family",static_cast<int>(p.family)},{"sequenceProgress",p.sequenceProgress},{"coordinationProgress",p.coordinationProgress},{"coordinationMask",p.coordinationMask},{"coordinationReservedMask",p.coordinationReservedMask},{"requiredAgents",p.requiredAgents},{"soloAllowed",p.soloAllowed},{"wireCrossingAllowed",p.wireCrossingAllowed},{"parentDevice",p.parentDevice},{"nodeIndex",p.nodeIndex},{"leverOrder",!filtered?p.leverOrder:std::vector<std::uint8_t>{}}});
    }
    snapshot["decoys"]=Json::array();
    const auto observer=std::ranges::find_if(world.agents,[&](const Agent &a){return a.id==player;});
    for (const auto &d:world.decoys) {
      const float t=d.duration>0.f?d.elapsed/d.duration:1.f; const auto position=d.from+(d.to-d.from)*t;
      if (filtered && d.actor!=player && (observer==world.agents.end() || !Visibility::IsGeometricallyVisible(world,*observer,position,Stance::Prone,d.elevation+DistractionGeometry::DecoyHeight(d)))) continue;
      const bool own=!filtered || d.actor==player;
      snapshot["decoys"].push_back({{"from",Vec(own?d.from:position)},{"to",Vec(own?d.to:position)},{"elapsed",d.elapsed},{"duration",d.duration},{"height",d.height},{"actor",own?d.actor:0},{"blocked",d.blocked},{"elevation",d.elevation}});
    }
  }
  inline void Read(const Json &snapshot, WorldState &world) {
    for (std::size_t i=0;i<world.agents.size();++i) {
      auto &a=world.agents[i]; const auto &j=snapshot["agents"][i];
      a.lifeState=static_cast<LifeState>(Integer(j,"lifeState",0,2)); a.rescueExpired=j.value("rescueExpired",false);
      if (a.lifeState != (!a.alive ? LifeState::Dead : a.incapacitated ? LifeState::Incapacitated : LifeState::Alive) || (a.rescueExpired && a.lifeState!=LifeState::Dead)) throw std::runtime_error("Estado vital contradictorio");
      a.cqcHeldPressTime=Bounded(j,"cqcHeldPressTime",0.f,CqcConfig::LongPressEscape); a.cqcLongPressArmed=j.value("cqcLongPressArmed",false);
      a.aiming=j.value("aiming",false); a.pistolSuppressor=Bounded(j,"pistolSuppressor",100.f,100.f); a.nonLethalSuppressor=Bounded(j,"nonLethalSuppressor",100.f,100.f);
      a.pistolSuppressorEquipped=j.value("pistolSuppressorEquipped",true); a.nonLethalSuppressorEquipped=j.value("nonLethalSuppressorEquipped",true);
      if ((a.pistolSuppressor==0.f && a.pistolSuppressorEquipped) || (a.nonLethalSuppressor==0.f && a.nonLethalSuppressorEquipped)) throw std::runtime_error("Silenciador roto equipado");
      a.transitionFrom=static_cast<Stance>(Integer(j,"transitionFrom",0,2)); a.transitionTime=Bounded(j,"transitionTime",0.f,GameplayConfig::StanceTransitionDuration);
      a.detectionIndicatorRemaining=Bounded(j,"detectionIndicatorRemaining",0.f,GameplayConfig::SuspicionIndicatorDuration); a.confirmedIndicatorRemaining=Bounded(j,"confirmedIndicatorRemaining",0.f,GameplayConfig::ConfirmedIndicatorDuration);
      a.cqcApproachGuard=Integer(j,"cqcApproachGuard",0,1000000); a.cqcApproachAgent=Integer(j,"cqcApproachAgent",0,1000000); a.cqcApproachTime=Bounded(j,"cqcApproachTime",0.f,CqcConfig::ApproachTimeout);
      a.cqcApproachSide=j.value("cqcApproachSide",0); if (a.cqcApproachSide < -1 || a.cqcApproachSide > 1) throw std::runtime_error("Lado CQC inválido");
      if (j.contains("maneuver")) {
        const auto &m=j["maneuver"]; a.maneuver.kind=static_cast<ManeuverKind>(Integer(m,"kind",0,2)); a.maneuver.phase=static_cast<ManeuverPhase>(Integer(m,"phase",0,4));
        a.maneuver.direction=Point(m.at("direction")); a.maneuver.time=Bounded(m,"time",0.f,3.f); a.maneuver.cooldown=Bounded(m,"cooldown",0.f,5.f); a.maneuver.travelled=Bounded(m,"travelled",0.f,500.f); a.maneuver.blocked=m.value("blocked",false);
        if (a.maneuver.kind!=ManeuverKind::None && (a.maneuver.phase==ManeuverPhase::None || std::abs(a.maneuver.direction.length()-1.f)>.01f || a.hidden || a.incapacitated || !a.alive)) throw std::runtime_error("Maniobra inválida");
      }
    }
    for (std::size_t i=0;i<world.guards.size();++i) { auto &g=world.guards[i]; const auto &j=snapshot["guards"][i]; g.patrolPauseRemaining=Bounded(j,"patrolPauseRemaining",0.f,SecurityConfig::PatrolPauseMaximum); g.perceptionLostTime=Bounded(j,"perceptionLostTime",0.f,86400.f); g.suspicionRemaining=Bounded(j,"suspicionRemaining",0.f,GameplayConfig::SuspicionIndicatorDuration); g.indicatorProgress=Bounded(j,"indicatorProgress",0.f,1.f); }
    for (std::size_t i=0;i<world.objects.size();++i) {
      auto &o=world.objects[i]; const auto &j=snapshot["objects"][i];
      if (j.contains("camera")) { const auto &c=j["camera"]; auto &v=o.camera;
        v.lostSightTime=Bounded(c,"lostSightTime",0.f,86400.f); v.perception=static_cast<CameraPerception>(Integer(c,"perception",0,3)); v.trackedAgent=Integer(c,"trackedAgent",0,1000000); v.lastKnown=c.contains("lastKnown")?Point(c["lastKnown"]):o.position;
        v.trackingSpeed=Bounded(c,"trackingSpeed",3.5f,20.f); v.searchRemaining=Bounded(c,"searchRemaining",0.f,10.f); v.suspicionRemaining=Bounded(c,"suspicionRemaining",0.f,5.f); v.confirmedRemaining=Bounded(c,"confirmedRemaining",0.f,10.f); v.indicatorProgress=Bounded(c,"indicatorProgress",0.f,1.f);
      }
      if (j.contains("puzzleDevice")) { const auto &p=j["puzzleDevice"]; auto &v=o.puzzle;
        v.family=static_cast<PuzzleFamily>(Integer(p,"family",0,4)); v.sequenceProgress=Integer(p,"sequenceProgress",0,8); v.coordinationProgress=Bounded(p,"coordinationProgress",0.f,10.f); v.requiredAgents=Integer(p,"requiredAgents",2,4); v.soloAllowed=p.value("soloAllowed",true); v.wireCrossingAllowed=p.value("wireCrossingAllowed",true);
        v.parentDevice=Integer(p,"parentDevice",0,1000000); v.nodeIndex=Integer(p,"nodeIndex",0,4); v.leverOrder=Sequence(p.value("leverOrder",Json::array()));
        v.coordinationMask=static_cast<std::uint8_t>(Integer(p,"coordinationMask",0,15)); v.coordinationReservedMask=static_cast<std::uint8_t>(Integer(p,"coordinationReservedMask",0,15));
        if (!v.requiredAgents || (v.parentDevice && !v.nodeIndex)) throw std::runtime_error("Dispositivo táctico inválido");
        if ((v.coordinationMask & ~v.coordinationReservedMask) || (v.coordinationReservedMask & ~Coordination::FullMask(o)) ||
            ((v.coordinationMask || v.coordinationReservedMask) && v.kind!=PuzzleKind::Coordination && v.family!=PuzzleFamily::Coordination)) throw std::runtime_error("Máscara de controles inválida");
      }
    }
    for (const auto &agent : world.agents) if (agent.puzzle.coordinationButton) {
      const auto panel=std::ranges::find(world.objects,agent.puzzle.target,&Interactable::id);
      if (panel==world.objects.end() || agent.puzzle.coordinationButton>Coordination::ButtonCount(*panel) ||
          !(panel->puzzle.coordinationReservedMask & Coordination::ButtonBit(agent.puzzle.coordinationButton))) throw std::runtime_error("Operador sin control autorizado");
    }
    for (std::size_t i=0;i<world.noises.size();++i) { auto &n=world.noises[i]; const auto &j=snapshot["noises"][i]; n.kind=static_cast<SoundKind>(Integer(j,"kind",4,static_cast<unsigned int>(SoundKind::Count)-1)); n.duration=Bounded(j,"duration",GameplayConfig::NoiseDuration,5.f); if(n.remaining>n.duration) throw std::runtime_error("Ruido con duración incoherente"); }
    if (snapshot.contains("decoys")) {
      if (!snapshot["decoys"].is_array() || snapshot["decoys"].size()>DistractionConfig::MaximumFlyingDecoys) throw std::runtime_error("Demasiados discos en vuelo");
      for (const auto &j:snapshot["decoys"]) { DecoyProjectile d{Point(j.at("from")),Point(j.at("to")),Bounded(j,"elapsed",0.f,3.f),Bounded(j,"duration",0.f,3.f),Bounded(j,"height",0.f,200.f),Integer(j,"actor",0,1000000),j.value("blocked",false)};
        if (!j.at("elevation").is_number()) throw std::runtime_error("Altura de disco no numérica");
        d.elevation = j.at("elevation").get<float>();
        if (!std::isfinite(d.elevation) || d.elevation < LevelConfig::MinimumElevation || d.elevation > LevelConfig::MaximumElevation) throw std::runtime_error("Altura de disco fuera de rango");
        if(d.duration<=0.f || d.elapsed>=d.duration) throw std::runtime_error("Disco sin vuelo válido"); world.decoys.push_back(d);
      }
    }
  }
}
