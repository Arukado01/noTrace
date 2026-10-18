#pragma once

#include "Core/EngineContext.h"
#include "NoTraceInteraction.h"
#include "NoTraceSimulation.h"
#include "NoTraceWorldRenderer.h"
#include <SFML/Graphics/RenderWindow.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunCleanHudRenderFixtures(EngineContext &context, sf::RenderWindow &window, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("HUD limpio: " + message);
    };
    const auto identical = [](const sf::Image &first, const sf::Image &second) {
      const auto size = first.getSize();
      return size == second.getSize() && std::equal(first.getPixelsPtr(), first.getPixelsPtr() + static_cast<std::size_t>(size.x) * size.y * 4, second.getPixelsPtr());
    };
    const auto changedIn = [&](const sf::Image &first, const sf::Image &second, sf::FloatRect logical, std::optional<sf::Color> color = std::nullopt) {
      const auto start = window.mapCoordsToPixel(logical.position, context.window.GetPresentationView());
      const auto end = window.mapCoordsToPixel(logical.position + logical.size, context.window.GetPresentationView());
      int changed = 0;
      for (int y = std::max(0, start.y); y < std::min(static_cast<int>(first.getSize().y), end.y); ++y)
        for (int x = std::max(0, start.x); x < std::min(static_cast<int>(first.getSize().x), end.x); ++x) {
          const sf::Vector2u point{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
          if (first.getPixel(point) != second.getPixel(point) && (!color || first.getPixel(point) == *color)) ++changed;
        }
      return changed;
    };
    WorldRenderer renderer(context);
    require(!renderer.NamesVisible(), "los nombres deben estar ocultos al construir el renderizador");
    WorldState world; world.phase = GamePhase::Base; world.size = {4000.f, 4000.f};
    Agent observer; observer.id = 1; observer.name = "LOCAL"; observer.position = {2000.f, 2000.f}; world.agents.push_back(observer);
    Agent ally; ally.id = 2; ally.name = "ALIADO"; ally.position = observer.position + sf::Vector2f{45.f, 0.f}; world.agents.push_back(ally);
    const std::array<sf::Vector2f, 4> farOffsets{{{1000.f, -1000.f}, {-1000.f, 1000.f}, {-1000.f, -1000.f}, {1000.f, 1000.f}}};
    for (std::size_t index = 0; index < farOffsets.size(); ++index) {
      auto distant = ally; distant.id = static_cast<PlayerId>(3 + index); distant.name = "BORDE " + std::to_string(index + 1);
      distant.position = observer.position + farOffsets[index]; world.agents.push_back(distant);
    }
    nlohmann::json names = nlohmann::json::array();
    for (int zoomIndex = 0; zoomIndex < 3; ++zoomIndex) {
      renderer.ResetZoom(); if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
      renderer.SetNamesVisible(false);
      const auto prefix = "clean-hud-names-" + std::to_string(zoomIndex);
      const auto baseline = capture(renderer, world, output / (prefix + "-hidden.png"), true);
      auto renamed = world;
      for (auto &agent : renamed.agents) agent.name = "NOMBRE CAMBIADO " + std::to_string(agent.id);
      require(identical(baseline, capture(renderer, renamed, output / (prefix + "-hidden-renamed.png"), true)), "un nombre oculto altera los píxeles");
      renderer.SetNamesVisible(true);
      const auto shown = capture(renderer, world, output / (prefix + "-held.png"), true);
      const auto logical = context.window.GetLogicalSize();
      const auto labelAt = [&](const Agent &agent) {
        auto label = WorldRenderer::ProjectGround(agent.position, observer.position, logical, renderer.GetZoom()) - sf::Vector2f{0.f, 72.f * renderer.GetZoom()};
        const float margin = std::min(220.f, logical.x * 0.2f);
        label.x = std::clamp(label.x, margin, logical.x - margin); label.y = std::clamp(label.y, 142.f, logical.y - 124.f);
        return label;
      };
      const auto localLabel = labelAt(world.agents[0]), allyLabel = labelAt(world.agents[1]);
      const int localPixels = changedIn(shown, baseline, {localLabel - sf::Vector2f{90.f, 2.f}, {180.f, 32.f}}, PresentationConfig::Green);
      const int allyPixels = changedIn(shown, baseline, {allyLabel - sf::Vector2f{90.f, 2.f}, {180.f, 32.f}}, PresentationConfig::Cyan);
      require(localPixels > 0 && allyPixels > 0, "TAB mantenido no dibuja los nombres local y aliado en sus posiciones proyectadas");
      nlohmann::json arrows = nlohmann::json::array();
      const auto center = logical.componentWiseMul(PresentationConfig::CameraFocusRatio);
      const sf::Vector2f minimum{48.f, 142.f}, maximum = logical - sf::Vector2f{48.f, 100.f};
      for (std::size_t index = 2; index < world.agents.size(); ++index) {
        const auto ground = WorldRenderer::ProjectGround(world.agents[index].position, observer.position, logical, renderer.GetZoom());
        require(ground.x < minimum.x || ground.x > maximum.x || ground.y < minimum.y || ground.y > maximum.y, "el aliado del fixture no está fuera del encuadre");
        const auto direction = (ground - center).normalized();
        const float horizontal = direction.x > 0.f ? (maximum.x - center.x) / direction.x : direction.x < 0.f ? (minimum.x - center.x) / direction.x : std::numeric_limits<float>::infinity();
        const float vertical = direction.y > 0.f ? (maximum.y - center.y) / direction.y : direction.y < 0.f ? (minimum.y - center.y) / direction.y : std::numeric_limits<float>::infinity();
        const auto tip = center + direction * std::min(horizontal, vertical);
        const int pixels = changedIn(shown, baseline, {tip - sf::Vector2f{27.f, 27.f}, {54.f, 54.f}}, PresentationConfig::Cyan);
        require(pixels > 0, "falta la flecha cian del aliado fuera del encuadre");
        arrows.push_back({{"agent", world.agents[index].id}, {"tip", {tip.x, tip.y}}, {"cyanPixels", pixels}});
      }
      renderer.SetNamesVisible(false);
      require(!renderer.NamesVisible() && identical(baseline, capture(renderer, world, output / (prefix + "-released.png"), true)), "soltar TAB no restaura exactamente el PNG inicial");
      names.push_back({{"zoom", renderer.GetZoom()}, {"hidden", prefix + "-hidden.png"}, {"held", prefix + "-held.png"}, {"released", prefix + "-released.png"},
        {"localNamePixels", localPixels}, {"allyNamePixels", allyPixels}, {"arrows", arrows}, {"restoredPixelExact", true}});
    }

    renderer.ResetZoom(); renderer.SetNamesVisible(true);
    WorldState fog; fog.phase = GamePhase::Mission; fog.size = {1000.f, 1000.f};
    observer.position = {400.f, 400.f}; observer.facing = {1.f, 0.f}; fog.agents.push_back(observer);
    ally.id = 2; ally.position = {500.f, 400.f}; fog.agents.push_back(ally);
    Guard enemy; enemy.id = 41; enemy.position = {520.f, 400.f}; enemy.facing = {-1.f, 0.f}; fog.guards.push_back(enemy);
    fog.obstacles.push_back({{{445.f, 300.f}, {14.f, 200.f}}, CoverKind::High, true, "Pared del fixture"});
    require(!Visibility::IsVisible(fog, fog.agents[0], ally.position) && !Visibility::GuardVisible(fog, fog.agents[0], enemy), "la pared del fixture no oculta las entidades");
    const auto occluded = capture(renderer, fog, output / "clean-hud-wall-occluded.png", true);
    auto redacted = fog; redacted.agents.resize(1); redacted.guards.clear();
    require(identical(occluded, capture(renderer, redacted, output / "clean-hud-wall-redacted.png", true)), "TAB revela nombres, flechas, minimapa o enemigos detrás de una pared");
    renderer.SetNamesVisible(false);
    auto objects = world; objects.agents.resize(1);
    Interactable terminal; terminal.id = 71; terminal.kind = ObjectKind::Terminal; terminal.position = objects.agents[0].position + sf::Vector2f{70.f, 0.f}; terminal.name = "TERMINAL DEL FIXTURE";
    objects.objects.push_back(terminal);
    const auto objectBaseline = capture(renderer, objects, output / "clean-hud-object-original.png", true);
    objects.objects[0].name = "ESTE NOMBRE NO DEBE APARECER FLOTANDO SOBRE EL OBJETO";
    require(identical(objectBaseline, capture(renderer, objects, output / "clean-hud-object-renamed.png", true)), "el nombre de un objeto sigue apareciendo sobre el mundo");
    renderer.SetNamesVisible(true);
    const auto objectHeld = capture(renderer, objects, output / "clean-hud-object-held.png", true);
    objects.objects[0].name = terminal.name;
    require(identical(objectHeld, capture(renderer, objects, output / "clean-hud-object-held-renamed.png", true)), "TAB también revela etiquetas de objetos");
    renderer.SetNamesVisible(false);

    WorldState waves; waves.phase = GamePhase::Base; waves.size = {1000.f, 1000.f}; waves.agents.push_back(observer);
    const NoiseEvent event{observer.position + sf::Vector2f{75.f, 75.f}, 100.f, GameplayConfig::NoiseDuration, observer.id, 0, SoundKind::Impact, GameplayConfig::NoiseDuration};
    const auto quiet = capture(renderer, waves, output / "clean-hud-noise-quiet.png", false);
    nlohmann::json noiseFrames = nlohmann::json::array();
    float previousRadius = 0.f;
    for (int frame = 0; frame < 2; ++frame) {
      waves.noises = {event}; waves.noises[0].remaining *= frame == 0 ? 0.8f : 0.35f;
      const auto original = waves.noises[0];
      const auto file = "clean-hud-noise-" + std::to_string(frame) + ".png";
      const auto image = capture(renderer, waves, output / file, false);
      const auto logical = context.window.GetLogicalSize();
      const auto center = WorldRenderer::ProjectGround(event.position, observer.position, logical, renderer.GetZoom());
      require(changedIn(image, quiet, {center - sf::Vector2f{8.f, 8.f}, {16.f, 16.f}}) == 0, "la onda conserva una cruz o marca en su centro");
      float radius = 0.f; int changed = 0;
      for (unsigned int y = 0; y < image.getSize().y; ++y) for (unsigned int x = 0; x < image.getSize().x; ++x) {
        if (image.getPixel({x, y}) == quiet.getPixel({x, y})) continue;
        ++changed;
        const auto logicalPixel = window.mapPixelToCoords({static_cast<int>(x), static_cast<int>(y)}, context.window.GetPresentationView());
        radius = std::max(radius, (WorldRenderer::UnprojectGround(logicalPixel, observer.position, logical, renderer.GetZoom()) - event.position).length());
      }
      require(changed > 0 && radius > previousRadius, "las ondas no se expanden con la edad del evento");
      const float expectedRadius = event.radius * (1.f - original.remaining / original.duration);
      require(std::abs(radius - expectedRadius) <= PresentationConfig::PixelSize * 2.f, "la expansión no respeta el radio físico del evento y el ajuste a píxeles");
      require(waves.noises[0].position == original.position && waves.noises[0].radius == original.radius && waves.noises[0].remaining == original.remaining,
        "dibujar la onda modifica el evento autoritativo");
      noiseFrames.push_back({{"file", file}, {"remaining", original.remaining}, {"outerGroundRadius", radius}, {"expectedGroundRadius", expectedRadius}, {"changedPixels", changed}, {"centerPixels", 0}});
      previousRadius = radius;
    }
    waves.noises[0].remaining = 0.f;
    require(identical(quiet, capture(renderer, waves, output / "clean-hud-noise-expired.png", false)), "un evento vencido sigue dibujando ondas");

    Simulation simulation; simulation.ResetBase({{1, "LOCAL"}}); simulation.Deploy(0);
    auto &live = const_cast<WorldState &>(simulation.GetState());
    live.size = {1000.f, 1000.f}; live.obstacles.clear(); live.objects.clear(); live.guards.clear(); live.noises.clear(); live.sounds.clear(); live.events.clear(); live.securityEntries.clear();
    live.agents[0].position = {400.f, 400.f}; live.agents[0].footstepTime = GameplayConfig::RunningFootstepInterval;
    ActionCommand running; running.moveX = 1.f; running.run = true; simulation.Step(0.05f, {{1, running}});
    require(!live.noises.empty() && live.agents[0].moving && live.agents[0].running && live.noises[0].radius == GameplayConfig::RunningNoiseRadius && live.noises[0].radius == 306.f,
      "el movimiento real no produce el radio de carrera de 306");
    auto runningView = live; runningView.noises.resize(1); runningView.noises[0].remaining = runningView.noises[0].duration * 0.6f;
    const auto runningEvent = runningView.noises[0];
    const auto waveCenter = [&](const WorldState &view, const std::string &prefix) {
      auto silentView = view; silentView.noises.clear();
      const auto silent = capture(renderer, silentView, output / (prefix + "-quiet.png"), false);
      const auto noisy = capture(renderer, view, output / (prefix + "-wave.png"), false);
      sf::Vector2i lower{static_cast<int>(noisy.getSize().x), static_cast<int>(noisy.getSize().y)}, upper{-1, -1};
      for (unsigned int y = 0; y < noisy.getSize().y; ++y) for (unsigned int x = 0; x < noisy.getSize().x; ++x) if (noisy.getPixel({x, y}) != silent.getPixel({x, y})) {
        lower.x = std::min(lower.x, static_cast<int>(x)); lower.y = std::min(lower.y, static_cast<int>(y));
        upper.x = std::max(upper.x, static_cast<int>(x)); upper.y = std::max(upper.y, static_cast<int>(y));
      }
      require(upper.x >= lower.x && upper.y >= lower.y, "un paso real no produce píxeles de ondas");
      return window.mapPixelToCoords((lower + upper) / 2, context.window.GetPresentationView());
    };
    const auto actualCenter = waveCenter(runningView, "clean-hud-running");
    const auto logical = context.window.GetLogicalSize();
    const auto visualOrigin = runningEvent.position + runningEvent.visualDirection * PresentationConfig::FootstepVisualOffset;
    const auto expectedCenter = WorldRenderer::ProjectGround(visualOrigin, runningView.agents[0].position, logical, renderer.GetZoom());
    const auto physicalOrigin = WorldRenderer::ProjectGround(runningEvent.position, runningView.agents[0].position, logical, renderer.GetZoom());
    const auto onePixel = window.mapPixelToCoords({1, 1}, context.window.GetPresentationView()) - window.mapPixelToCoords({0, 0}, context.window.GetPresentationView());
    const float rasterBound = PresentationConfig::PixelSize * renderer.GetZoom() + std::max(std::abs(onePixel.x), std::abs(onePixel.y)) * 2.f;
    require((actualCenter - expectedCenter).length() <= rasterBound && (actualCenter - physicalOrigin).length() > rasterBound,
      "la onda de pasos no se desplaza visualmente hacia delante dentro de la cota del rasterizador");
    require(runningView.noises[0].position == runningEvent.position && live.noises[0].position == runningEvent.position,
      "el desplazamiento visual altera el origen físico del ruido");
    nlohmann::json stableWaveFrames = nlohmann::json::array();
    for (int frame = 0; frame < 2; ++frame) {
      ActionCommand turn; if (frame == 0) turn.moveY = -1.f;
      simulation.Step(0.05f, {{1, turn}});
      require(live.noises.size() == 1 && live.noises[0].position == runningEvent.position && live.noises[0].visualDirection == runningEvent.visualDirection &&
        live.agents[0].facing == sf::Vector2f{0.f, -1.f} && live.agents[0].moving == (frame == 0), "girar o parar modifica el evento anterior o añade un paso ficticio");
      auto view = live; view.noises[0].remaining = runningEvent.remaining;
      const auto prefix = frame == 0 ? "clean-hud-running-turned" : "clean-hud-running-stopped";
      const auto center = waveCenter(view, prefix);
      const auto expected = WorldRenderer::ProjectGround(visualOrigin, view.agents[0].position, logical, renderer.GetZoom());
      require((center - expected).length() <= rasterBound, "la onda viva cambia su centro al girar o detenerse el emisor");
      stableWaveFrames.push_back({{"state", frame == 0 ? "turned" : "stopped"}, {"capturedCenter", {center.x, center.y}}, {"expectedCenter", {expected.x, expected.y}},
        {"eventDirection", {view.noises[0].visualDirection.x, view.noises[0].visualDirection.y}}, {"bodyFacing", {view.agents[0].facing.x, view.agents[0].facing.y}}});
    }

    Simulation covered; covered.ResetBase(); covered.Deploy(0);
    auto &coverWorld = const_cast<WorldState &>(covered.GetState());
    coverWorld.size = {1000.f, 1000.f}; coverWorld.obstacles.clear(); coverWorld.objects.clear(); coverWorld.guards.clear(); coverWorld.noises.clear(); coverWorld.sounds.clear(); coverWorld.events.clear(); coverWorld.securityEntries.clear();
    coverWorld.agents[0].position = {387.f, 400.f}; coverWorld.agents[0].footstepTime = 0.f;
    coverWorld.obstacles.push_back({{{412.f, 200.f}, {24.f, 400.f}}, CoverKind::High, true, "Pared de pasos laterales"});
    ActionCommand enter; enter.coverToggle = true; covered.Step(0.05f, {{1, enter}});
    coverWorld.agents[0].footstepTime = GameplayConfig::FootstepInterval;
    ActionCommand slide; slide.moveY = 1.f; covered.Step(0.05f, {{1, slide}});
    require(coverWorld.agents[0].cover.active && coverWorld.agents[0].facing == sf::Vector2f{-1.f, 0.f} && coverWorld.noises.size() == 1 && coverWorld.noises[0].visualDirection == sf::Vector2f{0.f, 1.f},
      "el paso lateral real no conserva dirección distinta de la normal corporal de cobertura");
    auto coverView = coverWorld; coverView.noises[0].remaining = coverView.noises[0].duration * 0.6f;
    const auto coverCenter = waveCenter(coverView, "clean-hud-cover-slide");
    const auto coverOrigin = coverView.noises[0].position + coverView.noises[0].visualDirection * PresentationConfig::FootstepVisualOffset;
    const auto coverExpected = WorldRenderer::ProjectGround(coverOrigin, coverView.agents[0].position, logical, renderer.GetZoom());
    const auto coverBodyOrigin = WorldRenderer::ProjectGround(coverView.noises[0].position + coverView.agents[0].facing * PresentationConfig::FootstepVisualOffset, coverView.agents[0].position, logical, renderer.GetZoom());
    require((coverCenter - coverExpected).length() <= rasterBound && (coverCenter - coverBodyOrigin).length() > rasterBound,
      "la onda lateral usa la orientación corporal hacia fuera de la pared en lugar del paso emitido");
    for (int tick = 0; tick < 8; ++tick) simulation.Step(0.1f, {});
    require(live.noises.empty(), "la simulación no retira las ondas al vencer su duración");
    const auto expiredLive = capture(renderer, live, output / "clean-hud-running-expired.png", false);
    auto expiredSilent = live; expiredSilent.noises.clear();
    require(identical(expiredLive, capture(renderer, expiredSilent, output / "clean-hud-running-expired-baseline.png", false)), "la carrera deja residuos gráficos tras caducar");
    live.agents[0].stance = Stance::Prone; live.agents[0].transitionTime = 0.f;
    ActionCommand crawling; crawling.moveX = 1.f;
    for (int tick = 0; tick < 12; ++tick) simulation.Step(0.1f, {{1, crawling}});
    require(live.agents[0].moving && live.agents[0].stance == Stance::Prone && live.noises.empty(), "arrastrarse genera ondas de pasos");
    capture(renderer, live, output / "clean-hud-prone-silent.png", false);

    const int interactionCheckStart = checks;
    WorldState query; query.phase = GamePhase::Mission; query.agents.push_back(observer); query.agents[0].position = {400.f, 400.f};
    terminal.position = query.agents[0].position + sf::Vector2f{35.f, 0.f}; query.objects.push_back(terminal);
    require(Interaction::Prompt(query, query.agents[0]) == "E · Recoger", "la interacción disponible no presenta su acción real");
    query.objects[0].completed = true;
    require(Interaction::Prompt(query, query.agents[0]).empty(), "un objetivo recogido conserva una indicación imposible");
    query.objects[0].completed = false; query.agents[0].reloadRemaining = 1.f;
    require(Interaction::Prompt(query, query.agents[0]).empty(), "la recarga no oculta el aviso de interacción bloqueada");
    query.agents[0].reloadRemaining = 0.f; query.objects[0].kind = ObjectKind::Camera; query.objects[0].puzzle = {}; query.objects[0].position = observer.position + sf::Vector2f{35.f, 0.f};
    require(Interaction::Prompt(query, query.agents[0]) == "T · Desactivar", "la cámara no muestra la tecla de herramienta real");
    query.agents[0].loadout.tools = 0;
    require(Interaction::Prompt(query, query.agents[0]).empty(), "una cámara ofrece desactivación sin herramientas");
    query.agents[0].cqcHoldingGuard = 99;
    require(Interaction::Prompt(query, query.agents[0]) == "F · Someter", "el agarre no presenta la acción de sometimiento");
    query.agents[0].cqcHoldingGuard = 0; query.agents[0].loadout.tools = 1;
    query.obstacles.push_back({{{412.f, 380.f}, {6.f, 40.f}}, CoverKind::High, true, "Pared de interacción"});
    require(Interaction::Prompt(query, query.agents[0]).empty(), "el aviso de interacción atraviesa una pared");

    Simulation interactionSimulation;
    const auto interactionFixture = [&]() -> WorldState & {
      interactionSimulation.ResetBase({{1, "LOCAL"}, {2, "ALIADO"}}); interactionSimulation.Deploy(0);
      auto &state = const_cast<WorldState &>(interactionSimulation.GetState());
      state.size = {1000.f, 1000.f}; state.obstacles.clear(); state.objects.clear(); state.guards.clear(); state.noises.clear();
      state.sounds.clear(); state.shots.clear(); state.events.clear(); state.securityEntries.clear();
      state.agents[0].position = {400.f, 400.f}; state.agents[1].position = {100.f, 100.f};
      for (auto &agent : state.agents) agent.privateOrder = PrivateOrder::None;
      return state;
    };
    const auto interact = [&](bool dragging = false) {
      ActionCommand use; use.interact = true; use.dragBody = dragging;
      interactionSimulation.Step(0.05f, {{1, use}});
    };
    auto &interactionWorld = interactionFixture();
    Interactable secondary{201, ObjectKind::Secondary, {435.f, 400.f}, "Archivo ya recogido"}; secondary.completed = true;
    interactionWorld.objects.push_back(secondary); interactionWorld.secondary = 1; interactionWorld.agents[0].privateOrder = PrivateOrder::CopyArchive;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]) == "E · Copiar", "el archivo recogido por otro agente oculta una copia privada todavía válida");
    interact();
    require(interactionWorld.agents[0].privateComplete && interactionWorld.secondary == 1 &&
      std::ranges::any_of(interactionWorld.events, [](const MissionEvent &event) { return event.kind == EventKind::PrivateObjective && event.actor == 1 && event.target == 201; }),
      "la acción anunciada no completa la copia privada o vuelve a contar el archivo secundario");
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "una copia privada completada conserva una indicación imposible");
    interactionWorld.agents[0].privateOrder = PrivateOrder::None; interactionWorld.agents[0].privateComplete = false;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "el archivo completado ofrece a otro agente una acción de copia ajena");

    interactionFixture();
    interactionWorld.objects.push_back({202, ObjectKind::Extraction, {435.f, 400.f}, "Extracción"});
    interactionWorld.objectives = GetContract(interactionWorld.contract).requiredObjectives; interactionWorld.agents[0].privateOrder = PrivateOrder::Traitor;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "la extracción ofrece al traidor una acción prohibida por su propia orden");
    interact();
    require(!interactionWorld.agents[0].extracted, "la simulación permite la extracción que el aviso del traidor debe ocultar");
    interactionWorld.agents[0].privateOrder = PrivateOrder::None;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]) == "E · Extraer", "la restricción del traidor oculta la extracción autorizada de un compañero");
    interact();
    require(interactionWorld.agents[0].extracted, "el aviso de extracción disponible no coincide con la acción autoritativa");

    interactionFixture();
    Interactable parent{203, ObjectKind::Computer, {650.f, 400.f}, "Computador padre"}; parent.puzzle.kind = PuzzleKind::Computer;
    parent.puzzle.leverOrder = {1, 2, 3, 4}; parent.puzzle.cooldown = 3.f;
    Interactable lever{204, ObjectKind::PuzzleNode, {435.f, 400.f}, "Palanca"}; lever.puzzle.kind = PuzzleKind::Computer;
    lever.puzzle.parentDevice = parent.id; lever.puzzle.nodeIndex = 1; lever.solid = SpatialOverride::Disabled; lever.blocksVision = SpatialOverride::Disabled;
    interactionWorld.objects = {parent, lever};
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "una palanca anuncia operación durante el bloqueo temporal de su padre");
    interact();
    require(!interactionWorld.objects[1].open && interactionWorld.objects[0].puzzle.sequenceProgress == 0, "la palanca bloqueada avanza el computador padre");
    interactionWorld.objects[0].puzzle.cooldown = 0.f; interactionWorld.objects[0].puzzle.state = PuzzleState::Solved;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "una palanca anuncia operación de un computador ya resuelto");
    interact();
    require(!interactionWorld.objects[1].open && interactionWorld.objects[0].puzzle.sequenceProgress == 0, "la palanca de un padre resuelto modifica el circuito");
    interactionWorld.objects[0].puzzle.state = PuzzleState::Ready; interactionWorld.objects[0].active = false;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "una palanca anuncia operación de un computador inactivo");
    interact();
    require(!interactionWorld.objects[1].open && interactionWorld.objects[0].puzzle.sequenceProgress == 0, "la palanca de un padre inactivo modifica el circuito");
    interactionWorld.objects[0].active = true;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]) == "E · Operar", "la consulta no recupera la palanca cuando el computador vuelve a estar disponible");
    interact();
    require(interactionWorld.objects[1].open && interactionWorld.objects[0].puzzle.sequenceProgress == 1, "la palanca disponible anunciada no ejecuta su paso real");
    interactionWorld.objects.erase(interactionWorld.objects.begin());
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "una palanca sin computador padre conserva una indicación imposible");

    interactionFixture();
    interactionWorld.objects.push_back({205, ObjectKind::Terminal, {435.f, 400.f}, "Archivo durante reacción"});
    interactionWorld.agents[0].staggerRemaining = CqcConfig::PushStagger;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(), "la reacción a un impacto conserva un aviso de interacción bloqueada");
    interact();
    require(!interactionWorld.objects[0].completed && interactionWorld.agents[0].staggerRemaining > 0.f, "la regresión de reacción no mantiene realmente bloqueada la interacción");
    interactionWorld.agents[0].staggerRemaining = 0.f;
    require(Interaction::Prompt(interactionWorld, interactionWorld.agents[0]) == "E · Recoger", "terminar la reacción no recupera el aviso de interacción");
    interact();
    require(interactionWorld.objects[0].completed, "la interacción vuelve a estar disponible en el aviso pero no en la simulación");

    interactionFixture();
    Guard body; body.id = 206; body.position = {400.f, 420.f}; body.state = GuardState::Incapacitated; body.incapacityRemaining = 60.f;
    interactionWorld.guards.push_back(body); interactionWorld.agents[0].draggingGuard = static_cast<int>(body.id);
    Interactable closer{207, ObjectKind::Terminal, {400.f, 370.f}, "Objeto más cercano"};
    Interactable hideout{208, ObjectKind::Hideout, {450.f, 400.f}, "Contenedor"}; hideout.hideoutKind = HideoutKind::TrashBin;
    interactionWorld.objects = {closer, hideout};
    require(WorldGeometry::DistanceToBounds(interactionWorld.agents[0].position, WorldGeometry::ObjectFootprint(closer)) <
      WorldGeometry::DistanceToBounds(interactionWorld.agents[0].position, WorldGeometry::ObjectFootprint(hideout)), "el fixture de arrastre no incluye un objeto general más cercano que el escondite");
    const auto *dragTarget = Interaction::FindObject(interactionWorld, interactionWorld.agents[0]);
    require(dragTarget && dragTarget->id == hideout.id && Interaction::Prompt(interactionWorld, interactionWorld.agents[0]) == "E · Ocultar cuerpo",
      "un objeto general oculta el contenedor que la acción de arrastre selecciona realmente");
    interactionWorld.objects.pop_back();
    require(!Interaction::FindObject(interactionWorld, interactionWorld.agents[0]) && Interaction::Prompt(interactionWorld, interactionWorld.agents[0]).empty(),
      "el arrastre selecciona un objeto general cuando no hay un escondite disponible");
    interact(true);
    require(!interactionWorld.objects[0].completed && !interactionWorld.guards[0].bodyHidden && interactionWorld.agents[0].draggingGuard == static_cast<int>(body.id),
      "interactuar sin escondite durante el arrastre recoge un objetivo o pierde el cuerpo");
    interactionWorld.objects.push_back(hideout);
    interact(true);
    require(interactionWorld.guards[0].bodyHidden && interactionWorld.guards[0].storedIn == hideout.id && interactionWorld.agents[0].draggingGuard < 0 &&
      interactionWorld.objects[1].occupantGuard == body.id && !interactionWorld.objects[0].completed,
      "la selección contextual del arrastre opera el objeto cercano en vez de almacenar el cuerpo en el escondite anunciado");

    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"names", names}, {"wallPixelsRevealed", 0}, {"floatingObjectNamePixels", 0}, {"noiseFrames", noiseFrames},
      {"running", {{"radius", runningEvent.radius}, {"physicalOrigin", {runningEvent.position.x, runningEvent.position.y}}, {"visualOrigin", {visualOrigin.x, visualOrigin.y}},
        {"capturedCenter", {actualCenter.x, actualCenter.y}}, {"expectedCenter", {expectedCenter.x, expectedCenter.y}}, {"rasterBoundLogical", rasterBound},
        {"eventOriginUnchanged", true}, {"stableAfterTurnAndStop", stableWaveFrames}, {"sample", "Evento real de carrera con edad congelada para medir la geometría"}}},
      {"coverWave", {{"capturedCenter", {coverCenter.x, coverCenter.y}}, {"expectedCenter", {coverExpected.x, coverExpected.y}}, {"eventDirection", {coverView.noises[0].visualDirection.x, coverView.noises[0].visualDirection.y}}, {"bodyFacing", {coverView.agents[0].facing.x, coverView.agents[0].facing.y}}}},
      {"proneFootstepEvents", live.noises.size()}, {"expiredNoisePixelExact", true}, {"interactionQueries", checks - interactionCheckStart},
      {"nativeSize", {window.getSize().x, window.getSize().y}}, {"logicalSize", {logical.x, logical.y}}, {"scope", "Render real y consultas de simulación; no sustituye teclado manual ni LAN física"}};
    std::ofstream(output / "clean-hud-fixtures.json") << report.dump(2);
    return report;
  }
}
