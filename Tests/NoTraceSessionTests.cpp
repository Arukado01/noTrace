#include "NoTraceSession.h"
#include "Config/NoTraceGameplayConfig.h"
#include "NoTraceCombatGeometry.h"
#include "NoTraceWorldGeometry.h"
#include "Config/NoTraceDistractionConfig.h"

#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/System/Time.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  using Json = nlohmann::json;

  void Require(bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
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
      Until([&] { return client.IsConnected() && client.GetWorld().agents.size() == 2 && host.GetLobbyPlayers().size() == 2; },
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
      Until([&] { return host.CanDeploy(); }, [&] { Tick(); });
      host.Deploy();
      Until([&] { return client.GetWorld().phase == GamePhase::Mission; }, [&] { Tick(); });
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
    Until([&] { return FindAgent(pair.host.GetWorld(), 2).loadout.lethalAmmo == GameplayConfig::MaximumAmmo; }, [&] { pair.Tick(); });
    Require(!pair.host.CanDeploy(), "El cambio de contrato/equipo no invalidó preparación");
    pair.Deploy();
    Require(pair.host.GetWorld().contract == ContractId::DoubleCross && pair.client.GetWorld().contract == ContractId::DoubleCross,
            "Contrato no replicado");
    pair.client.ReturnToBase();
    pair.Advance(4);
    Require(pair.host.GetWorld().phase == GamePhase::Mission, "El cliente devolvió la misión a la base");
    pair.host.ReturnToBase();
    Until([&] { return pair.client.GetWorld().phase == GamePhase::Base; }, [&] { pair.Tick(); });
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
    Until([&] { return pair.client.IsConnected() && pair.host.GetLobbyPlayers().size() == 2 && pair.client.GetWorld().agents.size() == 2; },
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
      pair.host.Deploy();
      pair.Tick();
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
    Require(pair.client.IsConnected() && pair.client.GetWorld().agents.size() == 2 && pair.client.GetWorld().seed == seed, "El reinicio perdió participantes o no se propagó al cliente");
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
    ActionCommand interact; interact.interact = true; interact.dragBody = true;
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
    Guard body; body.id = 77; body.position = FindAgent(world, 2).position + sf::Vector2f{20.f, 0.f};
    body.state = GuardState::Incapacitated; body.stun = 100.f; body.incapacityRemaining = 80.f; world.guards.push_back(body);
    ActionCommand drag; drag.dragBody = true; pair.client.Submit(drag);
    Until([&] { return FindAgent(world, 2).draggingGuard == 77; }, [&] { pair.Tick(true); });
    drag.moveY = 1.f; pair.client.Submit(drag); pair.Advance(15, true);
    Require(Distance(world.guards[0].position, FindAgent(world, 2).position) <= GameplayConfig::BodyDragOffset + 1.f, "El cuerpo LAN no sigue al agente");
    pair.client.Submit({});
    Until([&] { return FindAgent(world, 2).draggingGuard < 0; }, [&] { pair.Tick(true); });
    const auto released = world.guards[0].position;
    pair.Advance(8, true);
    Require(world.guards[0].position == released, "Soltar E dejó un cuerpo LAN bloqueado en arrastre");
    pair.client.Submit(interact);
    Until([&] { return world.guards[0].bodyHidden; }, [&] { pair.Tick(true); });
    pair.client.Submit({}); pair.Advance(10, true);
    Require(world.objects[0].occupantGuard == 77 && world.guards[0].storedIn == 90, "El host no conserva la ocupación del cuerpo almacenado");
    Require(pair.client.GetWorld().guards.empty(), "La instantánea reveló el cuerpo almacenado");
    Require(!pair.client.GetWorld().objects.empty() && pair.client.GetWorld().objects[0].occupied && pair.client.GetWorld().objects[0].occupantGuard == 0, "El cliente no distingue un contenedor ocupado o recibe la identidad del cuerpo");
    world.objects.clear(); world.guards.clear(); world.noises.clear(); remote.position = {150.f, 350.f}; remote.actionCooldown = 0.f; remote.hideoutId = 0; remote.hidden = false;
    ActionCommand decoy; decoy.aimX = 1.f; decoy.throwDecoy = true; decoy.decoyDistance = 300.f;
    const int decoys = remote.loadout.decoys;
    pair.client.Submit(decoy); pair.client.Submit({});
    Until([&] { return remote.loadout.decoys == decoys - 1; }, [&] { pair.Tick(true); });
    Require(!world.noises.empty() && std::abs(world.noises.back().position.x - 450.f) < 1.f, "Un señuelo pendiente perdió su dirección o distancia en la red");
    pair.Advance(50, true); world.noises.clear(); remote.position = {150.f, 180.f}; remote.facing = {1.f, 0.f}; remote.actionCooldown = 0.f;
    world.obstacles.push_back({{{170.f, 100.f}, {20.f, 160.f}}, CoverKind::High, true, "Pared"});
    ActionCommand knock; knock.knock = true; knock.aimX = 1.f; pair.client.Submit(knock);
    Until([&] { return !world.noises.empty(); }, [&] { pair.Tick(true); });
    Require(std::abs(world.noises.back().position.x - 170.f) < 0.1f && world.noises.back().actor == 2, "El golpe LAN no nace en la pared validada");
  }

  void AuthoritativeDecoyGuideOnTheWire() {
    Pair pair; pair.Deploy();
    auto &world = const_cast<WorldState &>(pair.host.GetWorld());
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.noises.clear(); world.events.clear(); world.footsteps.clear();
    world.agents[0].position = {150.f, 450.f}; world.agents[0].facing = {1.f, 0.f};
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
    Require(!world.noises.empty() && world.noises.back().position == destination, "El lanzamiento LAN difiere de su guía validada por el host");
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
    SoloPauseAndFiniteInput();
    LobbyAndAuthority();
    PrivateSnapshotAndClientDoesNotSimulate();
    PausedHostStillRunsRemoteAgent();
    SequenceTimeoutIdentityAndPulses();
    PendingShotKeepsItsAim();
    AimDirectionAndRestartAuthority();
    DisconnectAndRejoin();
    RejectMidMissionAndVersion();
    VisibilityAndCoverOnTheWire();
    HideoutsDraggingAndDistractionsOnTheWire();
    AuthoritativeDecoyGuideOnTheWire();
    HiddenFootstepsOnTheWire();
    RejectInvalidCommandAndFlood();
    std::cout << "PASS sesión: sala, autoridad, privacidad, mandos, pausa, desconexión y loopback de dos participantes\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL sesión: " << error.what() << '\n';
    return 1;
  }
}
