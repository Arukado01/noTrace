#pragma once

#include "NoTraceVisibility.h"
#include "NoTraceTacticalState.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include "Config/NoTraceTrainingConfig.h"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace NoTrace::StateExtensions {
  using Json = nlohmann::json;
  inline Json Vec(sf::Vector2f p) { return {p.x, p.y}; }
  inline sf::Vector2f Vector(const Json &j) {
    if (!j.is_array() || j.size() != 2) throw std::runtime_error("Vector de extensión inválido");
    sf::Vector2f p{j[0].get<float>(), j[1].get<float>()};
    if (!std::isfinite(p.x) || !std::isfinite(p.y)) throw std::runtime_error("Vector de extensión no finito");
    return p;
  }
  inline float Number(const Json &j, const char *key, float fallback, float lo, float hi) {
    const float v = j.value(key, fallback);
    if (!std::isfinite(v) || v < lo || v > hi) throw std::runtime_error("Magnitud de extensión inválida");
    return v;
  }
  template<class E> E Enum(const Json &j, const char *key, int max) {
    if (!j.at(key).is_number_integer()) throw std::runtime_error("Enumeración de extensión inválida");
    const int n = j.at(key).get<int>();
    if (n < 0 || n > max) throw std::runtime_error("Enumeración de extensión fuera de rango");
    return static_cast<E>(n);
  }
  inline void Write(Json &snapshot, const WorldState &world, PlayerId player, bool filtered) {
    snapshot["mode"] = static_cast<int>(world.mode);
    const auto knownAgent = [&](PlayerId id) { return !filtered || std::ranges::any_of(snapshot["agents"], [id](const Json &j) { return j["id"] == id; }); };
    for (auto &j : snapshot["agents"]) {
      const auto &a = *std::ranges::find_if(world.agents, [&](const Agent &a) { return a.id == j["id"]; });
      j.update({{"cqcHoldingAgent", knownAgent(a.cqcHoldingAgent) ? a.cqcHoldingAgent : 0}, {"cqcHeldBy", knownAgent(a.cqcHeldBy) ? a.cqcHeldBy : 0}, {"friendlyIncapacitated", a.friendlyIncapacitated},
        {"cqcPressPending", a.cqcPressPending}, {"cqcInputDown", a.cqcInputDown}, {"cqcNextKick", a.cqcNextKick},
        {"cqcStrikePending", a.cqcStrikePending}, {"cqcAttackTime", a.cqcAttackTime}});
      j["selectedWeapon"] = static_cast<int>(a.selectedWeapon); j["reloadingWeapon"] = static_cast<int>(a.reloadingWeapon);
      j["lethalMagazine"] = a.lethalMagazine; j["nonLethalMagazine"] = a.nonLethalMagazine; j["reloadRemaining"] = a.reloadRemaining;
      if (!filtered || a.id == player) j["operationLoadout"] = {a.operationLoadout.lethalAmmo, a.operationLoadout.nonLethalAmmo, a.operationLoadout.decoys, a.operationLoadout.tools};
      if (!filtered || a.id == player) j["puzzle"] = TacticalState::Puzzle(a.puzzle);
    }
    for (auto &j : snapshot["guards"]) {
      const auto &g = *std::ranges::find_if(world.guards, [&](const Guard &g) { return g.id == j["id"]; });
      j["trainingDummy"] = g.trainingDummy; j["trainingHits"] = g.trainingHits; j["trainingFeedback"] = g.trainingFeedback;
      if (filtered) continue;
      j["navigationRecovery"] = {{"stuck", g.navigationStuckTime}, {"failures", g.navigationFailures}, {"reject", g.navigationRejectTime},
        {"rejected", Vec(g.navigationRejectedTarget)}, {"target", Vec(g.navigationRecoveryTarget)}};
    }
    for (auto &j : snapshot["objects"]) {
      const auto &o = *std::ranges::find_if(world.objects, [&](const Interactable &o) { return o.id == j["id"]; });
      const auto &c = o.camera; const auto &t = o.trap; const auto &p = o.puzzle;
      j["locked"] = o.locked; j["user"] = !filtered || o.user == player ? o.user : 0;
      j["camera"] = {{"state", static_cast<int>(c.state)}, {"baseAngle", c.baseAngle}, {"sweepAngle", c.sweepAngle},
        {"minAngle", c.minAngle}, {"maxAngle", c.maxAngle}, {"speed", c.speed}, {"pauseRemaining", c.pauseRemaining},
        {"direction", c.direction}, {"range", c.range}, {"cosHalfAngle", c.cosHalfAngle}, {"confirmationTime", c.confirmationTime},
        {"reactivationRemaining", c.reactivationRemaining}, {"controlGroup", c.controlGroup}, {"sweeping", c.sweeping}};
      j["trap"] = {{"state", static_cast<int>(t.state)}, {"triggerRadius", t.triggerRadius}, {"audibleRadius", t.audibleRadius},
        {"blastRadius", t.blastRadius}, {"fuse", t.fuse}, {"remaining", t.remaining}, {"tickRemaining", t.tickRemaining},
        {"damage", t.damage}, {"effectRemaining", t.effectRemaining}};
      j["puzzleDevice"] = {{"kind", static_cast<int>(p.kind)}, {"state", static_cast<int>(p.state)}, {"difficulty", p.difficulty},
        {"cooldown", p.cooldown}, {"intelligence", !filtered || p.state == PuzzleState::Solved ? p.intelligence : ""}};
    }
    TacticalState::Write(snapshot, world, player, filtered);
    const auto observer = std::ranges::find_if(world.agents, [player](const Agent &a) { return a.id == player; });
    snapshot["sounds"] = Json::array();
    for (const auto &s : world.sounds) {
      if (filtered && (observer == world.agents.end() || !Visibility::SoundAudible(world, *observer, s))) continue;
      auto point = s.position;
      const bool hidden = filtered && !Visibility::IsVisible(world, *observer, point);
      if (hidden && s.actor != player) { point.x = std::floor(point.x / AudioConfig::PositionQuantization) * AudioConfig::PositionQuantization; point.y = std::floor(point.y / AudioConfig::PositionQuantization) * AudioConfig::PositionQuantization; }
      snapshot["sounds"].push_back({{"sequence", s.sequence}, {"kind", static_cast<int>(s.kind)}, {"position", Vec(point)},
        {"radius", s.radius}, {"remaining", s.remaining}, {"actor", !filtered || s.actor == player ? s.actor : 0}});
    }
  }
  inline void Read(const Json &snapshot, WorldState &world, PlayerId recipient) {
    const bool filtered = snapshot.value("visibilityFiltered", false);
    world.mode = snapshot.contains("mode") ? Enum<SimulationMode>(snapshot,"mode",1) : SimulationMode::Operation;
    if (world.mode == SimulationMode::Training && world.phase != GamePhase::Base) throw std::runtime_error("Entrenamiento fuera de la sala de espera");
    for (std::size_t i = 0; i < world.agents.size(); ++i) {
      auto &a = world.agents[i]; const auto &j = snapshot.at("agents")[i];
      a.cqcHoldingAgent = j.value("cqcHoldingAgent", PlayerId{0}); a.cqcHeldBy = j.value("cqcHeldBy", PlayerId{0});
      a.friendlyIncapacitated = j.value("friendlyIncapacitated", false); a.cqcPressPending = j.value("cqcPressPending", false);
      a.cqcInputDown = j.value("cqcInputDown", false); a.cqcNextKick = j.value("cqcNextKick", false); a.cqcStrikePending = j.value("cqcStrikePending", false);
      a.cqcAttackTime = Number(j, "cqcAttackTime", 0.f, 0.f, 1.f);
      a.selectedWeapon = j.contains("selectedWeapon") ? Enum<WeaponKind>(j,"selectedWeapon",1) : WeaponKind::Pistol;
      a.reloadingWeapon = j.contains("reloadingWeapon") ? Enum<WeaponKind>(j,"reloadingWeapon",1) : WeaponKind::Pistol;
      a.lethalMagazine = j.value("lethalMagazine", std::min(WeaponConfig::PistolCapacity,a.loadout.lethalAmmo)); a.nonLethalMagazine = j.value("nonLethalMagazine", std::min(WeaponConfig::NonLethalCapacity,a.loadout.nonLethalAmmo));
      a.reloadRemaining = Number(j,"reloadRemaining",0.f,0.f,WeaponConfig::ReloadDuration);
      if (a.lethalMagazine < 0 || a.lethalMagazine > WeaponConfig::PistolCapacity || a.nonLethalMagazine < 0 || a.nonLethalMagazine > WeaponConfig::NonLethalCapacity) throw std::runtime_error("Cargador inválido");
      if (j.contains("operationLoadout")) {
        if (filtered && a.id != recipient) throw std::runtime_error("Equipo de misión ajeno");
        const auto &equipment = j.at("operationLoadout");
        if (!equipment.is_array() || equipment.size() != 4) throw std::runtime_error("Equipo de misión inválido");
        int values[4]; for (int n=0;n<4;++n) {
          const int maximum = n<2 ? GameplayConfig::MaximumAmmo : n==2 ? GameplayConfig::MaximumDecoys : GameplayConfig::MaximumTools;
          if (!equipment[n].is_number_integer() || equipment[n]<0 || equipment[n]>maximum) throw std::runtime_error("Cantidad de equipo inválida");
          values[n]=equipment[n].get<int>();
        }
        a.operationLoadout = {values[0],values[1],values[2],values[3]};
      } else a.operationLoadout = a.loadout;
      if (IsTraining(world) && (!a.alive || a.incapacitated || a.health < 1.f || a.friendlyIncapacitated)) throw std::runtime_error("Agente no protegido en entrenamiento");
      if (a.friendlyIncapacitated && !a.incapacitated) throw std::runtime_error("Incapacitación amistosa incoherente");
      if (j.contains("puzzle")) {
        if (filtered && a.id != recipient) throw std::runtime_error("Puzle privado ajeno");
        TacticalState::ReadPuzzle(j.at("puzzle"), a.puzzle);
      }
    }
    for (std::size_t i = 0; i < world.guards.size(); ++i) {
      auto &g = world.guards[i]; const auto &j = snapshot.at("guards")[i];
      g.trainingDummy = j.value("trainingDummy",false); g.trainingHits = j.value("trainingHits",0u); g.trainingFeedback = Number(j,"trainingFeedback",0.f,0.f,5.f);
      if (g.trainingHits > TrainingConfig::MaximumRecordedHits || g.trainingFeedback > TrainingConfig::DummyFeedbackDuration) throw std::runtime_error("Registro de diana inválido");
      if (g.trainingDummy && (!IsTraining(world) || g.health < 1.f || g.state == GuardState::Dead || g.state == GuardState::Incapacitated)) throw std::runtime_error("Muñeco de entrenamiento inválido");
      if (IsTraining(world) && !g.trainingDummy) throw std::runtime_error("Guardia enemigo en entrenamiento");
      if (j.contains("navigationRecovery")) {
        if (filtered) throw std::runtime_error("Navegación privada en cliente");
        const auto &n = j.at("navigationRecovery"); g.navigationStuckTime = Number(n,"stuck",0.f,0.f,86400.f);
        g.navigationRejectTime = Number(n,"reject",0.f,0.f,86400.f); g.navigationFailures = n.at("failures").get<unsigned int>();
        if (g.navigationFailures > 10000) throw std::runtime_error("Fallos de navegación inválidos");
        g.navigationRejectedTarget = Vector(n.at("rejected")); g.navigationRecoveryTarget = Vector(n.at("target"));
      }
    }
    for (std::size_t i = 0; i < world.objects.size(); ++i) {
      auto &o = world.objects[i]; const auto &j = snapshot.at("objects")[i];
      o.locked = j.value("locked", false); o.user = j.value("user", PlayerId{0});
      if (filtered && o.user != 0 && o.user != recipient) throw std::runtime_error("Operador privado ajeno");
      if (o.locked && o.open) throw std::runtime_error("Puerta cerrada con apertura inválida");
      if (j.contains("camera")) {
        const auto &c = j.at("camera"); auto &v = o.camera; v.state = Enum<CameraState>(c,"state",2);
        v.baseAngle=Number(c,"baseAngle",0.f,-100.f,100.f); v.sweepAngle=Number(c,"sweepAngle",0.f,-7.f,7.f);
        v.minAngle=Number(c,"minAngle",-.65f,-7.f,7.f); v.maxAngle=Number(c,"maxAngle",.65f,-7.f,7.f);
        v.speed=Number(c,"speed",.4f,0.f,10.f); v.pauseRemaining=Number(c,"pauseRemaining",0.f,0.f,60.f);
        v.direction=c.at("direction").get<int>(); v.range=Number(c,"range",240.f,1.f,2000.f);
        v.cosHalfAngle=Number(c,"cosHalfAngle",.8f,-1.f,1.f); v.confirmationTime=Number(c,"confirmationTime",.85f,.05f,60.f);
        v.reactivationRemaining=Number(c,"reactivationRemaining",0.f,0.f,86400.f); v.controlGroup=c.at("controlGroup").get<unsigned int>(); v.sweeping=c.at("sweeping").get<bool>();
        if (v.minAngle>v.maxAngle || std::abs(v.direction)!=1 || v.controlGroup>1000) throw std::runtime_error("Barrido de cámara inválido");
        if (v.sweeping && (v.sweepAngle < v.minAngle || v.sweepAngle > v.maxAngle)) throw std::runtime_error("Orientación de cámara fuera del barrido");
      }
      if (j.contains("trap")) {
        const auto &t=j.at("trap"); auto &v=o.trap; v.state=Enum<TrapState>(t,"state",3);
        v.triggerRadius=Number(t,"triggerRadius",58.f,0.f,1000.f); v.audibleRadius=Number(t,"audibleRadius",180.f,0.f,2000.f);
        v.blastRadius=Number(t,"blastRadius",120.f,1.f,2000.f); v.fuse=Number(t,"fuse",5.f,.1f,120.f);
        v.remaining=Number(t,"remaining",-1.f,-1.f,120.f); v.tickRemaining=Number(t,"tickRemaining",0.f,0.f,60.f);
        v.damage=Number(t,"damage",110.f,0.f,1000.f); v.effectRemaining=Number(t,"effectRemaining",0.f,0.f,5.f);
      }
      if (j.contains("puzzleDevice")) {
        const auto &p=j.at("puzzleDevice"); o.puzzle.kind=Enum<PuzzleKind>(p,"kind",5); o.puzzle.state=Enum<PuzzleState>(p,"state",3);
        o.puzzle.difficulty=p.at("difficulty").get<unsigned int>(); o.puzzle.cooldown=Number(p,"cooldown",0.f,0.f,60.f);
        o.puzzle.intelligence=p.at("intelligence").get<std::string>();
        if (o.puzzle.difficulty>8 || o.puzzle.intelligence.size()>1024 || (filtered && o.puzzle.state!=PuzzleState::Solved && !o.puzzle.intelligence.empty())) throw std::runtime_error("Datos de puzle inválidos o privados");
      }
    }
    TacticalState::Read(snapshot, world);
    for (const auto &a : world.agents) {
      if ((a.cqcHeldBy && (a.cqcHoldingAgent || a.cqcHoldingGuard)) || (a.cqcStrikePending && a.cqcAttackTime <= 0.f)) throw std::runtime_error("Estado CQC incompatible");
      if (a.cqcHoldingAgent && a.cqcHoldingGuard) throw std::runtime_error("Dos objetivos CQC simultáneos");
      if (a.cqcHoldingAgent) {
        const auto target=std::ranges::find_if(world.agents,[&](const Agent &v){return v.id==a.cqcHoldingAgent;});
        if (target==world.agents.end() || target->id==a.id || target->cqcHeldBy!=a.id || !a.alive || a.incapacitated || !target->alive || target->incapacitated || (a.position-target->position).length()>GameplayConfig::CqcDistance*GameplayConfig::CqcMaximumStretch) throw std::runtime_error("Agarre amistoso inválido");
      }
      if (a.cqcHeldBy && std::ranges::none_of(world.agents,[&](const Agent &v){return v.id==a.cqcHeldBy && v.cqcHoldingAgent==a.id;})) throw std::runtime_error("Sujeción amistosa huérfana");
      if (a.puzzle.target) {
        const auto target=std::ranges::find_if(world.objects,[&](const Interactable &v){return v.id==a.puzzle.target;});
        if (!a.alive || a.incapacitated || target==world.objects.end() || target->puzzle.kind!=a.puzzle.kind || target->puzzle.state!=PuzzleState::InUse || (a.puzzle.family!=PuzzleFamily::Coordination && target->user!=a.id)) throw std::runtime_error("Sesión de puzle huérfana");
      }
    }
    if (snapshot.contains("sounds")) {
      const auto &list=snapshot.at("sounds"); if (!list.is_array() || list.size()>AudioConfig::MaximumCues) throw std::runtime_error("Demasiadas señales de audio");
      std::uint64_t previous=0;
      for (const auto &j:list) {
        SoundEvent s{j.at("sequence").get<std::uint64_t>(),Enum<SoundKind>(j,"kind",25),Vector(j.at("position")),Number(j,"radius",0.f,0.f,10000.f),Number(j,"remaining",0.f,0.f,AudioConfig::CueLifetime),j.value("actor",PlayerId{0})};
        if (s.sequence<=previous || (filtered && s.actor!=0 && s.actor!=recipient)) throw std::runtime_error("Señal de audio inválida");
        previous=s.sequence; world.sounds.push_back(s);
      }
    }
  }
}
