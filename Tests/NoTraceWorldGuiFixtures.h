#pragma once

#include "NoTraceWorldRenderer.h"
#include "NoTraceDistractionGeometry.h"
#include "NoTraceSimulation.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/Image.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunWorldRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    struct BitmapFrame { sf::IntRect rectangle; sf::Vector2f muzzle; };
    const auto readBitmapFrames = [](const char *name) {
      const auto directory = std::filesystem::path("Content/Textures/NoTrace/Characters");
      std::ifstream rectangles(directory / (std::string(name) + ".frames")), muzzles(directory / (std::string(name) + ".muzzles"));
      std::vector<BitmapFrame> frames;
      BitmapFrame frame;
      while (rectangles >> frame.rectangle.position.x >> frame.rectangle.position.y >> frame.rectangle.size.x >> frame.rectangle.size.y) {
        if (!(muzzles >> frame.muzzle.x >> frame.muzzle.y)) throw std::runtime_error("Falta una boca revisada del PNG en los fixtures");
        frames.push_back(frame);
      }
      return frames;
    };
    const auto bitmapFrames = readBitmapFrames("SoldiersAtlas"), proneBitmapFrames = readBitmapFrames("ProneAtlas");
    const auto bitmapMuzzle = [&](WorldRenderer::SpriteFrame selected, sf::Vector2f position, sf::Vector2f facing, Stance stance, sf::Vector2f camera, float zoom) {
      const auto &frame = (selected.prone ? proneBitmapFrames : bitmapFrames).at(selected.index);
      if (frame.muzzle.x < 0.f) throw std::runtime_error("La pose disparando no tiene una boca real visible en el PNG");
      const auto bounds = WorldRenderer::SpriteBounds(facing, stance, false);
      const sf::Vector2f sourcePoint{selected.mirror ? frame.rectangle.size.x - frame.muzzle.x : frame.muzzle.x, frame.muzzle.y};
      const auto local = bounds.position + sourcePoint.componentWiseDiv(sf::Vector2f(frame.rectangle.size)).componentWiseMul(bounds.size);
      return WorldRenderer::ProjectGround(position, camera, context.window.GetLogicalSize(), zoom) + local * zoom;
    };
    const auto requireBitmapFlash = [&](const sf::Image &body, const sf::Image &absent, const sf::Image &firing, sf::Vector2f muzzle, const std::string &scenario) {
      const auto center = muzzle.componentWiseDiv(context.window.GetLogicalSize()).componentWiseMul(sf::Vector2f(body.getSize()));
      bool barrel = false, flash = false;
      for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx) {
        const int x = static_cast<int>(std::floor(center.x)) + dx, y = static_cast<int>(std::floor(center.y)) + dy;
        if (x < 0 || y < 0 || x >= static_cast<int>(body.getSize().x) || y >= static_cast<int>(body.getSize().y)) continue;
        const sf::Vector2u point{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
        barrel = barrel || body.getPixel(point) != absent.getPixel(point);
        flash = flash || firing.getPixel(point) == PresentationConfig::Light;
      }
      if (!barrel || !flash) throw std::runtime_error("La boca del PNG y el destello capturado se separan: " + scenario);
    };
    WorldState world;
    world.phase = GamePhase::Base;
    world.size = {700.f, 700.f};
    world.elapsed = 1.f;
    world.obstacles = {
        {{{300.f, 300.f}, {100.f, 100.f}}, CoverKind::High, true, "Caja principal"},
        {{{455.f, 380.f}, {85.f, 50.f}}, CoverKind::Low, true, "Caja secundaria"}};
    Interactable terminal;
    terminal.kind = ObjectKind::Terminal;
    terminal.position = {275.f, 460.f};
    terminal.name = "TERMINAL";
    world.objects.push_back(terminal);
    Agent agent;
    agent.id = 1;
    agent.name = "AGENTE LOCAL";
    agent.facing = {1.f, 0.f};
    world.agents.push_back(agent);
    Guard back;
    back.id = 1;
    back.position = {275.f, 350.f};
    Guard front;
    front.id = 2;
    front.position = {425.f, 350.f};
    world.guards = {back, front};
    WorldRenderer renderer(context);
    const std::array<const char *, 5> names{{"behind", "front", "lateral-x", "lateral-y", "diagonal"}};
    const std::array<sf::Vector2f, 5> positions{{{350.f, 270.f}, {350.f, 430.f}, {430.f, 390.f}, {390.f, 430.f}, {430.f, 430.f}}};
    const std::array<const char *, 3> zoomNames{{"min", "normal", "max"}};
    nlohmann::json metadata = nlohmann::json::array();
    for (std::size_t scenario = 0; scenario < positions.size(); ++scenario) {
      world.agents[0].position = positions[scenario];
      const std::vector<sf::FloatRect> footprints{world.obstacles[0].bounds, world.obstacles[1].bounds,
          WorldRenderer::ObjectFootprint(terminal), {back.position, {0.f, 0.f}}, {front.position, {0.f, 0.f}}, {positions[scenario], {0.f, 0.f}}};
      const auto order = WorldRenderer::GroundDrawOrder(footprints);
      const auto rank = [&order](std::size_t index) { return std::find(order.begin(), order.end(), index) - order.begin(); };
      if (!(rank(3) < rank(0) && rank(0) < rank(4))) throw std::runtime_error("Los guardias no rodean la caja con profundidad coherente");
      if ((scenario == 0 && rank(5) > rank(0)) || (scenario != 0 && rank(5) < rank(0))) throw std::runtime_error("El agente no cambia de lado visual al rodear la caja");
      for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
        renderer.ResetZoom();
        if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
        const auto file = output / ("world-" + std::string(names[scenario]) + "-" + zoomNames[zoomIndex] + ".png");
        const auto pixels = capture(renderer, world, file);
        const auto logicalSize = context.window.GetLogicalSize();
        const auto focus = WorldRenderer::ProjectGround(positions[scenario], positions[scenario], logicalSize, renderer.GetZoom());
        const auto guardTorso = WorldRenderer::ProjectGround(front.position, positions[scenario], logicalSize, renderer.GetZoom()) - sf::Vector2f{0.f, PresentationConfig::ShotHeight * renderer.GetZoom()};
        const auto guardAim = renderer.AimAt(guardTorso, world, 1);
        const auto guardHit = CombatGeometry::IntersectActor(positions[scenario], CombatGeometry::Unit(guardAim.ground - positions[scenario]),
            WorldRenderer::WeaponHeight(world.agents[0].stance), guardAim.slope, GameplayConfig::WeaponRange, front.position, front.facing, Stance::Standing);
        if (!guardHit) throw std::runtime_error("El clic en el torso capturado no atraviesa un volumen válido del guardia");
        const auto torso = focus + sf::Vector2f{4.f, -22.f} * renderer.GetZoom();
        const auto pixel = torso.componentWiseDiv(logicalSize).componentWiseMul(sf::Vector2f(pixels.getSize()));
        const sf::Vector2u sample{static_cast<unsigned int>(std::floor(pixel.x)), static_cast<unsigned int>(std::floor(pixel.y))};
        const auto actual = pixels.getPixel(sample);
        if ((scenario == 0 && actual != PresentationConfig::WallTop) || (scenario != 0 && actual == PresentationConfig::WallTop)) throw std::runtime_error("La captura no muestra la oclusión esperada del torso: " + file.string());
        const auto head = (focus + sf::Vector2f{4.f, -38.f} * renderer.GetZoom()).componentWiseDiv(logicalSize).componentWiseMul(sf::Vector2f(pixels.getSize()));
        const sf::Vector2u headSample{static_cast<unsigned int>(std::floor(head.x)), static_cast<unsigned int>(std::floor(head.y))};
        if (pixels.getPixel(headSample) == PresentationConfig::WallTop) throw std::runtime_error("La cabeza no permanece parcialmente visible al rodear la caja: " + file.string());
        metadata.push_back({{"file", file.filename().string()}, {"zoom", renderer.GetZoom()},
            {"agent", {positions[scenario].x, positions[scenario].y}}, {"drawOrder", order},
            {"torsoSample", {sample.x, sample.y}}, {"torsoColor", {actual.r, actual.g, actual.b}},
            {"torsoOccluded", scenario == 0}, {"headSample", {headSample.x, headSample.y}}, {"headVisible", true},
            {"guardTorsoCursor", {guardTorso.x, guardTorso.y}}, {"guardAimGround", {guardAim.ground.x, guardAim.ground.y}}, {"aimSlope", guardAim.slope}});
      }
    }
    const std::array<const char *, 4> shotNames{{"standing", "crouched", "prone", "prone-blocked"}};
    const std::array<Stance, 4> shotStances{{Stance::Standing, Stance::Crouched, Stance::Prone, Stance::Prone}};
    renderer.ResetZoom();
    for (std::size_t index = 0; index < shotNames.size(); ++index) {
      auto &shooter = world.agents[0];
      shooter.position = {350.f, 470.f};
      shooter.stance = shotStances[index];
      const bool blocked = index == 3;
      world.guards[1].position = blocked ? sf::Vector2f{350.f, 350.f} : sf::Vector2f{475.f, 450.f};
      world.guards[1].state = blocked ? GuardState::Patrol : GuardState::Incapacitated;
      shooter.facing = world.guards[1].position - shooter.position;
      shooter.facing /= shooter.facing.length();
      const auto impact = blocked ? sf::Vector2f{350.f, 400.f} : world.guards[1].position - shooter.facing * GameplayConfig::AgentRadius;
      world.shots = {{shooter.position, impact, GameplayConfig::ShotTraceDuration, false, shooter.id, 0, !blocked, blocked}};
      world.shots[0].fromHeight = WorldRenderer::WeaponHeight(shooter.stance);
      world.shots[0].toHeight = blocked ? world.shots[0].fromHeight : CombatGeometry::WeaponHeight(Stance::Prone);
      const auto stem = "world-shot-" + std::string(shotNames[index]);
      const auto file = output / (stem + ".png");
      const auto shot = world.shots;
      world.shots.clear();
      const auto body = capture(renderer, world, output / (stem + "-weapon.png"));
      shooter.hidden = true;
      const auto absent = capture(renderer, world, output / (stem + "-absent.png"));
      shooter.hidden = false; world.shots = shot;
      const auto pixels = capture(renderer, world, file);
      const auto logicalSize = context.window.GetLogicalSize();
      const auto selected = WorldRenderer::SelectSprite(shooter.facing, shooter.stance, shooter.animationTime, shooter.moving, shooter.running, false, false);
      const auto muzzle = bitmapMuzzle(selected, shooter.position, shooter.facing, shooter.stance, shooter.position, renderer.GetZoom());
      const auto pixel = muzzle.componentWiseDiv(logicalSize).componentWiseMul(sf::Vector2f(pixels.getSize()));
      const sf::Vector2u sample{static_cast<unsigned int>(std::floor(pixel.x)), static_cast<unsigned int>(std::floor(pixel.y))};
      requireBitmapFlash(body, absent, pixels, muzzle, stem);
      metadata.push_back({{"file", file.filename().string()}, {"stance", shotNames[index]},
          {"sourceHeight", WorldRenderer::WeaponHeight(shooter.stance)}, {"impactHeight", WorldRenderer::ShotImpactHeight(world.shots[0], world)},
          {"muzzleSample", {sample.x, sample.y}}, {"atlasFrame", selected.index}, {"muzzleFromBitmap", true}, {"blocked", blocked}});
    }
    WorldState fogWorld;
    fogWorld.phase = GamePhase::Mission; fogWorld.size = {700.f, 600.f};
    Agent observer;
    observer.id = 1; observer.name = "VISIBILIDAD"; observer.position = {200.f, 300.f}; observer.facing = {1.f, 0.f};
    fogWorld.agents.push_back(observer);
    fogWorld.obstacles = {{{{290.f, 100.f}, {20.f, 400.f}}, CoverKind::High, true, "Muro opaco"}};
    Guard hidden;
    hidden.id = 1; hidden.position = {450.f, 300.f}; hidden.state = GuardState::Alert; hidden.detectedTime = 0.5f;
    fogWorld.guards.push_back(hidden);
    terminal.position = {460.f, 320.f}; fogWorld.objects.push_back(terminal);
    fogWorld.noises.push_back({hidden.position, 100.f, GameplayConfig::NoiseDuration, 0, hidden.id});
    fogWorld.shots = {{hidden.position, hidden.position + sf::Vector2f{20.f, 0.f}, GameplayConfig::ShotTraceDuration, true, 0, hidden.id}};
    const auto hiddenPixels = capture(renderer, fogWorld, output / "world-fog-hidden.png");
    const auto hiddenPoint = WorldRenderer::ProjectGround(hidden.position, observer.position, context.window.GetLogicalSize(), renderer.GetZoom());
    const auto hiddenPixel = hiddenPoint.componentWiseDiv(context.window.GetLogicalSize()).componentWiseMul(sf::Vector2f(hiddenPixels.getSize()));
    const sf::Vector2u fogSample{static_cast<unsigned int>(hiddenPixel.x), static_cast<unsigned int>(hiddenPixel.y)};
    if (hiddenPixels.getPixel(fogSample) != PresentationConfig::Fog) throw std::runtime_error("La zona detrás del muro no queda cubierta por niebla opaca");
    fogWorld.guards.clear(); fogWorld.objects.clear(); fogWorld.noises.clear(); fogWorld.shots.clear();
    const auto redactedPixels = capture(renderer, fogWorld, output / "world-fog-redacted.png");
    const auto bytes = static_cast<std::size_t>(hiddenPixels.getSize().x) * hiddenPixels.getSize().y * 4;
    if (hiddenPixels.getSize() != redactedPixels.getSize() || !std::equal(hiddenPixels.getPixelsPtr(), hiddenPixels.getPixelsPtr() + bytes, redactedPixels.getPixelsPtr())) {
      throw std::runtime_error("Un enemigo, objeto, icono, ruido, trazo o marcador oculto modifica la imagen y revela información");
    }
    metadata.push_back({{"file", "world-fog-hidden.png"}, {"identicalToRedacted", true}, {"opaqueFogSample", {fogSample.x, fogSample.y}}});
    fogWorld.obstacles = {{{{290.f, 100.f}, {20.f, 168.f}}, CoverKind::High, true, "Muro norte"},
        {{{290.f, 332.f}, {20.f, 168.f}}, CoverKind::High, true, "Muro sur"}};
    Interactable door;
    door.kind = ObjectKind::Door; door.position = {300.f, 300.f};
    fogWorld.objects.push_back(door); fogWorld.guards.push_back(hidden);
    if (Visibility::GuardVisible(fogWorld, observer, hidden)) throw std::runtime_error("La puerta cerrada permite ver al enemigo");
    capture(renderer, fogWorld, output / "world-fog-door-closed.png");
    fogWorld.objects[0].open = true;
    if (!Visibility::GuardVisible(fogWorld, observer, hidden)) throw std::runtime_error("Abrir la puerta no actualiza la visión del corredor");
    capture(renderer, fogWorld, output / "world-fog-door-open.png");
    metadata.push_back({{"file", "world-fog-door-open.png"}, {"visibleAfterOpeningDoor", true}});
    world.shots.clear(); world.obstacles.clear(); world.objects.clear(); world.guards.clear();
    world.agents[0].position = {350.f, 350.f}; world.agents[0].stance = Stance::Standing;
    for (std::size_t index = 0; index < 4; ++index) {
      Guard icon;
      icon.id = static_cast<std::uint32_t>(index + 1); icon.position = {260.f + index * 70.f, 250.f};
      icon.state = index == 0 ? GuardState::Investigate : index == 3 ? GuardState::Incapacitated : GuardState::Alert;
      icon.detectedTime = index == 1 ? 0.5f : 0.f;
      world.guards.push_back(icon);
    }
    capture(renderer, world, output / "world-ai-indicators.png");
    metadata.push_back({{"file", "world-ai-indicators.png"}, {"indicators", {"suspicion", "confirmed", "combat", "unconscious"}}});
    world.guards.clear();
    world.obstacles = {{{{300.f, 250.f}, {28.f, 180.f}}, CoverKind::High, true, "Cobertura"}};
    auto &coverAgent = world.agents[0];
    coverAgent.position = {340.f, 300.f}; coverAgent.cover.active = true; coverAgent.cover.anchor = coverAgent.position;
    coverAgent.cover.normal = {1.f, 0.f}; coverAgent.facing = {-1.f, 0.f};
    capture(renderer, world, output / "world-cover-braced.png");
    coverAgent.position = {340.f, 230.f}; coverAgent.cover.peekSide = PeekSide::Left;
    capture(renderer, world, output / "world-cover-peek.png");
    metadata.push_back({{"file", "world-cover-peek.png"}, {"physicalPeekPosition", {coverAgent.position.x, coverAgent.position.y}}});
    const auto identical = [](const sf::Image &first, const sf::Image &second) {
      const auto count = static_cast<std::size_t>(first.getSize().x) * first.getSize().y * 4;
      return first.getSize() == second.getSize() && std::equal(first.getPixelsPtr(), first.getPixelsPtr() + count, second.getPixelsPtr());
    };
    WorldState sprites;
    sprites.phase = GamePhase::Base; sprites.size = {900.f, 900.f};
    observer.position = {450.f, 450.f}; observer.hidden = true;
    sprites.agents.push_back(observer);
    const std::array<const char *, 3> stanceNames{{"standing", "crouched", "prone"}};
    for (std::size_t posture = 0; posture < stanceNames.size(); ++posture) for (int frame = 0; frame < (posture == 0 ? 4 : 2); ++frame) {
      sprites.agents.resize(1);
      for (int direction = 0; direction < 8; ++direction) {
        Agent actor;
        actor.id = direction + 2; actor.name = "DIRECCIÓN " + std::to_string(direction);
        actor.position = {330.f + (direction % 4) * 80.f, 380.f + (direction / 4) * 140.f};
        const float angle = direction * 0.78539816339f;
        actor.facing = CombatGeometry::Unit({std::sin(angle) / PresentationConfig::GroundProjection.x - std::cos(angle) / PresentationConfig::GroundProjection.y,
            -std::sin(angle) / PresentationConfig::GroundProjection.x - std::cos(angle) / PresentationConfig::GroundProjection.y});
        actor.stance = static_cast<Stance>(posture); actor.moving = true; actor.animationTime = frame * (posture == 2 ? 0.3f : posture == 1 ? 0.2f : 0.14f);
        sprites.agents.push_back(actor);
      }
      const auto file = "world-sprites-" + std::string(stanceNames[posture]) + "-" + std::to_string(frame) + ".png";
      const auto body = capture(renderer, sprites, output / file);
      const auto actors = sprites.agents;
      sprites.agents.resize(1);
      const auto absent = capture(renderer, sprites, output / ("world-sprites-absent-" + std::string(stanceNames[posture]) + "-" + std::to_string(frame) + ".png"));
      sprites.agents = actors;
      for (std::size_t actorIndex = 1; actorIndex < sprites.agents.size(); ++actorIndex) {
        const auto &actor = sprites.agents[actorIndex];
        ShotTrace shot;
        shot.actor = actor.id; shot.from = actor.position; shot.to = actor.position + actor.facing * 70.f; shot.remaining = GameplayConfig::ShotTraceDuration;
        shot.fromHeight = WorldRenderer::WeaponHeight(actor.stance); shot.toHeight = shot.fromHeight;
        sprites.shots.push_back(shot);
      }
      const auto firing = capture(renderer, sprites, output / ("world-sprites-flash-" + std::string(stanceNames[posture]) + "-" + std::to_string(frame) + ".png"));
      for (std::size_t actorIndex = 1; actorIndex < sprites.agents.size(); ++actorIndex) {
        const auto &actor = sprites.agents[actorIndex];
        const auto selected = WorldRenderer::SelectSprite(actor.facing, actor.stance, actor.animationTime, true, false, false, false);
        const auto muzzle = bitmapMuzzle(selected, actor.position, actor.facing, actor.stance, sprites.agents[0].position, renderer.GetZoom());
        requireBitmapFlash(body, absent, firing, muzzle, file + " dirección=" + std::to_string(actorIndex - 1));
      }
      sprites.shots.clear();
      metadata.push_back({{"file", file}, {"directions", 8}, {"designedAnimationFrame", frame}});
    }
    sprites.agents.resize(1);
    for (int movingFrame = -1; movingFrame < 4; ++movingFrame) {
      sprites.elapsed = std::max(0, movingFrame) * 0.14f;
      for (int direction = 0; direction < 8; ++direction) {
        Guard actor;
        actor.id = direction + 1; actor.position = {330.f + (direction % 4) * 80.f, 380.f + (direction / 4) * 140.f};
        const float angle = direction * 0.78539816339f;
        actor.facing = CombatGeometry::Unit({std::sin(angle) / PresentationConfig::GroundProjection.x - std::cos(angle) / PresentationConfig::GroundProjection.y,
            -std::sin(angle) / PresentationConfig::GroundProjection.x - std::cos(angle) / PresentationConfig::GroundProjection.y});
        actor.lastKnown = actor.position + actor.facing * (movingFrame < 0 ? 0.f : 100.f);
        sprites.guards.push_back(actor);
      }
      const auto stem = "world-guard-sockets-" + std::to_string(movingFrame + 1);
      const auto body = capture(renderer, sprites, output / (stem + "-weapon.png"));
      const auto guards = sprites.guards; sprites.guards.clear();
      const auto absent = capture(renderer, sprites, output / (stem + "-absent.png"));
      sprites.guards = guards;
      for (const auto &actor : sprites.guards) {
        ShotTrace shot;
        shot.guardId = actor.id; shot.from = actor.position; shot.to = actor.position + actor.facing * 70.f;
        shot.remaining = GameplayConfig::ShotTraceDuration; shot.fromHeight = PresentationConfig::ShotHeight; shot.toHeight = shot.fromHeight;
        sprites.shots.push_back(shot);
      }
      const auto firing = capture(renderer, sprites, output / (stem + "-flash.png"));
      for (const auto &actor : sprites.guards) {
        const auto selected = WorldRenderer::SelectSprite(actor.facing, Stance::Standing, sprites.elapsed + actor.id * 0.25f, movingFrame >= 0, false, false, false, false, false, true);
        const auto muzzle = bitmapMuzzle(selected, actor.position, actor.facing, Stance::Standing, sprites.agents[0].position, renderer.GetZoom());
        requireBitmapFlash(body, absent, firing, muzzle, stem + " dirección=" + std::to_string(actor.id - 1));
      }
      sprites.shots.clear(); sprites.guards.clear();
      metadata.push_back({{"file", stem + "-flash.png"}, {"guardDirections", 8}, {"locomotionFrame", movingFrame}});
    }
    Agent hiddenShooter;
    hiddenShooter.id = 2; hiddenShooter.hidden = true; hiddenShooter.position = {520.f, 400.f};
    sprites.agents.push_back(hiddenShooter);
    const auto hiddenSourceBaseline = capture(renderer, sprites, output / "world-socket-hidden-baseline.png");
    ShotTrace hiddenShot;
    hiddenShot.actor = hiddenShooter.id; hiddenShot.from = hiddenShooter.position; hiddenShot.to = hiddenShooter.position + sf::Vector2f{100.f, 0.f};
    hiddenShot.remaining = GameplayConfig::ShotTraceDuration; hiddenShot.fromHeight = PresentationConfig::ShotHeight; hiddenShot.toHeight = hiddenShot.fromHeight;
    sprites.shots.push_back(hiddenShot);
    const auto hiddenSource = capture(renderer, sprites, output / "world-socket-hidden-source.png");
    sprites.agents.resize(1);
    const auto redactedSource = capture(renderer, sprites, output / "world-socket-redacted-source.png");
    for (unsigned int y = 0; y < hiddenSource.getSize().y; ++y) for (unsigned int x = 0; x < hiddenSource.getSize().x; ++x) {
      const sf::Vector2u pixel{x, y};
      if (hiddenSourceBaseline.getPixel(pixel) != PresentationConfig::Light &&
          (hiddenSource.getPixel(pixel) == PresentationConfig::Light || redactedSource.getPixel(pixel) == PresentationConfig::Light)) {
        throw std::runtime_error("Una fuente oculta o eliminada del snapshot inventa un destello de arma");
      }
    }
    sprites.shots.clear();
    metadata.push_back({{"file", "world-socket-redacted-source.png"}, {"noInventedMuzzleFlash", true}});
    Simulation hostAnimation;
    hostAnimation.ResetBase();
    auto &animationWorld = const_cast<WorldState &>(hostAnimation.GetState());
    animationWorld.phase = GamePhase::Mission; animationWorld.size = {700.f, 600.f}; animationWorld.objects.clear(); animationWorld.obstacles.clear();
    animationWorld.agents[0].position = {200.f, 300.f}; animationWorld.agents[0].facing = {1.f, 0.f};
    Guard walkingGuard;
    walkingGuard.id = 1; walkingGuard.position = {320.f, 300.f}; walkingGuard.facing = {1.f, 0.f};
    walkingGuard.patrol = {{450.f, 300.f}}; walkingGuard.lastKnown = {450.f, 300.f}; walkingGuard.stepTime = 0.2f;
    animationWorld.guards = {walkingGuard};
    Simulation clientAnimation;
    std::string animationError;
    if (!clientAnimation.LoadSnapshot(hostAnimation.SnapshotFor(1, true), animationError)) throw std::runtime_error("El fixture de animación no carga el snapshot filtrado: " + animationError);
    const auto &clientGuard = clientAnimation.GetState().guards.at(0);
    if (!clientGuard.patrol.empty() || clientGuard.lastKnown != clientGuard.position || clientGuard.stepTime <= 0.f) throw std::runtime_error("La prueba de locomoción LAN no utiliza un snapshot con destino IA privado");
    const auto hostWalking = capture(renderer, animationWorld, output / "world-guard-host-walking.png");
    const auto clientWalking = capture(renderer, clientAnimation.GetState(), output / "world-guard-client-walking.png");
    const auto compareImages = [](const sf::Image &first, const sf::Image &second) {
      const auto bytes = static_cast<std::size_t>(first.getSize().x) * first.getSize().y * 4;
      return first.getSize() == second.getSize() && std::equal(first.getPixelsPtr(), first.getPixelsPtr() + bytes, second.getPixelsPtr());
    };
    if (!compareImages(hostWalking, clientWalking)) throw std::runtime_error("El guardia del snapshot filtrado pierde la locomoción visible del anfitrión");
    animationWorld.guards[0].patrol = {walkingGuard.position}; animationWorld.guards[0].lastKnown = walkingGuard.position; animationWorld.guards[0].stepTime = 0.f;
    if (!clientAnimation.LoadSnapshot(hostAnimation.SnapshotFor(1, true), animationError)) throw std::runtime_error("El snapshot de guardia detenido es inválido: " + animationError);
    const auto hostStopped = capture(renderer, animationWorld, output / "world-guard-host-stopped.png");
    const auto clientStopped = capture(renderer, clientAnimation.GetState(), output / "world-guard-client-stopped.png");
    if (!compareImages(hostStopped, clientStopped) || compareImages(clientWalking, clientStopped)) throw std::runtime_error("El guardia LAN no alterna entre fotogramas de paso y reposo");
    metadata.push_back({{"file", "world-guard-client-walking.png"}, {"filteredSnapshotMatchesHost", true}, {"privatePatrolAbsent", true}, {"stepTimeControlsLocomotion", true}});
    WorldState furniture;
    furniture.phase = GamePhase::Base; furniture.size = {700.f, 700.f};
    observer.hidden = false; observer.position = {350.f, 420.f}; furniture.agents.push_back(observer);
    for (std::size_t kind = 0; kind < 3; ++kind) {
      Interactable hideout;
      hideout.id = static_cast<std::uint32_t>(kind + 1); hideout.kind = ObjectKind::Hideout;
      hideout.hideoutKind = static_cast<HideoutKind>(kind + 1); hideout.position = {240.f + kind * 110.f, 320.f};
      hideout.name = kind == 0 ? "CONTENEDOR" : kind == 1 ? "ARMARIO" : "BAJO MESA";
      hideout.occupied = kind != 0;
      furniture.objects.push_back(hideout);
    }
    Agent concealed = observer;
    concealed.id = 2; concealed.name = "COMPAÑERO OCULTO"; concealed.hidden = true; concealed.stance = Stance::Prone;
    concealed.position = furniture.objects[2].position; concealed.hideoutId = furniture.objects[2].id;
    furniture.agents.push_back(concealed);
    const auto concealedPixels = capture(renderer, furniture, output / "world-hideouts-hidden.png");
    furniture.agents.pop_back();
    const auto removedPixels = capture(renderer, furniture, output / "world-hideouts-redacted.png");
    if (!identical(concealedPixels, removedPixels)) throw std::runtime_error("El compañero oculto bajo mesa revela cuerpo, sombra, nombre o marcador en el minimapa");
    metadata.push_back({{"file", "world-hideouts-hidden.png"}, {"hiddenPartnerIdenticalToRedacted", true}, {"hideoutTypes", {"bin", "closet", "under-table"}}});
    furniture.objects.clear();
    const auto noGuide = capture(renderer, furniture, output / "world-decoy-none.png");
    renderer.SetDecoyPreview(true, {1.f, 0.f}, 230.f);
    const auto guide = capture(renderer, furniture, output / "world-decoy-guide.png");
    if (identical(noGuide, guide)) throw std::runtime_error("Preparar un señuelo no dibuja guía ni pose de lanzamiento");
    furniture.obstacles = {{{{450.f, 330.f}, {24.f, 180.f}}, CoverKind::High, true, "Límite de lanzamiento"}};
    const auto clipped = DistractionGeometry::TraceDecoy(furniture, observer.position, {1.f, 0.f}, 230.f);
    if (!clipped.blocked || clipped.travelled >= 230.f) throw std::runtime_error("La guía del señuelo no utiliza el recorte de colisiones autoritativo");
    capture(renderer, furniture, output / "world-decoy-blocked.png");
    renderer.SetDecoyPreview(false, {}, 0.f); furniture.obstacles.clear();
    const auto cancelled = capture(renderer, furniture, output / "world-decoy-cancelled.png");
    if (!identical(noGuide, cancelled)) throw std::runtime_error("Cancelar el señuelo conserva guía o postura de lanzamiento");
    metadata.push_back({{"file", "world-decoy-blocked.png"}, {"clippedDistance", clipped.travelled}, {"cancelRestoresImage", true}});
    const auto serverGuide = DistractionGeometry::TraceDecoy(furniture, observer.position, {1.f, 0.f}, 50.f);
    const auto serverMaximum = DistractionGeometry::TraceDecoy(furniture, observer.position, {1.f, 0.f}, DistractionConfig::MaximumDecoyDistance);
    furniture.visibility.authoritative = true; furniture.visibility.observer = observer.id;
    auto &authorizedGuide = furniture.agents[0].decoyGuide;
    authorizedGuide = {true, {1.f, 0.f}, 50.f, serverGuide.to, serverMaximum.to, serverGuide.travelled, serverMaximum.travelled, serverGuide.valid, serverGuide.blocked, serverMaximum.blocked};
    renderer.SetDecoyPreview(true, {1.f, 0.f}, 230.f);
    const auto authoritativeGuide = capture(renderer, furniture, output / "world-decoy-authoritative.png");
    authorizedGuide.active = false;
    capture(renderer, furniture, output / "world-decoy-awaiting-server.png");
    furniture.visibility = {}; renderer.SetDecoyPreview(true, {1.f, 0.f}, 50.f);
    const auto hostGuide = capture(renderer, furniture, output / "world-decoy-host-reference.png");
    if (!identical(authoritativeGuide, hostGuide)) throw std::runtime_error("El cliente predice la guía con distancia local en vez del destino autorizado por el anfitrión");
    renderer.SetDecoyPreview(false, {}, 0.f);
    metadata.push_back({{"file", "world-decoy-authoritative.png"}, {"localRequestedDistance", 230.f}, {"authorizedDistance", 50.f}, {"identicalToHost", true}});
    WorldState hearing;
    hearing.phase = GamePhase::Mission; hearing.size = {700.f, 600.f};
    observer.position = {200.f, 300.f}; observer.facing = {1.f, 0.f}; hearing.agents.push_back(observer);
    hearing.obstacles = {{{{290.f, 100.f}, {20.f, 400.f}}, CoverKind::High, true, "Pared de escucha"}};
    hidden.position = {350.f, 300.f}; hidden.state = GuardState::Patrol; hearing.guards.push_back(hidden);
    FootstepCue cue;
    cue.position = {352.f, 288.f}; cue.direction = 2; cue.remaining = DistractionConfig::FootstepLifetime; cue.sourceGuard = hidden.id;
    hearing.footsteps.push_back(cue);
    const auto footsteps = capture(renderer, hearing, output / "world-footsteps-unseen.png");
    hearing.footsteps.clear();
    const auto silent = capture(renderer, hearing, output / "world-footsteps-none.png");
    if (identical(footsteps, silent)) throw std::runtime_error("Los pasos autorizados del guardia invisible no producen pistas aproximadas");
    hearing.guards.clear();
    const auto anonymous = capture(renderer, hearing, output / "world-footsteps-redacted.png");
    if (!identical(silent, anonymous)) throw std::runtime_error("La pista auditiva revela un modelo, cono, icono o marcador del guardia oculto");
    metadata.push_back({{"file", "world-footsteps-unseen.png"}, {"approximatePosition", {cue.position.x, cue.position.y}}, {"anonymous", true}, {"hiddenModelAbsent", true}});
    WorldState continuousFog;
    continuousFog.phase = GamePhase::Mission; continuousFog.size = {1200.f, 1000.f};
    Agent fogAgent;
    fogAgent.id = 1; fogAgent.position = {500.f, 500.f}; fogAgent.facing = {1.f, 0.f};
    continuousFog.agents.push_back(fogAgent);
    const std::array<const char *, 7> fogScenarios{{"open-room", "near-wall", "open-door", "corner", "hidden-corridor", "moving", "zoom"}};
    const auto wall = [](sf::Vector2f position, sf::Vector2f size) { return Obstacle{{position, size}, CoverKind::High, true, "Oclusión"}; };
    for (std::size_t scenario = 0; scenario < fogScenarios.size(); ++scenario) {
      continuousFog.objects.clear(); continuousFog.obstacles.clear(); continuousFog.agents[0] = fogAgent;
      if (scenario == 1) continuousFog.obstacles = {wall({590.f, 180.f}, {20.f, 640.f})};
      if (scenario == 2) {
        continuousFog.obstacles = {wall({590.f, 180.f}, {20.f, 288.f}), wall({590.f, 532.f}, {20.f, 288.f})};
        Interactable entrance;
        entrance.id = 6; entrance.kind = ObjectKind::Door; entrance.position = {600.f, 500.f}; entrance.open = true;
        continuousFog.objects.push_back(entrance);
      }
      if (scenario == 3) continuousFog.obstacles = {wall({590.f, 260.f}, {20.f, 240.f}), wall({590.f, 260.f}, {220.f, 20.f})};
      if (scenario == 4) continuousFog.obstacles = {wall({590.f, 180.f}, {20.f, 640.f}), wall({610.f, 180.f}, {400.f, 20.f}), wall({610.f, 800.f}, {400.f, 20.f})};
      if (scenario == 5) continuousFog.agents[0].position += sf::Vector2f{13.f, 7.f};
      const auto continuousBoundary = Visibility::BuildGrid(continuousFog, continuousFog.agents[0]);
      const auto hiddenFloor = continuousFog.agents[0].position + sf::Vector2f{150.f, 0.f};
      if ((scenario == 1 || scenario == 4) && WorldRenderer::FogOpacity(continuousBoundary, hiddenFloor) != 1.f) throw std::runtime_error("La niebla suave atraviesa un muro en fixture visual");
      for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
        const std::size_t preferredZoom = scenario == 0 || scenario == 5 ? 0 : 1;
        if (scenario != 6 && zoomIndex != preferredZoom) continue;
        renderer.ResetZoom();
        if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
        const auto file = output / ("world-soft-fog-" + std::string(fogScenarios[scenario]) + "-" + zoomNames[zoomIndex] + ".png");
        const auto pixels = capture(renderer, continuousFog, file);
        if (scenario == 0) {
          std::set<std::uint32_t> transitionColors;
          for (int distance = 367; distance < 400; ++distance) {
            const auto point = WorldRenderer::ProjectGround(fogAgent.position + sf::Vector2f{static_cast<float>(distance), 0.f}, fogAgent.position, context.window.GetLogicalSize(), renderer.GetZoom());
            const auto pixel = point.componentWiseDiv(context.window.GetLogicalSize()).componentWiseMul(sf::Vector2f(pixels.getSize()));
            transitionColors.insert(pixels.getPixel(sf::Vector2u(pixel)).toInteger());
          }
          if (transitionColors.size() < 12) throw std::runtime_error("La captura real conserva opacidades binarias en el borde de niebla");
          metadata.push_back({{"file", file.filename().string()}, {"capturedTransitionColors", transitionColors.size()}, {"smoothPixelsVerified", true}});
        }
        metadata.push_back({{"file", file.filename().string()}, {"fogScenario", fogScenarios[scenario]}, {"zoom", renderer.GetZoom()},
            {"continuousRayCount", continuousBoundary.visualRadii.size()}, {"featherWidth", PresentationConfig::FogFeatherWidth}, {"tileMaskDrawn", false}});
      }
    }
    continuousFog.objects.clear(); continuousFog.obstacles.clear(); continuousFog.agents[0] = fogAgent;
    const std::array<const char *, 4> eyeStates{{"quiet", "partial", "imminent", "confirmed"}};
    const std::array<float, 4> eyeProgress{{0.f, 0.5f, 0.9f, 1.f}};
    const auto eyeCrop = [&](const sf::Image &pixels) {
      std::vector<std::uint32_t> values;
      const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(context.window.GetLogicalSize());
      const auto from = (PresentationConfig::DetectionHudCenter - sf::Vector2f{34.f, 24.f}).componentWiseMul(scale);
      const auto to = (PresentationConfig::DetectionHudCenter + sf::Vector2f{34.f, 24.f}).componentWiseMul(scale);
      for (unsigned int y = static_cast<unsigned int>(from.y); y < static_cast<unsigned int>(to.y); ++y) for (unsigned int x = static_cast<unsigned int>(from.x); x < static_cast<unsigned int>(to.x); ++x) {
        values.push_back(pixels.getPixel({x, y}).toInteger());
      }
      return values;
    };
    std::vector<std::uint32_t> previousEye;
    for (std::size_t state = 0; state < eyeStates.size(); ++state) {
      continuousFog.agents[0].detectionProgress = eyeProgress[state]; continuousFog.agents[0].detectionConfirmed = state == 3;
      std::vector<std::uint32_t> reference;
      for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
        renderer.ResetZoom();
        if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
        const auto file = output / ("world-eye-" + std::string(eyeStates[state]) + "-" + zoomNames[zoomIndex] + ".png");
        const auto pixels = capture(renderer, continuousFog, file);
        const auto crop = eyeCrop(pixels);
        if (zoomIndex == 0) {
          reference = crop;
          if (!previousEye.empty() && previousEye == crop) throw std::runtime_error("El ojo no cambia de representación al crecer la detección autoritativa");
          previousEye = crop;
        } else if (reference != crop) throw std::runtime_error("El zoom del mundo desplaza o escala el icono de vigilancia del HUD");
        metadata.push_back({{"file", file.filename().string()}, {"detectionProgress", eyeProgress[state]}, {"confirmed", state == 3}, {"fixedHudAcrossZoom", true}});
      }
    }
    renderer.ResetZoom();
    continuousFog.obstacles = {wall({590.f, 180.f}, {20.f, 640.f})};
    Guard privateWatcher;
    privateWatcher.id = 9; privateWatcher.position = {700.f, 500.f}; privateWatcher.suspicion = 0.9f; privateWatcher.state = GuardState::Alert;
    continuousFog.guards = {privateWatcher};
    const auto eyePrivate = capture(renderer, continuousFog, output / "world-eye-private-observer.png");
    continuousFog.guards.clear();
    const auto eyeRedacted = capture(renderer, continuousFog, output / "world-eye-private-redacted.png");
    if (!identical(eyePrivate, eyeRedacted)) throw std::runtime_error("El indicador de vigilancia revela la posición de un observador privado");
    metadata.push_back({{"file", "world-eye-private-observer.png"}, {"hiddenObserverIdenticalToRedacted", true}});
    const auto eyeMenuHidden = capture(renderer, continuousFog, output / "world-eye-menu-hidden.png", false);
    const auto pupil = PresentationConfig::DetectionHudCenter.componentWiseDiv(context.window.GetLogicalSize()).componentWiseMul(sf::Vector2f(eyeMenuHidden.getSize()));
    if (eyeRedacted.getPixel(sf::Vector2u(pupil)) != PresentationConfig::Light || eyeMenuHidden.getPixel(sf::Vector2u(pupil)) == PresentationConfig::Light || eyeCrop(eyeMenuHidden) == eyeCrop(eyeRedacted)) {
      throw std::runtime_error("El ojo de vigilancia permanece detrás de los menús o pausa");
    }
    metadata.push_back({{"file", "world-eye-menu-hidden.png"}, {"hudVisible", false}, {"eyeSuppressedForMenus", true}});
    WorldState inspectedFurniture = furniture;
    inspectedFurniture.visibility = {}; inspectedFurniture.objects.clear(); inspectedFurniture.obstacles.clear();
    inspectedFurniture.agents[0].position = {350.f, 420.f};
    Interactable inspectionTable;
    inspectionTable.id = 50; inspectionTable.kind = ObjectKind::Hideout; inspectionTable.hideoutKind = HideoutKind::UnderTable; inspectionTable.position = {350.f, 340.f};
    inspectedFurniture.objects.push_back(inspectionTable);
    const auto restingTable = capture(renderer, inspectedFurniture, output / "world-table-resting.png");
    inspectedFurniture.objects[0].inspectionProgress = 0.5f;
    const auto inspectingTable = capture(renderer, inspectedFurniture, output / "world-table-inspecting.png");
    inspectedFurniture.objects[0].inspectionProgress = 0.f; inspectedFurniture.objects[0].lifted = true;
    const auto liftedTable = capture(renderer, inspectedFurniture, output / "world-table-lifted.png");
    if (identical(restingTable, inspectingTable) || identical(inspectingTable, liftedTable)) throw std::runtime_error("La mesa no representa la transición lógica de inspección y levantamiento");
    metadata.push_back({{"file", "world-table-inspecting.png"}, {"inspectionProgress", 0.5f}, {"distinctLiftedState", true}});
    WorldState partialHideout;
    partialHideout.phase = GamePhase::Mission; partialHideout.size = {1200.f, 1000.f};
    fogAgent.position = {500.f, 500.f}; fogAgent.hidden = true; fogAgent.hideoutId = inspectionTable.id; fogAgent.stance = Stance::Prone;
    partialHideout.agents = {fogAgent}; inspectionTable.position = fogAgent.position; inspectionTable.occupantAgent = fogAgent.id; inspectionTable.occupied = true;
    partialHideout.objects = {inspectionTable};
    const auto tableForward = capture(renderer, partialHideout, output / "world-table-view-forward.png");
    partialHideout.agents[0].facing = {-1.f, 0.f};
    const auto tableBackward = capture(renderer, partialHideout, output / "world-table-view-backward.png");
    if (identical(tableForward, tableBackward)) throw std::runtime_error("La niebla desde debajo de la mesa no gira con la orientación real");
    partialHideout.objects[0].hideoutKind = HideoutKind::Closet; partialHideout.objects[0].facing = {1.f, 0.f};
    partialHideout.agents[0].facing = {1.f, 0.f};
    const auto closetClosed = capture(renderer, partialHideout, output / "world-closet-view-closed.png");
    partialHideout.objects[0].open = true;
    const auto closetOpen = capture(renderer, partialHideout, output / "world-closet-view-open.png");
    if (identical(closetClosed, closetOpen)) throw std::runtime_error("Abrir el armario no modifica su abertura ni su visión exterior limitada");
    metadata.push_back({{"file", "world-table-view-forward.png"}, {"orientationChangesFog", true}, {"hiddenSilhouetteOmitted", true}});
    metadata.push_back({{"file", "world-closet-view-open.png"}, {"closedAndOpenDiffer", true}, {"limitedAperture", partialHideout.objects[0].hideoutAperture}});
    std::ofstream(output / "world-fixtures.json") << metadata.dump(2);
    return metadata;
  }
}
