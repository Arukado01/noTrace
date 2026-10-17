#include "NoTraceSession.h"
#include "Config/NoTraceGameplayConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceWorldGeometry.h"
#include "Config/NoTraceDistractionConfig.h"
#include "Config/NoTraceVisibilityConfig.h"
#include "Config/NoTraceManeuverConfig.h"
#include "Config/NoTraceWeaponConfig.h"

#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/System/Time.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  using Json = nlohmann::json;
  const char *currentCase = "inicio";

  void Require(bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(std::string(message) + " [" + currentCase + "]");
    }
  }

  void Until(const std::function<bool()> &condition, const std::function<void()> &tick) {
    sf::Clock timeout;
    while (!condition()) {
      Require(timeout.getElapsedTime().asSeconds() < 3.f, "Tiempo agotado en prueba de sesión");
      tick();
      sf::sleep(sf::milliseconds(1));
    }
  }

  const Agent &FindAgent(const WorldState &world, PlayerId id) {
    const auto found = std::ranges::find_if(world.agents, [id](const Agent &agent) { return agent.id == id; });
    Require(found != world.agents.end(), "No se encontró al agente esperado");
    return *found;
  }
  bool HasAgent(const WorldState &world, PlayerId id) {
    return std::ranges::any_of(world.agents, [id](const Agent &agent) { return agent.id == id; });
  }

  float Distance(sf::Vector2f first, sf::Vector2f second) {
    return std::hypot(first.x - second.x, first.y - second.y);
  }

  struct Pair {
    NetworkManager hostNetwork;
    NetworkManager clientNetwork;
    RandomManager hostRandom;
    RandomManager clientRandom;
    Session host{hostNetwork, hostRandom};
    Session client{clientNetwork, clientRandom};

    Pair() {
      Require(host.Host("Anfitrión", 0), "No se pudo crear sesión en puerto efímero");
      Require(client.Join("127.0.0.1", hostNetwork.GetPort(), "Operador"), "No se pudo iniciar cliente loopback");
      Until([&] { return client.IsConnected() && HasAgent(client.GetWorld(), client.GetLocalPlayerId()) && client.GetLobbyPlayers().size() == 2 && host.GetLobbyPlayers().size() == 2; },
            [&] { Tick(); });
      Require(host.GetLocalPlayerId() == 1 && client.GetLocalPlayerId() == 2, "El anfitrión no asignó las identidades esperadas");
    }

    void Tick(bool hostPaused = false, bool clientPaused = false) {
      host.Tick(0.01f, hostPaused);
      client.Tick(0.01f, clientPaused);
    }

    void Advance(int frames, bool hostPaused = false, bool clientPaused = false) {
      for (int frame = 0; frame < frames; ++frame) {
        Tick(hostPaused, clientPaused);
        sf::sleep(sf::milliseconds(1));
      }
    }

    void Deploy() {
      host.SetReady(true);
      client.SetReady(true);
      const auto tick = [&] { Tick(); Require(client.IsConnected(), client.GetStatus().c_str()); };
      Until([&] { return host.CanDeploy(); }, tick);
      host.Deploy();
      Until([&] { return client.GetWorld().phase == GamePhase::Mission; }, tick);
    }
  };

  struct RawPeer {
    NetworkManager hostNetwork;
    NetworkManager clientNetwork;
    RandomManager random;
    Session host{hostNetwork, random};
    std::uint32_t epoch = 0;
    PlayerId id = 0;
    bool rejected = false;
    bool disconnected = false;
    std::uint32_t version = GameplayConfig::ProtocolVersion;
    Json snapshot;

    explicit RawPeer(bool deployFirst = false, std::uint32_t protocol = GameplayConfig::ProtocolVersion) : version(protocol) {
      Require(host.Host("Anfitrión", 0), "No se pudo crear anfitrión para protocolo");
      if (deployFirst) {
        host.SetReady(true);
        host.Deploy();
      }
      Require(clientNetwork.Join("127.0.0.1", hostNetwork.GetPort()), "No se pudo crear cliente de protocolo");
      Until([&] { return id != 0 || rejected || disconnected; }, [&] { Tick(); });
    }

    void Tick(float dt = 0.01f) {
      host.Tick(dt);
      clientNetwork.Update();
      for (const auto &event : clientNetwork.DrainEvents()) {
        if (event.type == NetworkEventType::Connected) {
          Require(clientNetwork.Send(NetworkConfig::HostId, Json{{"type", "hello"}, {"version", version}, {"name", "Cliente crudo"}}.dump()),
                  "No se pudo enviar saludo de protocolo");
        } else if (event.type == NetworkEventType::Message) {
          const auto message = Json::parse(event.payload);
          if (message.at("type") == "welcome") {
            id = message.at("player").get<PlayerId>();
            epoch = message.at("epoch").get<std::uint32_t>();
          } else if (message.at("type") == "reject") {
            rejected = true;
          } else if (message.at("type") == "snapshot") {
            epoch = message.at("epoch").get<std::uint32_t>();
            snapshot = message.at("world");
          }
        } else if (event.type == NetworkEventType::Disconnected || event.type == NetworkEventType::Error) {
          disconnected = true;
        }
      }
    }

    void Advance(int frames) {
      for (int frame = 0; frame < frames; ++frame) {
        Tick();
        sf::sleep(sf::milliseconds(1));
      }
    }

    void Command(std::uint64_t sequence, float x, float y, std::uint32_t flags = 0, bool oldEpoch = false) {
      const Json message{{"type", "command"}, {"seq", sequence}, {"epoch", oldEpoch ? epoch + 1 : epoch},
                         {"move", {x, y}}, {"aim", {0, 0}}, {"slope", 0.f}, {"decoyDistance", 340.f}, {"flags", flags}, {"player", 1},
                         {"objectives", 99}, {"score", 999999}, {"guards", Json::array()}};
      Require(clientNetwork.Send(NetworkConfig::HostId, message.dump()), "No se pudo enviar mando de protocolo");
    }

    void PuzzleCommand(const Json &choice, const Json &target) {
      const Json message{{"type", "command"}, {"seq", 1}, {"epoch", epoch}, {"move", {0, 0}}, {"aim", {1, 0}},
        {"slope", 0.f}, {"decoyDistance", 100.f}, {"flags", 0}, {"puzzleInput", choice}, {"puzzleTarget", target}};
      Require(clientNetwork.Send(NetworkConfig::HostId, message.dump()), "No se pudo enviar mando de puzle");
    }
  };

  void SoloPauseAndFiniteInput() {
    NetworkManager network;
    RandomManager random;
    Session session(network, random);
    session.StartSolo("Agente");
    const auto before = FindAgent(session.GetWorld(), 1).position;
    ActionCommand command;
    command.moveX = std::numeric_limits<float>::quiet_NaN();
    command.moveY = std::numeric_limits<float>::infinity();
    session.Submit(command);
    session.Tick(0.1f);
    Require(Distance(before, FindAgent(session.GetWorld(), 1).position) < 0.01f, "Entrada no finita movió al agente");
    command.moveX = 1.f;
    command.moveY = 0.f;
    session.Submit(command);
    const auto tick = session.GetWorld().tick;
    session.Tick(0.1f, true);
    Require(session.GetWorld().tick == tick, "La pausa individual avanzó la simulación");
    Require(Distance(before, FindAgent(session.GetWorld(), 1).position) < 0.01f, "La pausa individual conservó movimiento");
    Require(session.CanDeploy(), "La operación individual no puede desplegar");
    session.Deploy();
    Require(session.GetWorld().phase == GamePhase::Mission, "El despliegue individual falló");
    session.ReturnToBase();
    Require(session.GetWorld().phase == GamePhase::Base, "No se regresó a la base individual");
  }

  void LobbyAndAuthority() {
    Pair pair;
    Require(!pair.host.CanDeploy(), "La sala permitió desplegar sin preparación");
    pair.host.SetReady(true);
    pair.host.Deploy();
    Require(pair.host.GetWorld().phase == GamePhase::Base, "El anfitrión ignoró un agente sin preparar");
    const auto contract = pair.host.GetWorld().contract;
    pair.client.SetContract(ContractId::DoubleCross);
    pair.client.Deploy();
    pair.Advance(8);
    Require(pair.host.GetWorld().contract == contract && pair.host.GetWorld().phase == GamePhase::Base,
            "El cliente cambió contrato o inició una misión");
    pair.host.SetContract(ContractId::DoubleCross);
    pair.client.SetLoadout({GameplayConfig::MaximumAmmo, GameplayConfig::MaximumAmmo, GameplayConfig::MaximumDecoys, GameplayConfig::MaximumTools});
    currentCase = "LobbyAndAuthority/equipo confirmado";
    Until([&] { return FindAgent(pair.host.GetWorld(), 2).operationLoadout.lethalAmmo == GameplayConfig::MaximumAmmo; }, [&] { pair.Tick(); Require(pair.client.IsConnected(), pair.client.GetStatus().c_str()); });
    Require(!pair.host.CanDeploy(), "El cambio de contrato/equipo no invalidó preparación");
    currentCase = "LobbyAndAuthority/despliegue";
    pair.Deploy();
    Require(pair.host.GetWorld().contract == ContractId::DoubleCross && pair.client.GetWorld().contract == ContractId::DoubleCross,
            "Contrato no replicado");
    pair.client.ReturnToBase();
    pair.Advance(4);
    Require(pair.host.GetWorld().phase == GamePhase::Mission, "El cliente devolvió la misión a la base");
    pair.host.ReturnToBase();
    currentCase = "LobbyAndAuthority/regreso a la base";
    Until([&] { return pair.client.GetWorld().phase == GamePhase::Base; }, [&] { pair.Tick(); Require(pair.client.IsConnected(), pair.client.GetStatus().c_str()); });
  }

  void PrivateSnapshotAndClientDoesNotSimulate() {
    Pair pair;
    pair.host.SetContract(ContractId::DoubleCross);
    pair.Deploy();
    for (const auto &agent : pair.client.GetWorld().agents) {
      if (agent.id == pair.client.GetLocalPlayerId()) {
        Require(agent.privateOrder == FindAgent(pair.host.GetWorld(), agent.id).privateOrder && agent.privateOrder != PrivateOrder::None,
                "El cliente no recibió su orden privada");
      } else {
        Require(agent.privateOrder == PrivateOrder::None && !agent.privateComplete, "Se filtró una orden privada ajena");
      }
    }
    pair.client.Tick(0.f);
    pair.client.Tick(0.f);
    const auto tick = pair.client.GetWorld().tick;
    const auto position = FindAgent(pair.client.GetWorld(), 2).position;
    ActionCommand command;
    command.moveX = 1.f;
    pair.client.Submit(command);
    pair.client.Tick(0.2f);
    Require(pair.client.GetWorld().tick == tick && Distance(position, FindAgent(pair.client.GetWorld(), 2).position) < 0.01f,
            "El cliente simuló un mundo sin una instantánea del anfitrión");
  }

  void PausedHostStillRunsRemoteAgent() {
    Pair pair;
    const auto hostBefore = FindAgent(pair.host.GetWorld(), 1).position;
    const auto clientBefore = FindAgent(pair.host.GetWorld(), 2).position;
    const auto tick = pair.host.GetWorld().tick;
    ActionCommand right;
    right.moveX = 1.f;
    pair.host.Submit(right);
    pair.client.Submit(right);
    pair.Advance(15, true);
    Require(pair.host.GetWorld().tick > tick, "La pausa local del anfitrión congeló LAN");
    Require(Distance(hostBefore, FindAgent(pair.host.GetWorld(), 1).position) < 0.01f, "El anfitrión pausado siguió caminando");
    Require(FindAgent(pair.host.GetWorld(), 2).position.x > clientBefore.x + 3.f, "La pausa del anfitrión bloqueó al cliente");
  }

  void SlowClientKeepsLobbyAndPhaseChanges() {
    Pair pair;
    sf::Clock hostFrame, clientFrame;
    const auto tick = [&] {
      pair.host.Tick(hostFrame.restart().asSeconds(), true);
      if (clientFrame.getElapsedTime().asSeconds() >= 0.125f) {
        pair.client.Tick(clientFrame.restart().asSeconds(), true);
      }
      Require(pair.host.GetLobbyPlayers().size() == 2 && pair.client.IsConnected(),
              "Un cliente de ocho actualizaciones por segundo perdió la sala LAN");
      sf::sleep(sf::milliseconds(5));
    };
    const auto advance = [&](float seconds) {
      sf::Clock duration;
      while (duration.getElapsedTime().asSeconds() < seconds) tick();
    };
    const auto before = pair.client.GetWorld().tick;
    currentCase = "SlowClient/base sostenida";
    advance(4.f);
    Require(pair.client.GetWorld().tick > before, "El cliente lento conservó conexión sin recibir estados nuevos");
    pair.host.SetReady(true);
    pair.client.SetReady(true);
    Until([&] { return pair.host.CanDeploy(); }, tick);
    pair.host.Deploy();
    currentCase = "SlowClient/despliegue";
    Until([&] { return pair.client.GetWorld().phase == GamePhase::Mission; }, tick);
    Require(pair.client.GetWorld().seed == pair.host.GetWorld().seed, "El despliegue lento perdió la semilla autoritativa");
    advance(1.f);
    const auto missionTick = pair.client.GetWorld().tick;
    Require(pair.host.RestartOperation(), "No se pudo reiniciar la misión con un cliente lento");
    currentCase = "SlowClient/reinicio";
    Until([&] { return pair.client.GetWorld().tick < missionTick; }, tick);
    pair.host.ReturnToBase();
    currentCase = "SlowClient/regreso a base";
    Until([&] { return pair.client.GetWorld().phase == GamePhase::Base; }, tick);
    Require(!pair.host.CanDeploy() && std::ranges::none_of(pair.client.GetLobbyPlayers(), [](const LobbyPlayer &player) { return player.ready; }),
            "La vuelta lenta a base conservó la preparación anterior");
    advance(5.f);
  }

  void SequenceTimeoutIdentityAndPulses() {
    RawPeer pair;
    Require(pair.id == 2, "Cliente de protocolo no asignado");
    const auto hostBefore = FindAgent(pair.host.GetWorld(), 1).position;
    const auto clientBefore = FindAgent(pair.host.GetWorld(), 2).position;
    pair.Command(10, 100.f, 100.f, 2);
    pair.Advance(8);
    const auto first = FindAgent(pair.host.GetWorld(), 2).position;
    Require(Distance(first, clientBefore) > 0.f && Distance(first, clientBefore) <= GameplayConfig::WalkSpeed * 0.1f,
            "No se limitó la velocidad diagonal del cliente");
    Require(FindAgent(pair.host.GetWorld(), 2).stance == Stance::Crouched, "El pulso agacharse se repitió o no se aplicó");
    pair.Command(10, -1.f, 0.f);
    pair.Command(9, -1.f, 0.f);
    pair.Command(11, -1.f, 0.f, 0, true);
    pair.Advance(6);
    Require(FindAgent(pair.host.GetWorld(), 2).position.x >= first.x, "Un mando duplicado/antiguo alteró la dirección");
    Require(Distance(hostBefore, FindAgent(pair.host.GetWorld(), 1).position) < 0.01f && pair.host.GetWorld().objectives == 0,
            "El cliente suplantó actor o modificó objetivos autoritativos");
    pair.Advance(40);
    const auto stopped = FindAgent(pair.host.GetWorld(), 2).position;
    pair.Advance(25);
    Require(Distance(stopped, FindAgent(pair.host.GetWorld(), 2).position) < 0.01f,
            "Un mando caducado siguió moviendo al agente");
  }

  void DisconnectAndRejoin() {
    Pair pair;
    pair.Deploy();
    pair.client.Stop();
    Until([&] { return pair.host.GetLobbyPlayers().size() == 1; }, [&] { pair.host.Tick(0.01f); });
    Require(!FindAgent(pair.host.GetWorld(), 2).connected, "El agente desconectado sigue activo en misión");
    pair.host.ReturnToBase();
    Require(pair.client.Join("127.0.0.1", pair.hostNetwork.GetPort(), "Reincorporado"), "No se pudo reintentar conexión");
    Until([&] { return pair.client.IsConnected() && pair.host.GetLobbyPlayers().size() == 2 && pair.client.GetLobbyPlayers().size() == 2 && HasAgent(pair.client.GetWorld(), pair.client.GetLocalPlayerId()); },
          [&] { pair.Tick(); });
    Require(FindAgent(pair.host.GetWorld(), 2).connected && FindAgent(pair.host.GetWorld(), 2).name == "Reincorporado",
            "La identidad desconectada no se reemplazó al reincorporarse");
    pair.host.Stop();
    Until([&] { return !pair.client.IsConnected(); }, [&] { pair.client.Tick(0.01f); });
    Require(pair.client.GetWorld().phase == GamePhase::Base && pair.client.GetStatus().find("Error LAN") != std::string::npos,
            "La pérdida del anfitrión no volvió a la base con error explícito");
  }

  void PendingShotKeepsItsAim() {
    {
      Pair pair;
      pair.host.SetContract(ContractId::DoubleCross);
      pair.Deploy();
      const auto direction = (FindAgent(pair.host.GetWorld(), 2).position - FindAgent(pair.host.GetWorld(), 1).position).normalized();
      ActionCommand fire;
      fire.aimX = direction.x; fire.aimY = direction.y;
      fire.aimSlope = (CombatGeometry::HeadCenterHeight(Stance::Standing) + CombatConfig::HeadHeight * 0.25f - CombatGeometry::WeaponHeight(Stance::Standing)) / Distance(FindAgent(pair.host.GetWorld(), 1).position, FindAgent(pair.host.GetWorld(), 2).position);
      fire.fireLethal = true;
      pair.host.Submit(fire);
      ActionCommand pointerMoved;
      pointerMoved.aimX = -direction.x; pointerMoved.aimY = -direction.y;
      pair.host.Submit(pointerMoved);
      pair.host.Tick(0.02f);
      Require(!FindAgent(pair.host.GetWorld(), 2).alive, "El anfitrión cambió la dirección de un disparo pendiente");
    }
    {
      Pair pair;
      pair.host.SetContract(ContractId::DoubleCross);
      pair.Deploy();
      const auto direction = (FindAgent(pair.host.GetWorld(), 1).position - FindAgent(pair.host.GetWorld(), 2).position).normalized();
      ActionCommand fire;
      fire.aimX = direction.x; fire.aimY = direction.y;
      fire.aimSlope = (CombatGeometry::HeadCenterHeight(Stance::Standing) + CombatConfig::HeadHeight * 0.25f - CombatGeometry::WeaponHeight(Stance::Standing)) / Distance(FindAgent(pair.host.GetWorld(), 1).position, FindAgent(pair.host.GetWorld(), 2).position);
      fire.fireLethal = true;
      pair.client.Submit(fire);
      ActionCommand pointerMoved;
      pointerMoved.aimX = -direction.x; pointerMoved.aimY = -direction.y;
      pair.client.Submit(pointerMoved);
      pair.Advance(10);
      Require(!FindAgent(pair.host.GetWorld(), 1).alive, "El envío del cliente cambió la dirección capturada al disparar");
    }
    {
      RawPeer pair;
      pair.host.SetContract(ContractId::DoubleCross);
      pair.host.SetReady(true);
      Require(pair.clientNetwork.Send(1, Json{{"type", "ready"}, {"ready", true}}.dump()), "No se preparó el cliente crudo");
      Until([&] { return pair.host.CanDeploy(); }, [&] { pair.Tick(); });
      const auto baseEpoch = pair.epoch;
      pair.host.Deploy();
      Until([&] { return pair.epoch != baseEpoch && pair.snapshot.value("phase", -1) == static_cast<int>(GamePhase::Mission); }, [&] { pair.Tick(); });
      const auto direction = (FindAgent(pair.host.GetWorld(), 1).position - FindAgent(pair.host.GetWorld(), 2).position).normalized();
      const Json fire{{"type", "command"}, {"seq", 1}, {"epoch", pair.epoch},
                      {"move", {0, 0}}, {"aim", {direction.x, direction.y}}, {"slope", (CombatGeometry::HeadCenterHeight(Stance::Standing) + CombatConfig::HeadHeight * 0.25f - CombatGeometry::WeaponHeight(Stance::Standing)) / Distance(FindAgent(pair.host.GetWorld(), 1).position, FindAgent(pair.host.GetWorld(), 2).position)}, {"decoyDistance", 340.f}, {"flags", 64}};
      const Json pointerMoved{{"type", "command"}, {"seq", 2}, {"epoch", pair.epoch},
                              {"move", {0, 0}}, {"aim", {-direction.x, -direction.y}}, {"slope", 0.f}, {"decoyDistance", 340.f}, {"flags", 0}};
      Require(pair.clientNetwork.Send(1, fire.dump()) && pair.clientNetwork.Send(1, pointerMoved.dump()), "No se enviaron ambos mandos de regresión");
      pair.Advance(8);
      Require(!FindAgent(pair.host.GetWorld(), 1).alive, "La recepción de varios mandos cambió la dirección del disparo pendiente");
    }
  }

  void AimDirectionAndRestartAuthority() {
    Pair pair;
    ActionCommand aim; aim.aimX = 200.f; aim.aimY = 100.f;
    pair.host.Submit(aim);
    pair.host.Tick(0.02f);
    const auto facing = FindAgent(pair.host.GetWorld(), 1).facing;
    Require(std::abs(facing.x / facing.y - 2.f) < 0.001f, "La normalización local de puntería cambió el ángulo del cursor");
    aim.aimX = -300.f; aim.aimY = 100.f;
    pair.client.Submit(aim);
    pair.Advance(10);
    const auto remoteFacing = FindAgent(pair.host.GetWorld(), 2).facing;
    Require(std::abs(remoteFacing.x / remoteFacing.y + 3.f) < 0.001f, "La puntería LAN perdió la proporción entre ejes");
    Require(!pair.host.RestartOperation(), "No se debe reiniciar una operación desde la base");
    pair.Deploy();
    const auto seed = pair.host.GetWorld().seed;
    const auto contract = pair.host.GetWorld().contract;
    const auto original = FindAgent(pair.host.GetWorld(), 1).loadout;
    const auto order = FindAgent(pair.host.GetWorld(), 2).privateOrder;
    Require(!pair.client.RestartOperation(), "El cliente reinició la operación sin autoridad");
    ActionCommand shoot; shoot.fireLethal = true; shoot.aimX = 1.f;
    pair.host.Submit(shoot);
    pair.Advance(15);
    Require(FindAgent(pair.host.GetWorld(), 1).loadout.lethalAmmo == original.lethalAmmo - 1, "La prueba de reinicio no consumió munición real");
    Require(pair.host.RestartOperation(), "El anfitrión no pudo reiniciar la misión activa");
    Require(pair.host.GetWorld().seed == seed && pair.host.GetWorld().contract == contract && pair.host.GetWorld().elapsed == 0.f && pair.host.GetWorld().events.empty(), "El reinicio no conservó semilla/contrato o dejó registro de la misión anterior");
    Require(FindAgent(pair.host.GetWorld(), 1).loadout.lethalAmmo == original.lethalAmmo && FindAgent(pair.host.GetWorld(), 2).privateOrder == order, "El reinicio no restauró el equipo preparado o cambió órdenes determinísticas");
    Until([&] { return pair.client.GetWorld().events.empty() && pair.client.GetWorld().elapsed < 0.15f && FindAgent(pair.client.GetWorld(), 1).loadout.lethalAmmo == original.lethalAmmo; }, [&] { pair.Tick(); });
    Require(pair.client.IsConnected() && pair.client.GetLobbyPlayers().size() == 2 && HasAgent(pair.client.GetWorld(), pair.client.GetLocalPlayerId()) && pair.client.GetWorld().seed == seed, "El reinicio perdió participantes o no se propagó al cliente");
  }

  void RejectMidMissionAndVersion() {
    RawPeer running(true);
    Require(running.id == 0 && (running.rejected || running.disconnected) && running.host.GetWorld().agents.size() == 1,
            "Se admitió un agente en mitad de una operación");
    RawPeer incompatible(false, GameplayConfig::ProtocolVersion + 1);
    Require(incompatible.id == 0 && (incompatible.rejected || incompatible.disconnected), "Se aceptó una versión incompatible");
  }

  void VisibilityAndCoverOnTheWire() {
    Pair pair;
    pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.objects.clear(); world.guards.clear(); world.noises.clear(); world.shots.clear(); world.events.clear();
    world.obstacles = {{{{220.f, 80.f}, {20.f, 220.f}}, CoverKind::High, true, "Separacion"}};
    world.agents[0].position = {120.f, 180.f}; world.agents[0].facing = {1.f, 0.f};
    world.agents[1].position = {252.1f, 180.f}; world.agents[1].facing = {1.f, 0.f};
    Guard visible; visible.id = 31; visible.position = {350.f, 180.f}; visible.facing = {1.f, 0.f};
    Guard hidden; hidden.id = 32; hidden.position = {165.f, 180.f}; hidden.facing = {-1.f, 0.f};
    world.guards = {visible, hidden};
    Until([&] {
      return pair.client.GetWorld().visibility.observer == 2 && pair.client.GetWorld().guards.size() == 1 && pair.client.GetWorld().guards[0].id == 31;
    }, [&] { pair.Tick(true); });
    Require(pair.host.GetWorld().guards.size() == 2, "El filtro de red modifico el mundo autoritativo");
    Require(pair.client.GetWorld().agents.size() == 1 && pair.client.GetWorld().agents[0].id == 2, "La red revela un agente tras pared");
    ActionCommand cover; cover.coverToggle = true; cover.aimX = 1.f; cover.aimSlope = 0.25f;
    pair.client.Submit(cover); pair.client.Submit({});
    Until([&] { return FindAgent(pair.host.GetWorld(), 2).cover.active && FindAgent(pair.client.GetWorld(), 2).cover.active; }, [&] { pair.Tick(true); });
    Require(FindAgent(pair.host.GetWorld(), 2).cover.anchor == FindAgent(pair.client.GetWorld(), 2).cover.anchor, "La cobertura LAN no comparte el ancla del anfitrion");
    cover.coverToggle = true; cover.aimSlope = -0.5f;
    pair.client.Submit(cover); pair.Advance(12, true);
    Require(!FindAgent(pair.host.GetWorld(), 2).cover.active && !FindAgent(pair.client.GetWorld(), 2).cover.active, "La salida de cobertura no se sincroniza");
    Require(std::abs(FindAgent(pair.host.GetWorld(), 2).aimSlope + 0.5f) < 0.001f, "La red perdio la elevacion del apuntado");
  }

  void HideoutsDraggingAndDistractionsOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.noises.clear(); world.events.clear(); world.footsteps.clear();
    world.agents[0].position = {800.f, 650.f};
    auto &remote = world.agents[1]; remote.position = {150.f, 180.f}; remote.facing = {1.f, 0.f}; remote.actionCooldown = 0.f;
    Interactable closet; closet.id = 90; closet.kind = ObjectKind::Hideout; closet.hideoutKind = HideoutKind::Closet;
    closet.position = {210.f, 180.f}; closet.name = "Armario"; world.objects.push_back(closet);
    currentCase = "Hideouts/entrada";
    ActionCommand interact; interact.interact = true; interact.dragBody = true; interact.aimX = 1.f;
    pair.client.Submit(interact);
    Until([&] { return FindAgent(pair.host.GetWorld(), 2).hidden && FindAgent(pair.client.GetWorld(), 2).hidden; }, [&] { pair.Tick(true); });
    const auto inside = FindAgent(pair.host.GetWorld(), 2).position;
    const int ammo = FindAgent(pair.host.GetWorld(), 2).loadout.lethalAmmo;
    ActionCommand incompatible; incompatible.moveX = 1.f; incompatible.fireLethal = true;
    pair.client.Submit(incompatible); pair.Advance(12, true);
    Require(FindAgent(pair.host.GetWorld(), 2).position == inside && FindAgent(pair.host.GetWorld(), 2).loadout.lethalAmmo == ammo, "Mover o disparar dentro del armario no fue rechazado por el host");
    pair.client.Submit(interact);
    Until([&] { return !FindAgent(pair.host.GetWorld(), 2).hidden && !FindAgent(pair.client.GetWorld(), 2).hidden; }, [&] { pair.Tick(true); });
    pair.client.Submit({}); pair.Advance(8, true);
    Require(!WorldGeometry::PositionBlocked(world, FindAgent(world, 2).position, GameplayConfig::AgentRadius), "La salida LAN del escondite penetra su volumen");
    Guard body; body.id = 77; body.position = FindAgent(world, 2).position + WorldGeometry::HideoutFrontNormal(world.objects[0]) * 20.f;
    body.state = GuardState::Incapacitated; body.stun = 100.f; body.incapacityRemaining = 80.f; world.guards.push_back(body);
    ActionCommand drag; drag.dragBody = true; pair.client.Submit(drag);
    currentCase = "Hideouts/arrastre";
    Until([&] { return FindAgent(world, 2).draggingGuard == 77; }, [&] { pair.Tick(true); });
    drag.moveX = -1.f; pair.client.Submit(drag); pair.Advance(15, true);
    Require(Distance(world.guards[0].position, FindAgent(world, 2).position) <= GameplayConfig::BodyDragOffset + 1.f, "El cuerpo LAN no sigue al agente");
    pair.client.Submit({});
    currentCase = "Hideouts/soltar";
    Until([&] { return FindAgent(world, 2).draggingGuard < 0; }, [&] { pair.Tick(true); });
    const auto released = world.guards[0].position;
    pair.Advance(8, true);
    Require(world.guards[0].position == released, "Soltar E dejó un cuerpo LAN bloqueado en arrastre");
    remote.position = WorldGeometry::HideoutFrontPosition(world.objects[0], GameplayConfig::AgentRadius);
    world.guards[0].position = remote.position + WorldGeometry::HideoutFrontNormal(world.objects[0]) * GameplayConfig::BodyDragOffset;
    pair.client.Submit(interact);
    currentCase = "Hideouts/almacenar";
    Until([&] { return world.guards[0].bodyHidden; }, [&] { pair.Tick(true); });
    pair.client.Submit({}); pair.Advance(10, true);
    Require(world.objects[0].occupantGuard == 77 && world.guards[0].storedIn == 90, "El host no conserva la ocupación del cuerpo almacenado");
    Require(pair.client.GetWorld().guards.empty(), "La instantánea reveló el cuerpo almacenado");
    Require(!pair.client.GetWorld().objects.empty() && pair.client.GetWorld().objects[0].occupied && pair.client.GetWorld().objects[0].occupantGuard == 0, "El cliente no distingue un contenedor ocupado o recibe la identidad del cuerpo");
    world.objects.clear(); world.guards.clear(); world.noises.clear(); remote.position = {150.f, 350.f}; remote.actionCooldown = 0.f; remote.hideoutId = 0; remote.hidden = false;
    ActionCommand decoy; decoy.aimX = 1.f; decoy.throwDecoy = true; decoy.decoyDistance = 300.f;
    const int decoys = remote.loadout.decoys;
    pair.client.Submit(decoy); pair.client.Submit({});
    currentCase = "Hideouts/señuelo";
    Until([&] { return remote.loadout.decoys == decoys - 1; }, [&] { pair.Tick(true); });
    Require(!world.decoys.empty() && world.decoys.back().actor == 2 && std::abs(world.decoys.back().to.x - 450.f) < 1.f &&
      std::ranges::none_of(world.noises, [](const NoiseEvent &noise) { return noise.kind == SoundKind::Decoy; }),
      "El señuelo LAN pierde su trayectoria capturada o produce ruido antes de aterrizar");
    Until([&] { return std::ranges::any_of(world.noises, [](const NoiseEvent &noise) { return noise.kind == SoundKind::Decoy && std::abs(noise.position.x - 450.f) < 1.f; }); }, [&] { pair.Tick(true); });
    Require(world.decoys.empty(), "El impacto del señuelo conserva un proyectil en vuelo");
    pair.Advance(50, true); world.noises.clear(); remote.position = {150.f, 180.f}; remote.facing = {1.f, 0.f}; remote.actionCooldown = 0.f;
    world.obstacles.push_back({{{170.f, 100.f}, {20.f, 160.f}}, CoverKind::High, true, "Pared"});
    ActionCommand cover; cover.coverToggle = true; cover.aimX = 1.f; pair.client.Submit(cover); pair.client.Submit({});
    Until([&] { return remote.cover.active && FindAgent(pair.client.GetWorld(), 2).cover.active; }, [&] { pair.Tick(true); });
    ActionCommand knock; knock.knock = true; knock.aimX = 1.f; pair.client.Submit(knock);
    Until([&] { return !world.noises.empty(); }, [&] { pair.Tick(true); });
    Require(std::abs(world.noises.back().position.x - 170.f) < 0.1f && world.noises.back().actor == 2, "El golpe LAN no nace en la pared validada");
  }

  void AuthoritativeDecoyGuideOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.noises.clear(); world.events.clear(); world.footsteps.clear();
    world.agents[0].position = {250.f, 350.f}; world.agents[0].facing = {1.f, 0.f};
    auto &remote = world.agents[1]; remote.position = {150.f, 350.f}; remote.facing = {1.f, 0.f}; remote.stance = Stance::Prone;
    Interactable furniture; furniture.id = 99; furniture.kind = ObjectKind::Terminal; furniture.position = {450.f, 350.f}; furniture.name = "Mueble fuera de visión";
    world.objects.push_back(furniture);
    ActionCommand prepare; prepare.prepareDecoy = true; prepare.aimX = 1.f; prepare.decoyDistance = DistractionConfig::MaximumDecoyDistance;
    const int decoys = remote.loadout.decoys;
    remote.actionCooldown = 0.5f;
    pair.host.Submit(prepare);
    pair.client.Submit(prepare);
    pair.Advance(8);
    Require(!remote.decoyGuide.active && remote.loadout.decoys == decoys, "El host publica una guía disponible durante una acción incompatible");
    remote.actionCooldown = 0.f;
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).decoyGuide.active; }, [&] { pair.Tick(); });
    const auto destination = remote.decoyGuide.destination;
    Require(remote.decoyGuide.blocked && destination.x < furniture.position.x && pair.client.GetWorld().objects.empty(), "La guía LAN no valida el mueble que el cliente no conoce");
    Require(FindAgent(pair.client.GetWorld(), 2).decoyGuide.destination == destination && remote.loadout.decoys == decoys, "Preparar un señuelo consume inventario o altera el destino autoritativo");
    Require(FindAgent(world, 1).decoyGuide.active && !FindAgent(pair.client.GetWorld(), 1).decoyGuide.active, "Se filtró una guía de lanzamiento ajena");
    pair.host.Submit({});
    ActionCommand launch; launch.throwDecoy = true; launch.aimX = 1.f; launch.decoyDistance = prepare.decoyDistance;
    pair.client.Submit(launch); pair.client.Submit({});
    Until([&] { return remote.loadout.decoys == decoys - 1 && !FindAgent(pair.client.GetWorld(), 2).decoyGuide.active; }, [&] { pair.Tick(true); });
    Require(!world.decoys.empty() && world.decoys.back().to == destination, "El vuelo LAN difiere de su guía validada por el host");
    Until([&] { return std::ranges::any_of(world.noises, [destination](const NoiseEvent &noise) { return noise.kind == SoundKind::Decoy && noise.position == destination; }); }, [&] { pair.Tick(true); });
    Require(world.decoys.empty(), "La llegada del disco no finaliza su vuelo autoritativo");
  }

  void HiddenFootstepsOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.objects.clear(); world.guards.clear(); world.obstacles.clear(); world.events.clear();
    world.agents[0].position = {800.f, 650.f}; world.agents[1].position = {150.f, 180.f}; world.agents[1].facing = {1.f, 0.f};
    world.obstacles.push_back({{{240.f, 80.f}, {20.f, 220.f}}, CoverKind::High, true, "Pared"});
    Guard guard; guard.id = 91; guard.position = {320.f, 180.f}; guard.facing = {1.f, 0.f}; world.guards.push_back(guard);
    FootstepCue cue; cue.position = {320.f, 192.f}; cue.direction = 2; cue.remaining = DistractionConfig::FootstepLifetime; cue.sourceGuard = 91;
    world.footsteps.push_back(cue);
    Until([&] { return !pair.client.GetWorld().footsteps.empty(); }, [&] { pair.Tick(true); });
    Require(pair.client.GetWorld().guards.empty() && pair.client.GetWorld().footsteps[0].sourceGuard == 0, "Los pasos LAN revelaron el cuerpo o la identidad oculta");
    pair.Advance(110, true);
    Require(pair.client.GetWorld().footsteps.empty(), "Los pasos LAN no caducaron");
  }

  void CqcAndPushOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.noises.clear(); world.shots.clear(); world.events.clear();
    for (auto &agent : world.agents) { agent.privateOrder = PrivateOrder::None; agent.actionCooldown = 0.f; }
    world.agents[0].position = {700.f, 650.f}; world.agents[1].position = {200.f, 300.f}; world.agents[1].facing = {1.f, 0.f};
    Guard guard; guard.id = 91; guard.position = {228.f, 300.f}; guard.facing = {1.f, 0.f}; guard.patrol = {guard.position};
    world.guards.push_back(guard);
    Until([&] { return pair.client.GetWorld().guards.size() == 1 && pair.client.GetWorld().guards[0].id == 91; }, [&] { pair.Tick(); });
    ActionCommand hold; hold.cqcHeld = true; hold.cqc = true; hold.aimX = 1.f;
    pair.client.Submit(hold); pair.Advance(40);
    Require(world.guards[0].heldBy == 2 && FindAgent(world, 2).cqcHoldingGuard == 91, "El anfitrión no valida el agarre F del cliente");
    Require(FindAgent(pair.client.GetWorld(), 2).cqcHoldingGuard == 91 && pair.client.GetWorld().guards[0].heldBy == 2, "El cliente no recibe la sujeción confirmada");
    Require(pair.client.GetWorld().visibility.visualRadii.size() == VisibilityConfig::VisualRayCount && pair.client.GetWorld().securityEntries.empty(), "El cliente pierde la frontera suave o recibe accesos privados de seguridad");
    Require(FindAgent(world, 2).cqcProgress == 0, "Mantener F se serializa como pulsaciones repetidas");
    for (unsigned int pulse = 1; pulse <= 3; ++pulse) {
      pair.client.Submit({}); pair.Advance(13);
      pair.client.Submit(hold); pair.Advance(34);
      if (pulse < 3) Require(FindAgent(world, 2).cqcProgress == pulse, "Las nuevas pulsaciones F no conservan la cadencia autoritativa");
    }
    Require(world.guards[0].state == GuardState::Incapacitated && world.guards[0].health == 100.f && world.guards[0].heldBy == 0, "CQC remoto no termina con inconsciencia y liberación");
    pair.client.Submit({}); pair.Advance(20);
    world.agents[0].position = {228.f, 300.f}; world.agents[0].cover = {};
    ActionCommand push; push.push = true; push.aimX = 1.f;
    pair.client.Submit(push); pair.Advance(15);
    Require(world.agents[0].position.x > 228.f && world.agents[0].position.x <= 264.01f && world.agents[0].health == 100.f, "El cliente no empuja a su compañero de forma limitada y no letal");
    Until([&] { return std::ranges::any_of(pair.client.GetWorld().agents, [&](const Agent &agent) { return agent.id == 1 && Distance(agent.position, world.agents[0].position) < 0.01f; }); }, [&] { pair.Tick(); });
  }

  void PuzzleOwnershipAndDoorsOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.noises.clear(); world.events.clear(); world.sounds.clear();
    world.agents[0].position = {230.f, 200.f}; world.agents[0].facing = {1.f, 0.f};
    world.agents[1].position = {150.f, 280.f}; world.agents[1].facing = {1.f, 0.f};
    world.objects.reserve(4);
    for (unsigned int index = 0; index < 2; ++index) {
      Interactable door{80u + index, ObjectKind::Door, {index == 0 ? 270.f : 190.f, 200.f + 80.f * index}, "Cerradura LAN"};
      door.footprint = {24.f, 24.f}; door.locked = true; door.puzzle.kind = PuzzleKind::Door; door.puzzle.difficulty = 3;
      world.objects.push_back(door);
    }
    ActionCommand interact; interact.interact = true; interact.aimX = 1.f;
    pair.host.Submit(interact); pair.host.Submit({});
    Until([&] { return FindAgent(world, 1).puzzle.target == 80; }, [&] { pair.Tick(); });
    pair.client.Submit(interact); pair.client.Submit({});
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).puzzle.target == 81; }, [&] { pair.Tick(); });
    Require(FindAgent(world, 1).puzzle.target == 80 && FindAgent(pair.client.GetWorld(), 1).puzzle.target == 0,
      "La réplica transmite al cliente el desafío privado que está resolviendo otro agente");
    Require(FindAgent(pair.client.GetWorld(), 1).privateOrder == PrivateOrder::None &&
      FindAgent(pair.client.GetWorld(), 2).privateOrder == FindAgent(world, 2).privateOrder, "Los nuevos estados de dispositivos alteran la privacidad de las órdenes");
    ActionCommand choice; choice.puzzleTarget = 80; choice.puzzleInput = 1;
    pair.client.Submit(choice); pair.Advance(12);
    Require(FindAgent(world, 1).puzzle.stage == 0 && FindAgent(world, 2).puzzle.stage == 0 && world.objects[0].locked && world.objects[1].locked,
      "Un comando del cliente resolvió una cerradura perteneciente a otro agente");
    choice.puzzleTarget = 81; choice.puzzleInput = 0; pair.client.Submit(choice); pair.client.Submit({});
    Until([&] { return FindAgent(world, 2).puzzle.target == 0 && FindAgent(pair.client.GetWorld(), 2).puzzle.target == 0; }, [&] { pair.Tick(); });
    Require(world.objects[1].locked && !world.objects[1].open && world.objects[1].user == 0, "Cancelar por LAN abre la puerta o deja la cerradura ocupada");
    pair.client.Submit(interact); pair.client.Submit({});
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).puzzle.target == 81; }, [&] { pair.Tick(); });
    for (unsigned int stage = 0; stage < 3; ++stage) {
      Until([&] { return FindAgent(pair.client.GetWorld(), 2).puzzle.stage == stage; }, [&] { pair.Tick(); });
      const auto &puzzle = FindAgent(pair.client.GetWorld(), 2).puzzle;
      choice.puzzleTarget = puzzle.target; choice.puzzleInput = static_cast<int>(puzzle.sequence.at(puzzle.stage));
      pair.client.Submit(choice); pair.client.Submit({});
      if (stage < 2) {
        Until([&] { return FindAgent(world, 2).puzzle.stage == stage + 1; }, [&] { pair.Tick(); });
        pair.Advance(10);
        Require(FindAgent(world, 2).puzzle.stage == stage + 1 && world.objects[1].locked, "Una sola pulsación de puzle se repite automáticamente en la simulación LAN");
      } else Until([&] { return !world.objects[1].locked && FindAgent(pair.client.GetWorld(), 2).puzzle.target == 0; }, [&] { pair.Tick(); });
    }
    Require(world.objects[1].open && std::ranges::any_of(pair.client.GetWorld().objects, [](const Interactable &o) { return o.id == 81 && o.open && !o.locked && o.puzzle.state == PuzzleState::Solved; }),
      "El éxito de la cerradura no replica su paso y estado reales");
  }

  void CameraTrapAndIntelligenceOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.noises.clear(); world.events.clear(); world.sounds.clear();
    world.agents[0].position = {800.f, 650.f}; world.agents[1].position = {200.f, 300.f}; world.agents[1].facing = {1.f, 0.f};
    world.objects.reserve(4);
    Interactable computer{91, ObjectKind::Computer, {240.f, 300.f}, "Computador LAN"};
    computer.footprint = {24.f, 24.f}; computer.puzzle.kind = PuzzleKind::Computer; computer.puzzle.family = PuzzleFamily::Arrows; computer.puzzle.difficulty = 2;
    computer.puzzle.intelligence = "INTELIGENCIA LAN: grupo de vigilancia 7."; world.objects.push_back(computer);
    Until([&] { return !pair.client.GetWorld().objects.empty() && pair.client.GetWorld().objects[0].id == 91; }, [&] { pair.Tick(true); });
    Require(pair.client.GetWorld().objects[0].puzzle.intelligence.empty(), "El snapshot revela inteligencia sin descifrar el computador");
    ActionCommand interact; interact.interact = true; interact.aimX = 1.f; pair.client.Submit(interact); pair.client.Submit({});
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).puzzle.target == 91; }, [&] { pair.Tick(true); });
    for (unsigned int stage = 0; stage < 2; ++stage) {
      Until([&] { return FindAgent(pair.client.GetWorld(), 2).puzzle.stage == stage; }, [&] { pair.Tick(true); });
      const auto puzzle = FindAgent(pair.client.GetWorld(), 2).puzzle;
      ActionCommand choice; choice.puzzleTarget = 91; choice.puzzleInput = static_cast<int>(puzzle.sequence.at(puzzle.stage));
      pair.client.Submit(choice); pair.client.Submit({});
      Until([&] { return stage == 1 ? world.objects[0].completed : FindAgent(world, 2).puzzle.stage == 1; }, [&] { pair.Tick(true); });
    }
    Until([&] { return pair.client.GetWorld().secondary == 1 && !pair.client.GetWorld().objects[0].puzzle.intelligence.empty(); }, [&] { pair.Tick(true); });
    Require(pair.client.GetWorld().objects[0].puzzle.intelligence == computer.puzzle.intelligence, "La inteligencia descifrada no se transmite tras el éxito real");
    world.objects.clear();
    Interactable camera{92, ObjectKind::Camera, {400.f, 300.f}, "Cámara LAN"};
    camera.facing = {-1.f, 0.f}; camera.camera.sweeping = true; camera.camera.baseAngle = 3.14159265f; camera.camera.minAngle = -0.1f; camera.camera.maxAngle = 0.1f; camera.camera.speed = 0.15f;
    world.objects.push_back(camera);
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).detectionProgress > 0.f; }, [&] { pair.Tick(true); });
    Require(!world.alarm && world.objects[0].progress > 0.f, "La cámara remota confirma la detección sin una fase progresiva");
    Until([&] { return pair.client.GetWorld().alarm && FindAgent(pair.client.GetWorld(), 2).detectionConfirmed; }, [&] { pair.Tick(true); });
    Require(world.alarm && world.securityActive && !pair.client.GetWorld().objects.empty() && pair.client.GetWorld().objects[0].camera.sweepAngle != 0.f,
      "El movimiento, la detección y la alarma de cámara no proceden del estado anfitrión");
    world.objects.clear(); world.alarm = false;
    Interactable trap{93, ObjectKind::Explosive, {240.f, 300.f}, "Trampa LAN"}; trap.footprint = {16.f, 12.f};
    trap.solid = SpatialOverride::Disabled; trap.blocksVision = SpatialOverride::Disabled; trap.puzzle.kind = PuzzleKind::Explosive; trap.puzzle.difficulty = 2; trap.trap.fuse = 0.25f;
    world.objects.push_back(trap);
    Until([&] { return std::ranges::any_of(pair.client.GetWorld().objects, [](const Interactable &o) { return o.id == 93 && o.trap.remaining > 0.f; }); }, [&] { pair.Tick(true); });
    Require(std::ranges::any_of(pair.client.GetWorld().sounds, [](const SoundEvent &sound) { return sound.kind == SoundKind::TrapTick; }), "La trampa audible no transmite su evento de reloj al participante cercano");
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).health < GameplayConfig::MaxHealth; }, [&] { pair.Tick(true); });
    Require(world.objects[0].trap.state == TrapState::Detonated && FindAgent(world, 2).health == FindAgent(pair.client.GetWorld(), 2).health,
      "La detonación y el daño de trampa no se resuelven de forma autoritativa");
  }

  void CoordinationAndPhysicalLeversOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.noises.clear(); world.events.clear(); world.sounds.clear();
    world.objects.reserve(6);
    world.agents[0].position = {220.f, 300.f}; world.agents[0].facing = {1.f, 0.f};
    world.agents[1].position = {200.f, 300.f}; world.agents[1].facing = {1.f, 0.f};
    Interactable relay{80, ObjectKind::CoopSwitch, {240.f, 320.f}, "Sincronización LAN"};
    relay.footprint = {24.f, 24.f}; relay.puzzle.kind = PuzzleKind::Coordination; relay.puzzle.family = PuzzleFamily::Coordination;
    relay.solid = SpatialOverride::Disabled; relay.blocksVision = SpatialOverride::Disabled; world.objects.push_back(relay);
    ActionCommand interact; interact.interact = true; interact.aimX = 1.f;
    pair.host.Submit(interact); pair.client.Submit(interact);
    currentCase = "CoordinaciónLAN/abrir";
    Until([&] { return FindAgent(world, 1).puzzle.target == 80 && FindAgent(pair.client.GetWorld(), 2).puzzle.target == 80; }, [&] { pair.Tick(); });
    Require(HasAgent(pair.client.GetWorld(), 1) && FindAgent(pair.client.GetWorld(), 1).puzzle.target == 0 &&
      FindAgent(pair.client.GetWorld(), 1).privateOrder == PrivateOrder::None, "La coordinación LAN revela el desafío o la orden privada del otro operador");
    ActionCommand hold; hold.puzzleTarget = 80; hold.puzzleHeld = true;
    pair.host.Submit(hold); pair.client.Submit(hold);
    currentCase = "CoordinaciónLAN/mantener";
    Until([&] { return world.objects[0].puzzle.coordinationProgress > 0.5f && FindAgent(pair.client.GetWorld(), 2).puzzle.coordinationProgress > 0.5f; }, [&] { pair.Tick(); });
    Require(!world.objects[0].completed && FindAgent(world, 1).puzzle.holding && FindAgent(world, 2).puzzle.holding,
      "Los comandos held no permanecen activos entre paquetes de red");
    pair.client.Submit({}); currentCase = "CoordinaciónLAN/soltar";
    Until([&] { return world.objects[0].puzzle.coordinationProgress == 0.f && !FindAgent(world, 2).puzzle.holding; }, [&] { pair.Tick(); });
    Require(world.objects[0].active && !world.objects[0].completed, "Soltar el interruptor remoto completa el circuito");
    ActionCommand cancel; cancel.puzzleTarget = 80; cancel.puzzleInput = 0; pair.client.Submit(cancel); pair.client.Submit({});
    Until([&] { return FindAgent(world, 2).puzzle.target == 0 && FindAgent(pair.client.GetWorld(), 2).puzzle.target == 0; }, [&] { pair.Tick(); });
    Require(FindAgent(world, 1).puzzle.target == 80 && world.objects[0].user == 1, "Cancelar un operador cierra o deja huérfano al otro");
    pair.client.Submit(interact); pair.client.Submit({});
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).puzzle.target == 80; }, [&] { pair.Tick(); });
    pair.client.Submit(hold); currentCase = "CoordinaciónLAN/éxito";
    Until([&] { return world.objects[0].completed && std::ranges::any_of(pair.client.GetWorld().objects, [](const Interactable &object) { return object.id == 80 && object.completed && !object.active; }); }, [&] { pair.Tick(); });
    Require(FindAgent(world, 1).puzzle.target == 0 && FindAgent(world, 2).puzzle.target == 0, "Completar por LAN no libera las dos sesiones de coordinación");

    pair.host.Submit({}); pair.client.Submit({}); pair.Advance(8);
    world.objects.clear(); world.agents[0].position = {60.f, 60.f}; world.agents[1].position = {200.f, 300.f};
    Interactable computer{90, ObjectKind::Computer, {240.f, 300.f}, "Control de palancas LAN"};
    computer.footprint = {24.f, 24.f}; computer.puzzle.kind = PuzzleKind::Computer; computer.puzzle.family = PuzzleFamily::Levers;
    computer.puzzle.leverOrder = {2, 1, 4, 3}; computer.puzzle.intelligence = "INTELIGENCIA: circuito distribuido verificado por LAN."; world.objects.push_back(computer);
    const std::array<sf::Vector2f, 4> positions{{{360.f, 300.f}, {360.f, 420.f}, {480.f, 300.f}, {480.f, 420.f}}};
    for (unsigned int index = 0; index < positions.size(); ++index) {
      Interactable node{91u + index, ObjectKind::PuzzleNode, positions[index], "Palanca LAN"};
      node.footprint = {16.f, 16.f}; node.solid = SpatialOverride::Disabled; node.blocksVision = SpatialOverride::Disabled;
      node.puzzle.kind = PuzzleKind::Computer; node.puzzle.parentDevice = 90; node.puzzle.nodeIndex = index + 1; world.objects.push_back(node);
    }
    currentCase = "PalancasLAN/panel";
    Until([&] { return std::ranges::any_of(pair.client.GetWorld().objects, [](const Interactable &object) { return object.id == 90; }); }, [&] { pair.Tick(); });
    pair.client.Submit(interact); pair.client.Submit({});
    Until([&] { return FindAgent(pair.client.GetWorld(), 2).puzzle.target == 90; }, [&] { pair.Tick(); });
    const auto sequence = FindAgent(pair.client.GetWorld(), 2).puzzle.sequence;
    const auto panel = std::ranges::find_if(pair.client.GetWorld().objects, [](const Interactable &object) { return object.id == 90; });
    Require(sequence == computer.puzzle.leverOrder && panel->puzzle.leverOrder.empty() && panel->puzzle.intelligence.empty(),
      "La sesión no entrega la instrucción solicitada o el objeto revela la solución e inteligencia privadas");
    ActionCommand remoteInput; remoteInput.puzzleTarget = 90; remoteInput.puzzleInput = sequence.front(); pair.client.Submit(remoteInput); pair.client.Submit({}); pair.Advance(10);
    Require(world.objects[0].puzzle.sequenceProgress == 0 && !world.objects[0].completed, "Un mensaje remoto elude la activación física de las palancas");
    cancel.puzzleTarget = 90; pair.client.Submit(cancel); pair.client.Submit({});
    Until([&] { return FindAgent(world, 2).puzzle.target == 0; }, [&] { pair.Tick(); });
    const auto moveTo = [&](sf::Vector2f destination) {
      for (const bool horizontal : {false, true}) {
        const auto coordinate = [&](sf::Vector2f point) { return horizontal ? point.x : point.y; };
        const float remaining = coordinate(destination) - coordinate(FindAgent(world, 2).position);
        if (std::abs(remaining) <= 4.f) continue;
        ActionCommand move; if (horizontal) move.moveX = remaining > 0.f ? 1.f : -1.f; else move.moveY = remaining > 0.f ? 1.f : -1.f;
        pair.client.Submit(move);
        Until([&] { return std::abs(coordinate(destination) - coordinate(FindAgent(world, 2).position)) <= 4.f; }, [&] { pair.Tick(); });
        pair.client.Submit({}); pair.Advance(5);
      }
    };
    unsigned int stage = 0;
    for (const auto node : sequence) {
      currentCase = "PalancasLAN/desplazar y accionar";
      moveTo(positions[node - 1] - sf::Vector2f{30.f, 0.f}); pair.client.Submit(interact); pair.client.Submit({});
      ++stage; Until([&] { return world.objects[0].puzzle.sequenceProgress == stage; }, [&] { pair.Tick(); });
      Require(world.objects[node].open, "La palanca accionada por el cliente no cambia su estado autoritativo");
    }
    moveTo({280.f, 300.f});
    currentCase = "PalancasLAN/inteligencia";
    Until([&] { return std::ranges::any_of(pair.client.GetWorld().objects, [](const Interactable &object) { return object.id == 90 && object.completed && !object.puzzle.intelligence.empty(); }); }, [&] { pair.Tick(); });
    Require(world.objects[0].completed && FindAgent(world, 2).puzzle.target == 0, "El circuito físico completo no modifica ni libera el computador por LAN");
  }

  void ManeuversAimAndSuppressorsOnTheWire() {
    Pair pair;
    pair.client.SetLoadout({12, 12, 0, 0});
    currentCase = "ManiobrasLAN/equipo";
    Until([&] { return FindAgent(pair.host.GetWorld(), 2).operationLoadout.lethalAmmo == 12 && FindAgent(pair.host.GetWorld(), 2).operationLoadout.nonLethalAmmo == 12; }, [&] { pair.Tick(); });
    pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.guards.clear(); world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear(); world.noises.clear(); world.sounds.clear(); world.events.clear(); world.shots.clear(); world.decoys.clear();
    world.agents[0].position = {800.f, 650.f}; world.agents[1].position = {150.f, 300.f}; world.agents[1].facing = {1.f, 0.f};
    const auto remote = [&]() -> const Agent & { return FindAgent(world, 2); };
    const auto replica = [&]() -> const Agent & { return FindAgent(pair.client.GetWorld(), 2); };
    ActionCommand run; run.moveX = 1.f; run.run = true; run.aimX = 1.f; pair.client.Submit(run);
    currentCase = "ManiobrasLAN/carrera";
    Until([&] { return remote().running && remote().moving; }, [&] { pair.Tick(); });
    const auto diveFrom = remote().position;
    auto dive = run; dive.dive = true; pair.client.Submit(dive); pair.client.Submit(run);
    currentCase = "ManiobrasLAN/zambullida";
    Until([&] { return remote().maneuver.kind == ManeuverKind::Dive && replica().maneuver.kind == ManeuverKind::Dive; }, [&] { pair.Tick(); });
    Require(remote().maneuver.phase != ManeuverPhase::None && replica().maneuver.phase != ManeuverPhase::None && replica().maneuver.direction.x > 0.99f,
      "El pulso de zambullida del cliente no replica sus fases y dirección físicas");
    Until([&] { return remote().maneuver.kind == ManeuverKind::None && replica().maneuver.kind == ManeuverKind::None && replica().stance == Stance::Prone; }, [&] { pair.Tick(); });
    Require(remote().stance == Stance::Prone && remote().position.x > diveFrom.x + 100.f, "La zambullida por LAN no desplaza al agente ni termina tumbada");
    auto stand = run; stand.prone = true; pair.client.Submit(stand); pair.client.Submit(run);
    Until([&] { return remote().stance == Stance::Standing && remote().running; }, [&] { pair.Tick(); });
    const auto resumed = world.elapsed;
    Until([&] { return world.elapsed > resumed + ManeuverConfig::DiveCooldown + 0.1f; }, [&] {
      pair.Tick(); Require(remote().maneuver.kind == ManeuverKind::None, "Un pulso de zambullida antiguo se repite al volver a correr");
    });
    ActionCommand roll; roll.roll = true; roll.aiming = true; roll.aimX = 1.f; pair.client.Submit(roll);
    ActionCommand aim; aim.aiming = true; aim.aimX = 1.f; pair.client.Submit(aim);
    currentCase = "ManiobrasLAN/rodada de pie";
    Until([&] { return remote().aiming && !remote().running; }, [&] { pair.Tick(); });
    Require(remote().stance == Stance::Standing && remote().maneuver.kind == ManeuverKind::None, "El anfitrión acepta una rodada mientras el cliente está de pie");
    ActionCommand crouch; crouch.crouch = true; crouch.aimX = 1.f; pair.client.Submit(crouch); pair.client.Submit({});
    Until([&] { return remote().stance == Stance::Crouched && replica().stance == Stance::Crouched; }, [&] { pair.Tick(); });
    roll = {}; roll.roll = true; roll.moveY = 1.f; roll.aimY = 1.f; pair.client.Submit(roll);
    ActionCommand move; move.moveY = 1.f; move.aimY = 1.f; pair.client.Submit(move);
    currentCase = "ManiobrasLAN/rodada agachado";
    Until([&] { return remote().maneuver.kind == ManeuverKind::Roll && replica().maneuver.kind == ManeuverKind::Roll; }, [&] { pair.Tick(); });
    Require(replica().maneuver.direction.y > 0.99f && replica().maneuver.phase != ManeuverPhase::None, "La rodada LAN pierde la dirección y fase del anfitrión");
    Until([&] { return remote().maneuver.kind == ManeuverKind::None && replica().maneuver.kind == ManeuverKind::None && replica().stance == Stance::Crouched; }, [&] { pair.Tick(); });
    pair.client.Submit(aim);
    Until([&] { return remote().aiming && replica().aiming; }, [&] { pair.Tick(); });
    Require(remote().stance == Stance::Crouched, "La rodada no conserva la postura agachada al completar la recuperación");

    auto fire = aim; fire.fireSelected = true; fire.weaponChoice = 0; pair.client.Submit(fire); pair.client.Submit(aim);
    currentCase = "SilenciadoresLAN/pistola";
    Until([&] { return remote().pistolSuppressor < WeaponConfig::MaximumSuppressor && replica().pistolSuppressor == remote().pistolSuppressor; }, [&] { pair.Tick(); });
    const float pistolWear = remote().pistolSuppressor;
    Require(pistolWear == WeaponConfig::MaximumSuppressor - WeaponConfig::PistolSuppressorWear && remote().nonLethalSuppressor == WeaponConfig::MaximumSuppressor,
      "El disparo de pistola desgasta el silenciador ajeno o no replica su consumo real");
    auto select = aim; select.weaponChoice = 1; pair.client.Submit(select); pair.client.Submit(aim);
    Until([&] { return remote().selectedWeapon == WeaponKind::NonLethal && remote().actionCooldown <= 0.f; }, [&] { pair.Tick(); });
    fire = aim; fire.fireSelected = true; pair.client.Submit(fire); pair.client.Submit(aim);
    currentCase = "SilenciadoresLAN/no letal";
    Until([&] { return remote().nonLethalSuppressor < WeaponConfig::MaximumSuppressor && replica().nonLethalSuppressor == remote().nonLethalSuppressor; }, [&] { pair.Tick(); });
    const float nonLethalWear = remote().nonLethalSuppressor;
    Require(nonLethalWear == WeaponConfig::MaximumSuppressor - WeaponConfig::NonLethalSuppressorWear && replica().pistolSuppressor == pistolWear,
      "Cambiar de arma o disparar por LAN reinicia el desgaste del otro silenciador");
    auto reload = aim; reload.reloadWeapon = true; pair.client.Submit(reload); pair.client.Submit(aim);
    currentCase = "SilenciadoresLAN/recarga";
    Until([&] { return remote().reloadRemaining > 0.f && replica().reloadRemaining > 0.f; }, [&] { pair.Tick(); });
    Until([&] { return remote().reloadRemaining == 0.f && replica().reloadRemaining == 0.f && replica().nonLethalMagazine == WeaponConfig::NonLethalCapacity; }, [&] { pair.Tick(); });
    Require(remote().pistolSuppressor == pistolWear && remote().nonLethalSuppressor == nonLethalWear && replica().pistolSuppressor == pistolWear && replica().nonLethalSuppressor == nonLethalWear,
      "La recarga LAN restaura la durabilidad del silenciador");
    pair.client.Submit({});
    Until([&] { return !remote().aiming && !replica().aiming; }, [&] { pair.Tick(); });
  }

  void RejectInvalidPuzzleCommands() {
    const std::array<std::pair<Json, Json>, 5> invalid{{{17, 91}, {-2, 91}, {1.5, 91}, {1, -1}, {1, 1000001}}};
    for (const auto &[choice, target] : invalid) {
      RawPeer peer; peer.PuzzleCommand(choice, target);
      Until([&] { return peer.rejected || peer.disconnected; }, [&] { peer.Tick(); });
    }
  }

  void TrainingAndMissionRulesOnTheWire() {
    Pair pair;
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    Require(IsTraining(world) && IsTraining(pair.client.GetWorld()), "El cliente no recibe el modo seguro de entrenamiento de su anfitrión");
    world.objects.clear(); world.obstacles.clear(); world.securityEntries.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[1].position = {250.f, 300.f}; world.agents[1].facing = {1.f, 0.f};
    for (std::size_t index = 0; index < world.guards.size(); ++index) world.guards[index].position = {350.f + 500.f * index, 300.f};
    ActionCommand fire; fire.fireLethal = true; fire.aimX = -1.f;
    fire.aimSlope = (CombatConfig::StandingHeight - CombatConfig::HeadHeight * 0.5f - CombatConfig::StandingWeaponHeight) / 50.f;
    pair.client.Submit(fire); pair.client.Submit({});
    Until([&] { return FindAgent(world, 1).health < GameplayConfig::MaxHealth; }, [&] { pair.Tick(true); });
    Require(FindAgent(world, 1).alive && !FindAgent(world, 1).incapacitated && FindAgent(world, 1).health >= 1.f,
      "Un disparo remoto mata o incapacita al anfitrión en la sala segura");
    pair.Advance(80, true);
    fire.aimX = 1.f; fire.aimSlope = (CombatConfig::StandingHeight - CombatConfig::HeadHeight * 0.5f - CombatConfig::StandingWeaponHeight) / 100.f;
    pair.client.Submit(fire); pair.client.Submit({});
    Until([&] { return world.guards[0].trainingHits > 0 && std::ranges::any_of(pair.client.GetWorld().guards, [](const Guard &guard) { return guard.trainingDummy && guard.trainingHits > 0; }); }, [&] { pair.Tick(true); });
    Require(world.guards[0].trainingDummy && world.guards[0].health >= 1.f && world.guards[0].state != GuardState::Dead && world.guards[0].state != GuardState::Incapacitated,
      "El impacto remoto convierte el muñeco en un enemigo muerto o incapacitado");
    Interactable trap{400, ObjectKind::Explosive, {260.f, 300.f}, "Prueba LAN segura"};
    trap.footprint = {16.f, 12.f}; trap.solid = SpatialOverride::Disabled; trap.blocksVision = SpatialOverride::Disabled;
    trap.puzzle.kind = PuzzleKind::Explosive; trap.trap.fuse = 0.2f; world.objects.push_back(trap);
    Until([&] { return world.objects[0].trap.state == TrapState::Detonated && FindAgent(pair.client.GetWorld(), 2).health < GameplayConfig::MaxHealth; }, [&] { pair.Tick(true); });
    Require(FindAgent(world, 2).alive && !FindAgent(world, 2).incapacitated && FindAgent(pair.client.GetWorld(), 2).alive && !FindAgent(pair.client.GetWorld(), 2).incapacitated,
      "La explosión de entrenamiento produce estados de muerte o desmayo distintos entre anfitrión y cliente");
    Require(!world.alarm && !world.securityActive && world.reinforcementWaves == 0 && std::ranges::all_of(world.guards, [](const Guard &guard) { return guard.trainingDummy; }),
      "Una prueba LAN genera guardias, refuerzos o alarmas enemigos en la sala");
    pair.host.SetContract(ContractId::DoubleCross); pair.Deploy();
    Require(world.mode == SimulationMode::Operation && pair.client.GetWorld().mode == SimulationMode::Operation &&
      std::ranges::none_of(world.guards, [](const Guard &guard) { return guard.trainingDummy; }), "El despliegue LAN conserva el modo o los muñecos protegidos de la sala");
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear();
    world.agents[0].position = {200.f, 300.f}; world.agents[1].position = {250.f, 300.f};
    fire.aimX = -1.f; fire.aimSlope = (CombatConfig::StandingHeight - CombatConfig::HeadHeight * 0.5f - CombatConfig::StandingWeaponHeight) / 50.f;
    pair.client.Submit(fire); pair.client.Submit({});
    Until([&] { return !FindAgent(world, 1).alive; }, [&] { pair.Tick(true); });
    Require(!FindAgent(world, 1).alive && world.mode == SimulationMode::Operation, "La protección amistosa de la sala contamina el daño letal de una misión LAN");
  }

  void RejectInvalidCommandAndFlood() {
    RawPeer invalid;
    const std::string malformed = "{\"type\":\"command\",\"seq\":1,\"epoch\":" + std::to_string(invalid.epoch) +
                                  ",\"move\":[1e100,0],\"aim\":[0,0],\"flags\":0}";
    Require(invalid.clientNetwork.Send(NetworkConfig::HostId, malformed), "No se pudo enviar caso de eje no finito");
    Until([&] { return invalid.rejected || invalid.disconnected; }, [&] { invalid.Tick(); });
    RawPeer flood;
    for (std::uint64_t sequence = 1; sequence <= GameplayConfig::MaximumCommandsPerSecond + 2; ++sequence) {
      flood.Command(sequence, 0.f, 0.f);
    }
    Until([&] { return flood.rejected || flood.disconnected; }, [&] { flood.Tick(); });
  }
}

