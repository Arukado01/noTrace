#pragma once

#include "NoTraceCoordination.h"
#include "NoTraceSimulation.h"
#include "NoTraceSpatialFixture.h"
#include "NoTraceWorldRenderer.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/Image.hpp>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunCoordinationRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    unsigned int checks = 0;
    const auto require = [&](bool valid, const char *message) {
      ++checks;
      if (!valid) throw std::runtime_error(std::string("Panel cooperativo: ") + message);
    };
    Simulation simulation; simulation.ResetBase(); simulation.AddPlayer(2, "Segundo operador");
    auto &world = const_cast<WorldState &>(simulation.GetState()); TestFixture::LegacyPlane(world);
    world.objects.clear(); world.obstacles.clear(); world.guards.clear(); world.securityEntries.clear();
    world.agents[0].position = {280.f, 280.f}; world.agents[1].position = {320.f, 350.f};
    Interactable relay{90, ObjectKind::CoopSwitch, {320.f, 300.f}, "Panel sincronizado"};
    relay.footprint = {44.f, 34.f}; relay.solid = SpatialOverride::Disabled; relay.blocksVision = SpatialOverride::Disabled;
    relay.puzzle.kind = PuzzleKind::Coordination; relay.puzzle.family = PuzzleFamily::Coordination;
    world.objects.push_back(relay);
    WorldRenderer renderer(context);
    EnvironmentArt art(context);
    const auto logical = context.window.GetLogicalSize();
    const auto focus = WorldRenderer::ProjectGround(world.agents[0].position, world.agents[0].position, logical);
    const auto feet = WorldRenderer::ProjectGround(world.objects[0].position, world.agents[0].position, logical);
    const auto project = [&](sf::Vector2f sourcePoint) {
      const auto unscaled = art.ObjectPoint(world.objects[0], feet, sourcePoint);
      return focus + (unscaled - focus) * renderer.GetZoom();
    };
    const auto first = project(PresentationConfig::CoordinationButtonSockets[0]);
    const auto second = project(PresentationConfig::CoordinationButtonSockets[1]);
    const sf::Image atlas("Content/Textures/NoTrace/Environment/MilitaryFurniture.png");
    const auto redButton = atlas.getPixel({576u + 76u, 384u + 103u}), greenButton = atlas.getPixel({576u + 128u, 384u + 93u});
    require(redButton.a >= 250 && redButton.r > redButton.g * 2 && greenButton.a >= 250 && greenButton.g > greenButton.r * 2,
      "los sockets de estado no caen sobre los pulsadores rojo y verde del PNG real");
    const auto wire = (first + second) * .5f + sf::Vector2f{0.f, 9.f * renderer.GetZoom()};
    const auto hasColor = [&](const sf::Image &image, sf::Vector2f point, sf::Color expected) {
      const auto pixel = point.componentWiseDiv(logical).componentWiseMul(sf::Vector2f(image.getSize()));
      for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        const int x = static_cast<int>(std::round(pixel.x)) + dx, y = static_cast<int>(std::round(pixel.y)) + dy;
        if (x >= 0 && y >= 0 && x < static_cast<int>(image.getSize().x) && y < static_cast<int>(image.getSize().y) &&
            image.getPixel({static_cast<unsigned int>(x), static_cast<unsigned int>(y)}) == expected) return true;
      }
      return false;
    };
    nlohmann::json states = nlohmann::json::array();
    const auto snapshot = [&](const char *name) {
      const auto &device = world.objects[0];
      const auto image = capture(renderer, world, output / (std::string("coordination-") + name + ".png"), false);
      states.push_back({{"state", name}, {"pressedMask", device.puzzle.coordinationMask}, {"reservedMask", device.puzzle.coordinationReservedMask},
        {"progress", device.puzzle.coordinationProgress}, {"completed", device.completed}});
      return image;
    };
    const auto available = snapshot("available");
    require((first - second).length() > 30.f, "los dos puntos físicos no se distinguen en pantalla");
    require(hasColor(available, first, PresentationConfig::Cyan) && hasColor(available, second, PresentationConfig::Cyan), "faltan botones disponibles separados");
    require(!hasColor(available, wire, PresentationConfig::Green), "el circuito aparece energizado antes de operar");
    ActionCommand interact; interact.interact = true;
    simulation.Step(.1f, {{1, interact}, {2, interact}});
    require(world.agents[0].puzzle.target == 90 && world.agents[1].puzzle.target == 90 &&
      world.agents[0].puzzle.coordinationButton != world.agents[1].puzzle.coordinationButton, "dos operadores no reservan controles distintos");
    const auto reserved = snapshot("reserved");
    require(world.objects[0].puzzle.coordinationReservedMask == 3 && world.objects[0].puzzle.coordinationMask == 0 &&
      hasColor(reserved, first, PresentationConfig::Amber) && hasColor(reserved, second, PresentationConfig::Amber), "los controles reservados no muestran su ocupación real");
    ActionCommand hold; hold.puzzleTarget = 90; hold.puzzleHeld = true;
    simulation.Step(.1f, {{1, hold}});
    const auto partial = snapshot("one-pressed");
    const auto ownButton = world.agents[0].puzzle.coordinationButton;
    const auto ownPoint = ownButton == 1 ? first : second, otherPoint = ownButton == 1 ? second : first;
    require(world.objects[0].puzzle.coordinationMask == Coordination::ButtonBit(ownButton) && world.objects[0].puzzle.coordinationProgress == 0.f,
      "la pulsación no corresponde al botón asignado o completa la sincronización sin compañero");
    require(hasColor(partial, ownPoint, PresentationConfig::Green) && hasColor(partial, otherPoint, PresentationConfig::Amber), "la primera pulsación no distingue el botón presionado del reservado");
    require(!hasColor(partial, wire, PresentationConfig::Green), "se ilumina la conexión incompleta");
    simulation.Step(.1f, {{1, hold}, {2, hold}});
    const auto connected = snapshot("connected");
    require(world.objects[0].puzzle.coordinationMask == 3 && world.objects[0].puzzle.coordinationProgress > 0.f, "faltan las dos pulsaciones autoritativas");
    require(hasColor(connected, first, PresentationConfig::Green) && hasColor(connected, second, PresentationConfig::Green), "un control mantenido no cambia visualmente");
    require(hasColor(connected, wire, PresentationConfig::Green), "no se dibuja la señal entre los controles activos");
    simulation.Step(.1f, {{1, hold}});
    const auto interrupted = snapshot("interrupted");
    require(world.objects[0].puzzle.coordinationProgress == 0.f && !hasColor(interrupted, wire, PresentationConfig::Green), "la señal persiste tras soltar un control");
    for (unsigned int frame = 0; frame < 22; ++frame) simulation.Step(.1f, {{1, hold}, {2, hold}});
    const auto completed = snapshot("completed");
    require(world.objects[0].completed && hasColor(completed, wire, PresentationConfig::Green), "el circuito resuelto no conserva su conexión real");
    nlohmann::json result{{"checks", checks}, {"states", states}, {"physicalButtons", 2}, {"authoritativeCommands", true}};
    std::ofstream(output / "coordination-visual-review.json") << result.dump(2);
    return result;
  }
}
