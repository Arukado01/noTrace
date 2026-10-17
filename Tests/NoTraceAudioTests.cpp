#include "NoTraceSimulation.h"
#include "NoTraceVisibility.h"
#include "Config/NoTraceAudioConfig.h"
#include <SFML/Audio/SoundBuffer.hpp>
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
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.sounds.clear(); world.noises.clear();
    auto &agent = world.agents[0]; agent.position = {300.f,300.f}; agent.facing = {1.f,0.f};
    world.noises.push_back({{230.f,300.f},200.f,1.f,1,0});
    world.noises.push_back({{240.f,300.f},200.f,1.f,0,99});
    world.noises.push_back({{900.f,700.f},40.f,1.f,0,98});
    Require(!Visibility::IsVisible(world,agent,{230.f,300.f}) && Visibility::NoiseAudible(world,agent,world.noises[0]), "La niebla oculta el ruido propio");
    world.sounds.push_back({1,SoundKind::TrapTick,{350.f,300.f},180.f,1.f,0});
    world.sounds.push_back({2,SoundKind::Rifle,{900.f,700.f},50.f,1.f,0});
    const auto snapshot = simulation.SnapshotFor(1,true);
    Require(snapshot["noises"].size()==2 && snapshot["noises"][1]["guardId"]==0, "El ruido no se filtra por audición o revela al enemigo");
    Require(snapshot["sounds"].size()==1, "El audio revela una acción fuera de audición");
    std::string error; Require(replica.LoadSnapshot(snapshot,error), "El cliente rechaza señales acústicas válidas");
    auto invalid=snapshot; invalid["sounds"][0]["kind"]=99;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta un sonido inválido");
    invalid=snapshot; invalid["sounds"][0]["remaining"]=-1;
    Require(!replica.LoadSnapshot(invalid,error), "El cliente acepta una duración inválida");
    Require(replica.GetState().sounds.size()==1, "El fallo de carga destruye la réplica anterior");
    std::cout << "PASS audio y percepción acústica: " << checks << " comprobaciones\n";
  } catch (const std::exception &e) { std::cerr << "FAIL audio: " << e.what() << '\n'; return 1; }
}