int main() {
  try {
    const auto run = [](const char *name, auto test) { currentCase = name; test(); };
    run("SoloPauseAndFiniteInput", SoloPauseAndFiniteInput);
    run("LobbyAndAuthority", LobbyAndAuthority);
    run("PrivateSnapshotAndClientDoesNotSimulate", PrivateSnapshotAndClientDoesNotSimulate);
    run("PausedHostStillRunsRemoteAgent", PausedHostStillRunsRemoteAgent);
    run("SlowClientKeepsLobbyAndPhaseChanges", SlowClientKeepsLobbyAndPhaseChanges);
    run("SequenceTimeoutIdentityAndPulses", SequenceTimeoutIdentityAndPulses);
    run("PendingShotKeepsItsAim", PendingShotKeepsItsAim);
    run("AimDirectionAndRestartAuthority", AimDirectionAndRestartAuthority);
    run("DisconnectAndRejoin", DisconnectAndRejoin);
    run("RejectMidMissionAndVersion", RejectMidMissionAndVersion);
    run("VisibilityAndCoverOnTheWire", VisibilityAndCoverOnTheWire);
    run("HideoutsDraggingAndDistractionsOnTheWire", HideoutsDraggingAndDistractionsOnTheWire);
    run("AuthoritativeDecoyGuideOnTheWire", AuthoritativeDecoyGuideOnTheWire);
    run("HiddenFootstepsOnTheWire", HiddenFootstepsOnTheWire);
    run("CqcAndPushOnTheWire", CqcAndPushOnTheWire);
    run("PuzzleOwnershipAndDoorsOnTheWire", PuzzleOwnershipAndDoorsOnTheWire);
    run("CoordinationAndPhysicalLeversOnTheWire", CoordinationAndPhysicalLeversOnTheWire);
    run("ManeuversAimAndSuppressorsOnTheWire", ManeuversAimAndSuppressorsOnTheWire);
    run("CameraTrapAndIntelligenceOnTheWire", CameraTrapAndIntelligenceOnTheWire);
    run("RejectInvalidPuzzleCommands", RejectInvalidPuzzleCommands);
    run("TrainingAndMissionRulesOnTheWire", TrainingAndMissionRulesOnTheWire);
    run("RejectInvalidCommandAndFlood", RejectInvalidCommandAndFlood);
    std::cout << "PASS sesión: sala, autoridad, privacidad, mandos, pausa, desconexión y loopback de dos participantes\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL sesión: " << error.what() << '\n';
    return 1;
  }
}
