#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceWorldGeometry.h"
#include "Config/NoTraceManeuverConfig.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  int checks = 0;
  void Require(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
  WorldState &Fixture(Simulation &simulation, bool training = false) {
    simulation.ResetBase(); if (!training) simulation.Deploy(17);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    TestFixture::LegacyPlane(world);
    world.phase = training ? GamePhase::Base : GamePhase::Mission; world.mode = training ? SimulationMode::Training : SimulationMode::Operation;
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.noises.clear(); world.sounds.clear(); world.shots.clear(); world.events.clear(); world.securityEntries.clear();
    world.agents[0].position = {150.f, 300.f}; world.agents[0].facing = {1.f, 0.f}; world.agents[0].privateOrder = PrivateOrder::None;
    return world;
  }
  void Tick(Simulation &simulation, ActionCommand command = {}) { simulation.Step(0.1f, {{1, command}}); }
  ActionCommand Run() { ActionCommand command; command.run = true; command.moveX = 1.f; return command; }
  void StartDive(Simulation &simulation) { Tick(simulation, Run()); auto command = Run(); command.dive = true; Tick(simulation, command); }
  void PhasesAndFinalStance() {
    Simulation simulation, replica; auto &world = Fixture(simulation);
    StartDive(simulation);
    const auto start = world.agents[0].position;
    Require(world.agents[0].maneuver.kind == ManeuverKind::Dive && world.agents[0].maneuver.phase == ManeuverPhase::Start, "Correr y pulsar zambullida no inicia su fase de preparación");
    bool travel = false, land = false, recovery = false;
    std::string error;
    for (int frame = 0; frame < 10 && world.agents[0].maneuver.kind != ManeuverKind::None; ++frame) {
      const auto previous = world.agents[0].position;
      ActionCommand conflicting = Run(); conflicting.fireSelected = true; conflicting.cqc = true; conflicting.cqcHeld = true;
      Tick(simulation, conflicting);
      const auto &motion = world.agents[0].maneuver;
      travel |= motion.phase == ManeuverPhase::Travel; land |= motion.phase == ManeuverPhase::Land; recovery |= motion.phase == ManeuverPhase::Recover;
      Require((world.agents[0].position - previous).length() <= ManeuverConfig::DiveSpeed * 0.1f + 0.01f, "La zambullida cambia de posición como un teletransporte");
      Require(replica.LoadSnapshot(simulation.SnapshotFor(1, true), error), "El cliente rechaza una fase autoritativa de zambullida");
    }
    Require(travel && land && recovery && world.agents[0].stance == Stance::Prone && world.agents[0].maneuver.kind == ManeuverKind::None, "La zambullida omite fases o no termina tumbada");
    Require(std::abs((world.agents[0].position - start).length() - ManeuverConfig::DiveSpeed * ManeuverConfig::DiveTravel) < 0.1f, "El impulso no desplaza la distancia configurada");
    Require(world.shots.empty() && world.agents[0].cqcHoldingGuard == 0 && world.agents[0].maneuver.cooldown > 0.f, "Disparar o CQC interrumpe el movimiento o evita su recuperación");
    Require(std::ranges::any_of(world.sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::Land; }), "Aterrizar no genera el sonido de contacto correspondiente");
    Require(std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::Noise; }) == 2, "La zambullida no emite exactamente impulso y aterrizaje, o repite ruido cada fotograma");
    auto repeat = Run(); repeat.dive = true; Tick(simulation, repeat);
    Require(world.agents[0].maneuver.kind == ManeuverKind::None && world.agents[0].maneuver.cooldown > 0.f, "Se puede encadenar otra zambullida durante la recuperación");
    Fixture(simulation); world.agents[0].stance = Stance::Crouched;
    ActionCommand roll; roll.roll = true; roll.moveY = 1.f; Tick(simulation, roll);
    const auto rollStart = world.agents[0].position;
    Require(world.agents[0].maneuver.kind == ManeuverKind::Roll && world.agents[0].maneuver.phase == ManeuverPhase::Start, "Agachado no puede iniciar la rodada");
    bool rollTravel = false, rollRecover = false;
    for (int frame = 0; frame < 8 && world.agents[0].maneuver.kind != ManeuverKind::None; ++frame) {
      Tick(simulation); rollTravel |= world.agents[0].maneuver.phase == ManeuverPhase::Travel; rollRecover |= world.agents[0].maneuver.phase == ManeuverPhase::Recover;
    }
    Require(rollTravel && rollRecover && world.agents[0].stance == Stance::Crouched && world.agents[0].maneuver.kind == ManeuverKind::None, "La rodada no recupera la postura agachada con sus fases diferenciadas");
    Require(std::abs((world.agents[0].position - rollStart).length() - ManeuverConfig::RollSpeed * ManeuverConfig::RollTravel) < 0.1f, "La rodada no aplica su desplazamiento físico configurado");
  }
  void InvalidStartsAndCollisions() {
    Simulation simulation; auto &world = Fixture(simulation);
    ActionCommand dive; dive.dive = true; dive.moveX = 1.f; Tick(simulation, dive);
    Require(world.agents[0].maneuver.kind == ManeuverKind::None && world.agents[0].position.x > 150.f, "Zambullirse sin carrera se activa o bloquea el movimiento normal");
    for (const auto stance : {Stance::Standing, Stance::Prone}) {
      Fixture(simulation); world.agents[0].stance = stance; ActionCommand roll; roll.roll = true; Tick(simulation, roll);
      Require(world.agents[0].maneuver.kind == ManeuverKind::None && world.agents[0].stance == stance, "Se inicia rodada desde una postura incompatible");
    }
    Fixture(simulation); world.agents[0].stance = Stance::Crouched; world.agents[0].reloadRemaining = 0.6f;
    ActionCommand roll; roll.roll = true; Tick(simulation, roll);
    Require(world.agents[0].maneuver.kind == ManeuverKind::None, "Se puede rodar durante una recarga");
    for (const bool diving : {false, true}) {
      Fixture(simulation);
      world.obstacles.push_back({{{215.f, 250.f}, {20.f, 140.f}}, CoverKind::High, true, "Pared del impulso"});
      if (diving) StartDive(simulation);
      else { world.agents[0].stance = Stance::Crouched; ActionCommand command; command.roll = true; command.moveX = 1.f; Tick(simulation, command); }
      bool blocked = false;
      for (int frame = 0; frame < 12 && world.agents[0].maneuver.kind != ManeuverKind::None; ++frame) { Tick(simulation); blocked |= world.agents[0].maneuver.blocked; Require(!WorldGeometry::PositionBlocked(world, world.agents[0].position, GameplayConfig::AgentRadius), "Una maniobra penetra la pared durante una fase intermedia"); }
      Require(blocked && world.agents[0].position.x <= 203.01f && world.agents[0].stance == (diving ? Stance::Prone : Stance::Crouched), "La maniobra atraviesa el obstáculo o no ejecuta su recuperación corta");
    }
  }
  void EightGroundDirections() {
    for (int sector = 0; sector < 8; ++sector) {
      Simulation simulation; auto &world = Fixture(simulation); world.agents[0].position = {500.f, 380.f};
      const float angle = sector * std::numbers::pi_v<float> * 0.25f;
      const sf::Vector2f direction{std::cos(angle), std::sin(angle)};
      ActionCommand command; command.run = true; command.moveX = direction.x; command.moveY = direction.y;
      Tick(simulation, command); command.dive = true; Tick(simulation, command);
      const auto origin = world.agents[0].position;
      Require(world.agents[0].maneuver.kind == ManeuverKind::Dive && (world.agents[0].maneuver.direction - direction).length() < 0.01f, "La maniobra pierde la dirección de suelo al iniciar un sector axonométrico");
      for (int frame = 0; frame < 12; ++frame) Tick(simulation);
      const auto displacement = world.agents[0].position - origin;
      Require(std::abs(displacement.dot(direction) - ManeuverConfig::DiveSpeed * ManeuverConfig::DiveTravel) < 0.1f, "Los sectores diagonales cambian la distancia física del impulso");
      Require(std::abs(displacement.x * direction.y - displacement.y * direction.x) < 0.1f && world.agents[0].stance == Stance::Prone, "La proyección visual desvía el movimiento o pierde la postura final");
    }
  }
  void HazardAndTrainingSafety() {
    for (const bool training : {false, true}) {
      Simulation simulation; auto &world = Fixture(simulation, training);
      Interactable bomb{90, ObjectKind::Explosive, {295.f, 300.f}, "Trampa de aterrizaje"};
      bomb.footprint = {16.f, 12.f}; bomb.solid = SpatialOverride::Disabled; bomb.blocksVision = SpatialOverride::Disabled;
      bomb.trap.state = TrapState::Detected; bomb.trap.remaining = 0.55f; bomb.trap.damage = 1000.f;
      world.objects.push_back(bomb); StartDive(simulation);
      for (int frame = 0; frame < 12 && world.phase != GamePhase::Results; ++frame) Tick(simulation);
      Require(world.objects[0].trap.state == TrapState::Detonated, "La zambullida suspende la trampa en lugar de aplicar sus reglas reales");
      if (training) Require(world.agents[0].alive && !world.agents[0].incapacitated && world.agents[0].health >= 1.f && world.phase == GamePhase::Base, "Aterrizar sobre explosivos omite la protección del entrenamiento");
      else Require(world.agents[0].lifeState == LifeState::Dead && !world.agents[0].alive && world.phase == GamePhase::Results, "La maniobra concede invulnerabilidad de entrenamiento a una misión real");
    }
  }
}
int main() {
  try { PhasesAndFinalStance(); InvalidStartsAndCollisions(); EightGroundDirections(); HazardAndTrainingSafety(); std::cout << "PASS maniobras: " << checks << " comprobaciones\n"; return 0; }
  catch (const std::exception &error) { std::cerr << "FAIL maniobras tras " << checks << ": " << error.what() << '\n'; return 1; }
}
