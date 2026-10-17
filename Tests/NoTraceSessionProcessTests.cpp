#include "NoTraceSession.h"
#include "NoTraceCombatGeometry.h"

#include <SFML/System/Clock.hpp>
#include <SFML/System/Sleep.hpp>
#include <SFML/System/Time.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
  using namespace NoTrace;

  void Require(bool condition, const char *message) {
    if (!condition) {
      throw std::runtime_error(message);
    }
  }

  const Agent &FindAgent(const WorldState &world, PlayerId id) {
    const auto found = std::ranges::find_if(world.agents, [id](const Agent &agent) { return agent.id == id; });
    Require(found != world.agents.end(), "No existe el agente esperado");
    return *found;
  }

  struct Node {
    NetworkManager network;
    RandomManager random;
    Session session{network, random};
    sf::Clock frame;

    void Tick() {
      session.Tick(std::min(frame.restart().asSeconds(), 0.05f));
      sf::sleep(sf::milliseconds(5));
    }

    void Until(const std::function<bool()> &condition, const std::function<void()> &beforeTick = {}) {
      sf::Clock deadline;
      for (;;) {
        if (session.IsClient() && session.GetStatus().starts_with("Error LAN:")) {
          throw std::runtime_error(session.GetStatus());
        }
        if (condition()) {
          return;
        }
        Require(deadline.getElapsedTime().asSeconds() < 15.f, "Tiempo agotado entre procesos de sesión");
        if (beforeTick) {
          beforeTick();
        }
        Tick();
      }
    }

    void Advance(float seconds) {
      sf::Clock duration;
      while (duration.getElapsedTime().asSeconds() < seconds) {
        Tick();
      }
    }

    void SubmitTraitorAction(bool &fired) {
      ActionCommand command;
      const auto &world = session.GetWorld();
      if (!session.IsConnected()) {
        throw std::runtime_error(session.GetStatus());
      }
      const auto &local = FindAgent(world, session.GetLocalPlayerId());
      if (world.phase == GamePhase::Mission && world.elapsed >= 0.65f && local.privateOrder == PrivateOrder::Traitor) {
        const auto other = std::ranges::find_if(world.agents, [&](const Agent &agent) { return agent.id != local.id && agent.connected && agent.alive; });
        Require(other != world.agents.end(), "El traidor no encontró otro agente");
        const auto head = CombatGeometry::ActorVolumes(other->position, other->facing, other->stance).front().center;
        auto direction = head - local.position;
        const float length = std::hypot(direction.x, direction.y);
        Require(length > 0.f, "Los agentes ocuparon exactamente la misma posición");
        direction /= length;
        command.aimX = direction.x;
        command.aimY = direction.y;
        const float headHeight = CombatGeometry::HeadCenterHeight(other->stance) + (other->stance == Stance::Prone ? 0.f : CombatConfig::HeadHeight * 0.25f);
        command.aimSlope = (headHeight - CombatGeometry::WeaponHeight(local.stance)) / length;
        if (!fired) {
          command.fireLethal = true;
          fired = true;
        }
      }
      session.Submit(command);
    }
  };

  int Host(std::uint16_t port) {
    Node node;
    Require(node.session.Host("Proceso anfitrión", port), "No se pudo abrir la sesión entre procesos");
    std::cout << "PORT=" << node.network.GetPort() << std::endl;
    node.Until([&] { return node.session.GetLobbyPlayers().size() == 2; });
    const auto hostPosition = FindAgent(node.session.GetWorld(), 1).position;
    const auto clientPosition = FindAgent(node.session.GetWorld(), 2).position;
    node.session.SetContract(ContractId::DoubleCross);
    node.session.SetReady(true);
    node.Until([&] {
      const auto &client = FindAgent(node.session.GetWorld(), 2);
      return node.session.CanDeploy() && client.position.x > clientPosition.x + 10.f && client.stance == Stance::Prone;
    });
    Require(node.session.GetWorld().contract == ContractId::DoubleCross, "El cliente modificó el contrato del anfitrión");
    Require(FindAgent(node.session.GetWorld(), 1).position == hostPosition, "El mando del cliente movió al anfitrión");
    Require(node.session.GetWorld().objectives == 0, "Los objetivos cambiaron durante la preparación");
    std::cout << "PASS host: identidad, sala preparada y arrastre automatico autoritativo del cliente" << std::endl;
    node.session.Deploy();
    Require(node.session.GetWorld().phase == GamePhase::Mission, "No se desplegó desde la base");
    bool fired = false;
    node.Until([&] { return node.session.GetWorld().phase == GamePhase::Results; }, [&] { node.SubmitTraitorAction(fired); });
    const auto &world = node.session.GetWorld();
    Require(world.results.size() == 2 && world.alarmCount == 0, "No se cerró la operación autoritativa sin alarma");
    for (const auto &agent : world.agents) {
      const auto found = world.results.find(agent.id);
      Require(found != world.results.end(), "Falta un informe del anfitrión");
      Require(found->second.traitorVictory == (agent.privateOrder == PrivateOrder::Traitor), "Resultado privado incorrecto");
      Require(agent.alive || agent.health == 0.f, "La eliminacion autoritativa no agoto la salud");
      std::cout << "RESULT host player=" << agent.id << " seed=" << world.seed << " score=" << found->second.score
                << " traitorVictory=" << found->second.traitorVictory << " health=" << agent.health << " stamina=" << agent.stamina << std::endl;
    }
    node.Advance(1.f);
    node.session.ReturnToBase();
    node.Until([&] {
      const auto &players = node.session.GetLobbyPlayers();
      return std::ranges::any_of(players, [](const LobbyPlayer &player) { return player.id == 2 && player.ready; });
    });
    Require(node.session.GetWorld().phase == GamePhase::Base, "El equipo no regresó a la base");
    node.Until([&] { return node.session.GetLobbyPlayers().size() == 1; });
    Require(!FindAgent(node.session.GetWorld(), 2).connected, "El cliente finalizado sigue conectado");
    node.session.Stop();
    std::cout << "PASS host: despliegue, informe, regreso a base y desconexión entre procesos" << std::endl;
    return 0;
  }

  int Client(const std::string &ip, std::uint16_t port) {
    Node node;
    Require(node.session.Join(ip, port, "Proceso cliente"), "No se inició cliente entre procesos");
    node.Until([&] {
      return node.session.IsConnected() && node.session.GetLobbyPlayers().size() == 2 &&
             std::ranges::any_of(node.session.GetWorld().agents, [&](const Agent &agent) { return agent.id == node.session.GetLocalPlayerId(); }) &&
             node.session.GetWorld().contract == ContractId::DoubleCross;
    });
    Require(node.session.GetLocalPlayerId() == 2, "No se recibió la identidad asignada");
    node.session.SetContract(ContractId::GhostArchive);
    node.session.Deploy();
    const auto origin = FindAgent(node.session.GetWorld(), 2).position;
    ActionCommand move;
    move.moveX = 1.f;
    move.prone = true;
    node.session.Submit(move);
    node.Advance(0.45f);
    node.session.Submit({});
    node.Until([&] { return FindAgent(node.session.GetWorld(), 2).position.x > origin.x + 10.f; });
    Require(FindAgent(node.session.GetWorld(), 2).stance == Stance::Prone, "El arrastre perdio la postura sin pulsar crawl");
    Require(node.session.GetWorld().phase == GamePhase::Base && node.session.GetWorld().contract == ContractId::DoubleCross,
            "El cliente logró cambiar contrato o desplegar");
    node.session.SetReady(true);
    node.Until([&] { return node.session.GetWorld().phase == GamePhase::Mission; });
    Require(!node.session.RestartOperation(), "El cliente pudo reiniciar la operacion del anfitrion");
    const auto ownOrder = FindAgent(node.session.GetWorld(), 2).privateOrder;
    Require(ownOrder != PrivateOrder::None, "El cliente no recibió su orden privada");
    Require(FindAgent(node.session.GetWorld(), 1).privateOrder == PrivateOrder::None, "Se filtró la orden del anfitrión");
    node.session.ReturnToBase();
    Require(node.session.GetWorld().phase == GamePhase::Mission, "El cliente regresó a base sin el anfitrión");
    bool fired = false;
    node.Until([&] { return node.session.GetWorld().phase == GamePhase::Results; }, [&] { node.SubmitTraitorAction(fired); });
    const auto &world = node.session.GetWorld();
    Require(world.results.size() == 2 && world.results.contains(2), "El cliente no recibió los informes finales");
    const auto result = world.results.at(2);
    Require(result.traitorVictory == (ownOrder == PrivateOrder::Traitor), "El informe del cliente no coincide con su orden");
    Require(FindAgent(world, 1).privateOrder == PrivateOrder::None, "El informe filtró una orden privada ajena");
    for (const auto &agent : world.agents) Require(agent.alive || agent.health == 0.f, "El snapshot de eliminacion conservo salud");
    std::cout << "RESULT client player=2 seed=" << world.seed << " score=" << result.score
              << " traitorVictory=" << result.traitorVictory << " health=" << FindAgent(world, 2).health
              << " stamina=" << FindAgent(world, 2).stamina << std::endl;
    node.Until([&] { return node.session.GetWorld().phase == GamePhase::Base; });
    node.session.SetReady(true);
    node.Advance(0.15f);
    node.session.Stop();
    std::cout << "PASS client: mundo remoto, privacidad, autoridad, resultados y regreso a base" << std::endl;
    return 0;
  }

  std::uint16_t ParsePort(const std::string &text, bool allowZero) {
    unsigned int port = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), port);
    Require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && port <= 65535 && (allowZero || port != 0), "Puerto no válido");
    return static_cast<std::uint16_t>(port);
  }
}

int main(int argc, char **argv) {
  try {
    if (argc == 3 && std::string(argv[1]) == "--host") {
      return Host(ParsePort(argv[2], true));
    }
    if (argc == 4 && std::string(argv[1]) == "--client") {
      return Client(argv[2], ParsePort(argv[3], false));
    }
    throw std::runtime_error("Uso: NoTraceSessionProcessTests --host puerto | --client IPv4 puerto");
  } catch (const std::exception &error) {
    std::cerr << "FAIL sesión entre procesos: " << error.what() << std::endl;
    return 1;
  }
}
