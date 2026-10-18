#pragma once

#include "Core/EngineContext.h"
#include "NoTraceWorldRenderer.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace EngineTests {
  template<class Capture>
  nlohmann::json RunRendererPerformanceFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    const auto identical = [](const sf::Image &first, const sf::Image &second) {
      const auto size = first.getSize();
      return size == second.getSize() && std::equal(first.getPixelsPtr(), first.getPixelsPtr() + static_cast<std::size_t>(size.x) * size.y * 4, second.getPixelsPtr());
    };
    WorldState world; world.phase = GamePhase::Base; world.size = {160.f, 160.f};
    Agent agent; agent.id = 1; agent.name = "Élite 0"; agent.position = {80.f, 80.f}; agent.facing = {1.f, 0.f}; agent.weaponAim = agent.facing;
    world.agents.push_back(agent);
    WorldRenderer renderer(context); renderer.SetNamesVisible(true);
    unsigned int checks = 0;
    auto samples = nlohmann::json::array();
    const auto compare = [&](const std::string &name) {
      const auto cachedFile = "renderer-label-" + name + "-cached.png", freshFile = "renderer-label-" + name + "-fresh.png";
      const auto cached = capture(renderer, world, output / cachedFile, false);
      WorldRenderer fresh(context); fresh.SetNamesVisible(true);
      const auto reference = capture(fresh, world, output / freshFile, false);
      const bool matches = identical(cached, reference);
      ++checks; samples.push_back({{"name", name}, {"cached", cachedFile}, {"fresh", freshFile}, {"identicalRgba", matches}});
      if (!matches) throw std::runtime_error("La caché de rótulos difiere del renderer fresco: " + name);
    };
    compare("initial");
    for (unsigned int index = 1; index <= 270; ++index) {
      world.agents[0].name = "Élite " + std::to_string(index);
      renderer.Render(world, 1, false);
      if (index == 128) compare("name-128");
      if (index == 270) compare("name-270");
    }
    world.agents[0].name = agent.name;
    Interactable camera; camera.id = 91; camera.kind = ObjectKind::Camera; camera.position = {100.f, 60.f}; camera.active = true;
    camera.camera.state = CameraState::Operating; camera.camera.perception = CameraPerception::Searching;
    world.objects.push_back(camera);
    compare("return-and-searching-camera");
    world.objects.clear();
    world.agents[0].name = "?";
    auto ally = agent; ally.id = 2; ally.position = {115.f, 80.f}; world.agents.push_back(ally);
    compare("same-text-new-size-and-color");
    nlohmann::json report{{"checks", checks}, {"distinctUtf8Names", 271}, {"samples", samples},
      {"scope", "API pública; 270 nombres nuevos, retorno, signo de cámara16 a nombre18 y nombre propio verde a aliado cian; RGBA completo contra renderer fresco"}};
    std::ofstream(output / "renderer-performance-report.json", std::ios::binary) << report.dump(2) << '\n';
    return report;
  }
}
