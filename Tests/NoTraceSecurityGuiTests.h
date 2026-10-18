#pragma once

#include "Core/EngineContext.h"
#include "Config/NoTraceSecurityConfig.h"
#include "NoTraceSimulation.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceWorldRenderer.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunSecurityRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    int checks = 0;
    const auto require = [&](bool condition, const std::string &message) {
      ++checks;
      if (!condition) throw std::runtime_error("Investigación y alertas visibles: " + message);
    };
    Simulation simulation;
    simulation.ResetBase({{1, "Observador"}}); simulation.Deploy(0);
    auto &world = const_cast<WorldState &>(simulation.GetState());
    world.obstacles.clear(); world.objects.clear(); world.guards.clear(); world.noises.clear(); world.events.clear(); world.sounds.clear(); world.securityEntries.clear();
    world.guards.reserve(4);
    const sf::Vector2f camera{210.f, 300.f}, initialGuard{280.f, 300.f}, bodyPosition{350.f, 300.f};
    auto &observer = world.agents[0]; observer.position = camera; observer.facing = {1.f, 0.f};
    Interactable closet; closet.id = 77; closet.kind = ObjectKind::Hideout; closet.hideoutKind = HideoutKind::Closet;
    closet.position = {170.f, 300.f}; closet.facing = {1.f, 0.f}; closet.name = "Armario del observador"; world.objects.push_back(closet);
    Guard investigator; investigator.id = 1; investigator.position = initialGuard; investigator.facing = {1.f, 0.f};
    investigator.patrol = {initialGuard, {380.f, 300.f}}; world.guards.push_back(investigator);
    Guard body; body.id = 91; body.position = bodyPosition; body.health = 0.f; body.state = GuardState::Dead; world.guards.push_back(body);
    require(observer.alive && !observer.hidden && (investigator.position - observer.position).length() > GameplayConfig::CloseVisionDistance &&
      !simulation.CanSee(investigator.position, investigator.facing, observer), "el observador inicial debe estar vivo detrás del guardia y fuera de su detección automática");
    WorldRenderer renderer(context); renderer.ResetZoom();
    nlohmann::json phases = nlohmann::json::array();
    const auto snapshot = [&](const char *name) {
      auto view = world;
      const bool exteriorView = observer.hideoutId == closet.id;
      if (exteriorView) {
        // La cámara de evidencia conserva la posición exterior; la simulación mantiene al observador dentro del armario cerrado.
        view.agents[0].position = camera; view.agents[0].hideoutId = 0; view.agents[0].hideoutExit = {};
      }
      const auto &guard = view.guards[0];
      require(Visibility::GuardVisible(view, view.agents[0], guard), std::string("el guardia debe estar visible en la captura ") + name);
      const std::string file = std::string("security-") + name + ".png";
      const auto pixels = capture(renderer, view, output / file, true);
      auto absent = view; absent.guards.erase(absent.guards.begin());
      const auto background = capture(renderer, absent, output / (std::string("security-") + name + "-without-guard.png"), true);
      const auto logical = context.window.GetLogicalSize();
      const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(logical);
      const auto feet = WorldRenderer::ProjectGround(guard.position, camera, logical, renderer.GetZoom());
      const auto minimum = (feet - sf::Vector2f{60.f, 115.f} * renderer.GetZoom()).componentWiseMul(scale);
      const auto maximum = (feet + sf::Vector2f{60.f, 12.f} * renderer.GetZoom()).componentWiseMul(scale);
      const int left = std::max(0, static_cast<int>(std::floor(minimum.x))), top = std::max(0, static_cast<int>(std::floor(minimum.y)));
      const int right = std::min(static_cast<int>(pixels.getSize().x), static_cast<int>(std::ceil(maximum.x)));
      const int bottom = std::min(static_cast<int>(pixels.getSize().y), static_cast<int>(std::ceil(maximum.y)));
      int changed = 0;
      for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        const sf::Vector2u point{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
        if (pixels.getPixel(point) != background.getPixel(point)) ++changed;
      }
      require(changed > 100, std::string("el PNG no contiene píxeles reconocibles del guardia en ") + name);
      const auto frame = WorldRenderer::SelectGuardSprite(guard, view);
      const auto indicator = WorldRenderer::IndicatorFor(guard);
      if (guard.state == GuardState::InspectBody) {
        const auto &animation = SpriteConfig::Animations[static_cast<std::size_t>(SpriteConfig::Clip::Interact)];
        require(frame.operative && frame.index / SpriteConfig::FamilyFrames == animation.family && frame.index % SpriteConfig::Columns >= animation.first &&
          frame.index % SpriteConfig::Columns < animation.first + animation.count, "la inspección detenida debe usar la animación de interacción");
      }
      require(guard.state == GuardState::Patrol ? indicator == WorldRenderer::GuardIndicator::None : indicator == WorldRenderer::GuardIndicator::Searching,
        std::string("el indicador no distingue la investigación o vigilancia en ") + name);
      phases.push_back({{"phase", name}, {"file", file}, {"elapsed", world.elapsed}, {"tick", world.tick}, {"alert", static_cast<int>(world.securityAlert)},
        {"alertRemaining", world.securityRemaining}, {"guardState", static_cast<int>(guard.state)}, {"guardPosition", {guard.position.x, guard.position.y}},
        {"guardFacing", {guard.facing.x, guard.facing.y}}, {"bodyPosition", {bodyPosition.x, bodyPosition.y}}, {"inspectionTime", guard.bodyInspectionTime},
        {"spriteFrame", frame.index}, {"indicator", static_cast<int>(indicator)}, {"changedGuardPixels", changed},
        {"observerPosition", {observer.position.x, observer.position.y}}, {"observerHideout", observer.hideoutId}, {"observerAlive", observer.alive},
        {"cameraPosition", {camera.x, camera.y}}, {"exteriorFixtureView", exteriorView}});
    };
    float maximumMovement = 0.f, inspectionStart = -1.f, orangeAt = -1.f, yellowAt = -1.f, calmAt = -1.f;
    sf::Vector2f inspectionPosition;
    bool physical = true, inspectionStationary = true, approachCaptured = false, inspectionCaptured = false;
    bool orangeCaptured = false, yellowCaptured = false, enteredCloset = false, unexpectedDetection = false;
    bool assistingCaptured = false, awakeCaptured = false, colleagueContinued = false;
    float assistanceRemaining = 0.f, assistanceAt = 0.f;
    sf::Vector2f assistedPosition;
    for (unsigned int step = 0; step < 450; ++step) {
      ActionCommand command;
      if (orangeCaptured && !enteredCloset) command.interact = true;
      const auto previous = world.guards[0].position;
      simulation.Step(0.1f, {{1, command}});
      const auto &guard = world.guards[0];
      const float distance = (guard.position - previous).length();
      maximumMovement = std::max(maximumMovement, distance);
      physical &= distance <= GameplayConfig::GuardSpeed * 0.1f + 0.01f && !WorldGeometry::PositionBlocked(world, guard.position, GameplayConfig::AgentRadius);
      unexpectedDetection |= world.securityAlert == SecurityAlert::Red || observer.detectionConfirmed;
      if (guard.state == GuardState::ApproachBody && !approachCaptured) { snapshot("approach"); approachCaptured = true; }
      if (guard.state == GuardState::InspectBody && guard.bodyTargetId == 91) {
        if (inspectionStart < 0.f) { inspectionStart = world.elapsed - guard.bodyInspectionTime; inspectionPosition = guard.position; }
        inspectionStationary &= guard.position == inspectionPosition && guard.stepTime == 0.f;
        if (guard.bodyInspectionTime >= 0.6f && !inspectionCaptured) { snapshot("inspect"); inspectionCaptured = true; }
      }
      if (world.securityAlert == SecurityAlert::Orange && !orangeCaptured) {
        orangeAt = world.elapsed;
        require(std::abs(world.securityRemaining - SecurityConfig::SearchDuration) < 0.001f, "el hallazgo debe iniciar veinte segundos completos de naranja");
        snapshot("orange"); orangeCaptured = true;
        Guard unconscious; unconscious.id = 92; unconscious.position = bodyPosition + sf::Vector2f{0.f, 45.f}; unconscious.health = 37.f;
        unconscious.state = GuardState::Incapacitated; unconscious.incapacityRemaining = GameplayConfig::GuardIncapacityDuration; unconscious.bodyReported = true;
        unconscious.patrol = {unconscious.position}; assistedPosition = unconscious.position; world.guards.push_back(unconscious);
        Guard colleague; colleague.id = 93; colleague.position = {420.f, 430.f}; colleague.facing = {0.f, 1.f};
        colleague.patrol = {colleague.position}; colleague.state = GuardState::Search; colleague.lastKnown = bodyPosition; world.guards.push_back(colleague);
      }
      if (guard.state == GuardState::InspectBody && guard.bodyTargetId == 92 && guard.bodyInspectionTime >= 0.6f && !assistingCaptured) {
        assistanceRemaining = world.securityRemaining; assistanceAt = world.elapsed;
        require(world.securityAlert == SecurityAlert::Orange && world.guards[3].state == GuardState::Search,
          "la asistencia cancela la naranja o detiene la búsqueda del otro guardia");
        snapshot("assisting"); assistingCaptured = true;
      }
      if (assistingCaptured && !awakeCaptured && world.guards[2].state != GuardState::Incapacitated) {
        const auto &awakened = world.guards[2];
        require((awakened.position - assistedPosition).length() <= GameplayConfig::GuardSpeed * 0.1f + 0.01f && awakened.health == 37.f && awakened.state == GuardState::Search &&
          awakened.incapacityRemaining == 0.f && awakened.stun == 0.f && awakened.cqcState == CqcState::None,
          "el compañero asistido no recupera un estado de IA coherente en su posición original");
        require(world.securityAlert == SecurityAlert::Orange && std::abs(world.securityRemaining - (assistanceRemaining - (world.elapsed - assistanceAt))) < 0.003f,
          "despertar al compañero reinicia o cancela el reloj de búsqueda naranja");
        colleagueContinued = world.guards[3].state == GuardState::Search;
        snapshot("awakened"); awakeCaptured = true;
      }
      if (command.interact) {
        enteredCloset = observer.hidden && observer.hideoutId == closet.id && world.objects[0].occupantAgent == observer.id && !world.objects[0].open;
        require(enteredCloset, "el observador debe entrar mediante interacción real en el armario cerrado antes de la búsqueda");
      }
      if (world.securityAlert == SecurityAlert::Yellow && !yellowCaptured) {
        yellowAt = world.elapsed;
        require(guard.state == GuardState::Vigilance && std::abs(world.securityRemaining - SecurityConfig::VigilanceDuration) < 0.001f,
          "la amarilla debe comenzar con vigilancia real y doce segundos completos");
        snapshot("yellow"); yellowCaptured = true;
      }
      if (yellowCaptured && world.securityAlert == SecurityAlert::Calm) { calmAt = world.elapsed; snapshot("calm"); break; }
    }
    require(approachCaptured && inspectionCaptured && orangeCaptured && yellowCaptured && calmAt > 0.f, "faltan transiciones o capturas de alguna fase real");
    require(assistingCaptured && awakeCaptured && colleagueContinued, "faltan la asistencia visible, el compañero despierto o la búsqueda de los demás");
    require(physical && maximumMovement > 0.f && (inspectionPosition - initialGuard).length() > 20.f, "la aproximación o búsqueda atraviesa sólidos, salta o no avanza físicamente");
    require(inspectionStationary && inspectionStart >= 0.f && std::abs(orangeAt - inspectionStart - SecurityConfig::BodyInspectionDuration) < 0.002f,
      "el investigador debe permanecer detenido durante los 1,5 segundos de inspección");
    require(std::abs(yellowAt - orangeAt - SecurityConfig::SearchDuration) < 0.003f && std::abs(calmAt - yellowAt - SecurityConfig::VigilanceDuration) < 0.003f,
      "los tiempos reales de búsqueda y vigilancia no coinciden con veinte y doce segundos");
    require(enteredCloset && observer.alive && !observer.incapacitated && !unexpectedDetection && world.guards[0].state == GuardState::Patrol &&
      std::ranges::count_if(world.events, [](const MissionEvent &event) { return event.kind == EventKind::BodyFound; }) == 1,
      "la ocultación debe impedir contacto real y el mismo cuerpo no debe reiniciar la alerta ni bloquear el regreso a patrulla");
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"cases", phases}, {"inspectionStartedAt", inspectionStart}, {"orangeStartedAt", orangeAt},
      {"yellowStartedAt", yellowAt}, {"patrolResumedAt", calmAt}, {"inspectionDuration", orangeAt - inspectionStart}, {"searchDuration", yellowAt - orangeAt},
      {"vigilanceDuration", calmAt - yellowAt}, {"maximumMovementPerStep", maximumMovement}, {"stepSeconds", 0.1f},
      {"assistanceDuringOrange", "PASS"}, {"awakeWithoutTeleport", "PASS"}, {"otherGuardContinuedSearch", colleagueContinued}, {"orangeClockPreserved", "PASS"},
      {"cameraNote", "Después de entrar por interacción real en el armario cerrado, sólo la copia de presentación conserva la cámara exterior; posiciones y fases del guardia proceden de la simulación sin modificaciones."}};
    std::ofstream file(output / "security-fixtures.json"); file << report.dump(2);
    return report;
  }
}
