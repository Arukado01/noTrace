#include "NoTraceSimulation.h"
#include "NoTraceVisibility.h"
#include "Config/NoTraceAudioConfig.h"
#include <SFML/Audio/SoundBuffer.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
  int checks = 0;
  void Require(bool value, const char *message) { ++checks; if (!value) throw std::runtime_error(message); }
}
int main() {
  try {
    using namespace NoTrace;
    const auto root = std::filesystem::exists("Content") ? std::filesystem::path(".") : std::filesystem::path("..");
    std::set<std::uint64_t> hashes;
    for (std::size_t index = 0; index < AudioConfig::Files.size(); ++index) {
      sf::SoundBuffer buffer;
      Require(buffer.loadFromFile(root / "Content/Sounds" / AudioConfig::File(static_cast<SoundKind>(index))), "Falta un recurso táctico o no es decodificable");
      Require(buffer.getDuration().asSeconds() > .04f && buffer.getDuration().asSeconds() < 2.f, "Duración de recurso inválida");
      Require(buffer.getChannelCount() == 1 && buffer.getSampleRate() == 22050, "Formato PCM incorrecto");
      std::uint64_t hash = 1469598103934665603ull; int peak = 0;
      for (std::uint64_t n = 0; n < buffer.getSampleCount(); ++n) { const int sample = buffer.getSamples()[n]; peak = std::max(peak, std::abs(sample)); hash = (hash ^ static_cast<std::uint16_t>(sample)) * 1099511628211ull; }
      Require(peak > 200 && peak < 32760, "Recurso silencioso o saturado");
      Require(hashes.insert(hash).second, "Dos acciones reutilizan el mismo audio");
    }
    Simulation simulation, replica; simulation.ResetBase(); simulation.Deploy(7);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.sounds.clear(); world.noises.clear(); world.decoys.clear(); world.securityEntries.clear();
    auto &agent = world.agents[0]; agent.position = {300.f,300.f}; agent.facing = {1.f,0.f};
    world.noises.push_back({{230.f,300.f},200.f,1.f,1,0,SoundKind::Knock,1.f});
    world.noises.push_back({{240.f,300.f},200.f,1.f,0,99,SoundKind::Decoy,1.f});
    world.noises.push_back({{900.f,700.f},40.f,1.f,0,98,SoundKind::Rifle,1.f});
    Require(!Visibility::IsVisible(world,agent,{230.f,300.f}) && Visibility::NoiseAudible(world,agent,world.noises[0]), "La niebla oculta el ruido propio");
    world.sounds.push_back({1,SoundKind::TrapTick,{350.f,300.f},180.f,1.f,0});
    world.sounds.push_back({2,SoundKind::Rifle,{900.f,700.f},50.f,1.f,0});
    const auto snapshot = simulation.SnapshotFor(1,true);
    Require(snapshot["noises"].size()==2 && snapshot["noises"][1]["guardId"]==0, "El ruido no se filtra por audición o revela al enemigo");
    Require(snapshot["noises"][0]["kind"]==static_cast<int>(SoundKind::Knock) && snapshot["noises"][1]["kind"]==static_cast<int>(SoundKind::Decoy) && snapshot["noises"][0]["duration"]==1.f, "El evento acústico pierde tipo y duración al replicarse");
    Require(snapshot["noises"][0]["position"]==nlohmann::json::array({230.f,300.f}) && snapshot["noises"][1]["position"]!=nlohmann::json::array({240.f,300.f}), "La señal propia se desplaza o la ajena revela una ubicación oculta exacta");
    Require(snapshot["sounds"].size()==1, "El audio revela una acción fuera de audición");
    std::string error; Require(replica.LoadSnapshot(snapshot,error), "El cliente rechaza señales acústicas válidas");
    auto invalid=snapshot; invalid["sounds"][0]["kind"]=99;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta un sonido inválido");
    invalid=snapshot; invalid["sounds"][0]["remaining"]=-1;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta una duración inválida");
    Require(replica.GetState().sounds.size()==1, "El fallo de carga destruye la réplica anterior");
    invalid=snapshot; invalid["noises"][0]["kind"]=99;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta un tipo de ruido que no corresponde a ninguna acción");
    invalid=snapshot; invalid["noises"][0]["duration"]=0.5f;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta un ruido cuya duración total es menor que el tiempo restante");
    struct StepCase { Stance stance; bool run; float radius; };
    const std::array<StepCase,5> steps{{{Stance::Standing,false,GameplayConfig::WalkingNoiseRadius}, {Stance::Standing,true,GameplayConfig::RunningNoiseRadius},
      {Stance::Crouched,false,GameplayConfig::CrouchedNoiseRadius}, {Stance::Crouched,true,GameplayConfig::CrouchedRunningNoiseRadius}, {Stance::Prone,false,GameplayConfig::ProneNoiseRadius}}};
    for (const auto &step : steps) {
      Simulation moving; moving.ResetBase(); moving.Deploy(4); auto &state=const_cast<WorldState&>(moving.GetState());
      state.obstacles.clear(); state.objects.clear(); state.guards.clear(); state.sounds.clear(); state.noises.clear(); state.decoys.clear(); state.securityEntries.clear();
      state.agents[0].position={100.f,500.f}; state.agents[0].stance=step.stance;
      ActionCommand command; command.moveX=1.f; command.run=step.run;
      for(int frame=0;frame<4;++frame) moving.Step(0.1f,{{1,command}});
      Require(!state.noises.empty() && state.noises.back().radius==step.radius && step.radius>0.f, "Una postura o carrera elimina el ruido real o usa el mismo alcance para cualquier movimiento");
      Require(state.noises.back().position==state.agents[0].position && state.noises.back().actor==1 && state.noises.back().kind==SoundKind::StepHard && state.noises.back().duration==GameplayConfig::NoiseDuration, "Los pasos no conservan origen, emisor, tipo y duración de la simulación");
    }
    std::cout << "PASS audio y percepción acústica: " << checks << " comprobaciones\n";
  } catch (const std::exception &e) { std::cerr << "FAIL audio: " << e.what() << '\n'; return 1; }
}
