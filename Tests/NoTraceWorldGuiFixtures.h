#include "NoTraceSpatialFixture.h"
#include "NoTraceCharacterAtlasFixture.h"
#pragma once

#include "NoTraceWorldRenderer.h"
#include "NoTraceDistractionGeometry.h"
#include "NoTraceSimulation.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceAudioConfig.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/Image.hpp>
#include <array>
#include <functional>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunWorldRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    WorldRenderer renderer(context);
    CharacterAtlasFixture characterAtlas;
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
    std::vector<BitmapFrame> operativeBitmapFrames(SpriteConfig::FrameCount);
    std::vector<WorldRenderer::GroundSupport> operativeSupports(operativeBitmapFrames.size());
    for (const auto &[index, entry] : characterAtlas.frames) {
      operativeBitmapFrames[index] = {{{0, 0}, {192, 192}}, CharacterAtlasFixture::Vector(entry.at("muzzle"))};
      operativeSupports[index] = {CharacterAtlasFixture::Vector(entry.at("groundAnchor")), entry.at("supportWidth").get<float>()};
    }
    const auto bitmapMuzzle = [&](WorldRenderer::SpriteFrame selected, sf::Vector2f position, sf::Vector2f facing, Stance stance, sf::Vector2f camera, float zoom, bool guard = false, std::optional<WorldRenderer::SpriteFrame> lower = std::nullopt) {
      if (selected.operative) {
        const auto logical = context.window.GetLogicalSize();
        const auto feet = WorldRenderer::ProjectGround(position, camera, logical);
        const auto upperFeet = lower ? renderer.CharacterSprites().AlignUpper(lower->index, selected.index, feet) : feet;
        const auto point = renderer.CharacterSprites().Muzzle(selected.index, upperFeet, guard ? CharacterArt::Weapon::GuardRifle : CharacterArt::Weapon::Pistol, !guard,
            selected.index / SpriteConfig::Columns % SpriteConfig::Directions);
        const auto center = WorldRenderer::ProjectGround(camera, camera, logical);
        return center + (point - center) * zoom;
      }
      const auto &frame = (selected.operative ? operativeBitmapFrames : selected.prone ? proneBitmapFrames : bitmapFrames).at(selected.index);
      if (frame.muzzle.x < 0.f) throw std::runtime_error("La pose disparando no tiene una boca real visible en el PNG");
      const auto size = context.window.GetLogicalSize();
      const auto feet = WorldRenderer::ProjectGround(position, camera, size);
      const auto placement = WorldRenderer::PlaceSprite(feet, facing, stance, false, selected, frame.rectangle.size);
      const auto center = WorldRenderer::ProjectGround(camera, camera, size);
      return center + (placement.Map(frame.muzzle) - center) * zoom;
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
      if (!barrel || !flash) throw std::runtime_error("La boca del PNG y el destello capturado se separan: " + scenario +
          " (PNG " + (barrel ? "presente" : "ausente") + "; destello " + (flash ? "presente" : "ausente") + ")");
    };
    WorldState world;
    world.phase = GamePhase::Base;
    world.size = {700.f, 700.f};
    world.elapsed = 1.f;
    world.obstacles = {
        {{{300.f, 300.f}, {100.f, 100.f}}, CoverKind::High, true, "Caja principal"},
        {{{455.f, 380.f}, {85.f, 50.f}}, CoverKind::Low, true, "Caja secundaria"}};
    Interactable terminal;
    terminal.id = 90;
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
        world.agents[0].hidden = true;
        const auto absent = capture(renderer, world, output / (file.stem().string() + "-absent.png"));
        const auto obstacles = world.obstacles; world.obstacles.clear(); world.agents[0].hidden = false;
        const auto unobstructed = capture(renderer, world, output / (file.stem().string() + "-unobstructed.png"));
        world.agents[0].hidden = true;
        const auto unobstructedAbsent = capture(renderer, world, output / (file.stem().string() + "-unobstructed-absent.png"));
        world.obstacles = obstacles; world.agents[0].hidden = false;
        const auto logicalSize = context.window.GetLogicalSize();
        const auto focus = WorldRenderer::ProjectGround(positions[scenario], positions[scenario], logicalSize, renderer.GetZoom());
        const auto guardTorso = WorldRenderer::ProjectGround(front.position, positions[scenario], logicalSize, renderer.GetZoom()) - sf::Vector2f{0.f, PresentationConfig::ShotHeight * renderer.GetZoom()};
        const auto guardAim = renderer.AimAt(guardTorso, world, 1);
        const auto guardHit = CombatGeometry::IntersectActor(positions[scenario], CombatGeometry::Unit(guardAim.ground - positions[scenario]),
            WorldRenderer::WeaponHeight(world.agents[0].stance), guardAim.slope, GameplayConfig::WeaponRange, front.position, front.facing, Stance::Standing);
        if (!guardHit) throw std::runtime_error("El clic en el torso capturado no atraviesa un volumen válido del guardia");
        const auto silhouette = [&](sf::Vector2f offset, sf::Vector2f halfSize) {
          const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(logicalSize);
          const auto center = (focus + offset * renderer.GetZoom()).componentWiseMul(scale);
          const auto half = (halfSize * renderer.GetZoom()).componentWiseMul(scale);
          std::array<unsigned int, 2> counts{};
          for (int y = static_cast<int>(std::floor(center.y - half.y)); y <= static_cast<int>(std::ceil(center.y + half.y)); ++y)
            for (int x = static_cast<int>(std::floor(center.x - half.x)); x <= static_cast<int>(std::ceil(center.x + half.x)); ++x) {
              if (x < 0 || y < 0 || x >= static_cast<int>(pixels.getSize().x) || y >= static_cast<int>(pixels.getSize().y)) continue;
              const sf::Vector2u point{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
              if (unobstructed.getPixel(point) == unobstructedAbsent.getPixel(point)) continue;
              ++counts[0]; if (pixels.getPixel(point) != absent.getPixel(point)) ++counts[1];
            }
          return counts;
        };
        // La franja abdominal queda bajo la tapa; el pecho superior puede sobresalir junto a la cabeza.
        const auto torso = silhouette({0.f, -18.f}, {8.f, 4.f});
        const auto head = silhouette({0.f, -PresentationConfig::ActorHeight * 0.8f}, {12.f, 8.f});
        if (!torso[0] || (scenario == 0 ? torso[1] != 0 : torso[1] == 0)) throw std::runtime_error("La comparación de silueta real no muestra la oclusión esperada del torso: " + file.string());
        if (!head[0] || !head[1]) throw std::runtime_error("La cabeza del bitmap no permanece parcialmente visible al rodear la caja: " + file.string());
        metadata.push_back({{"file", file.filename().string()}, {"zoom", renderer.GetZoom()},
            {"agent", {positions[scenario].x, positions[scenario].y}}, {"drawOrder", order},
            {"torsoReferencePixels", torso[0]}, {"torsoVisiblePixels", torso[1]}, {"torsoOccluded", scenario == 0}, {"headReferencePixels", head[0]}, {"headVisiblePixels", head[1]},
            {"guardTorsoCursor", {guardTorso.x, guardTorso.y}}, {"guardAimGround", {guardAim.ground.x, guardAim.ground.y}}, {"aimSlope", guardAim.slope}});
      }
    }
    const std::array<const char *, 4> shotNames{{"standing", "crouched", "prone", "prone-blocked"}};
    const std::array<Stance, 4> shotStances{{Stance::Standing, Stance::Crouched, Stance::Prone, Stance::Prone}};
    renderer.ResetZoom();
    for (std::size_t index = 0; index < shotNames.size(); ++index) {
      // Cada estado aislado parte sin la histéresis corporal de las escenas anteriores de oclusión.
      WorldRenderer shotRenderer(context);
      auto &shooter = world.agents[0];
      shooter.position = {350.f, 470.f};
      shooter.stance = shotStances[index];
      shooter.actionPose = ActionPose::Shoot; shooter.actionPoseTime = GameplayConfig::AttackAnimationDuration * 0.75f;
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
      const auto body = capture(shotRenderer, world, output / (stem + "-weapon.png"));
      shooter.hidden = true;
      const auto absent = capture(shotRenderer, world, output / (stem + "-absent.png"));
      shooter.hidden = false; world.shots = shot;
      const auto pixels = capture(shotRenderer, world, file);
      const auto logicalSize = context.window.GetLogicalSize();
      const auto selected = WorldRenderer::SelectWeaponSprite(shooter);
      // La boca corresponde al torso apuntado, cuya dirección es independiente de la histéresis de las piernas.
      const auto visualDirection = WorldRenderer::SpriteDirection(WeaponFacing(shooter));
      const auto muzzle = bitmapMuzzle(selected, shooter.position, WeaponFacing(shooter), shooter.stance, shooter.position, shotRenderer.GetZoom(), false, WorldRenderer::SelectBodySprite(shooter));
      const auto pixel = muzzle.componentWiseDiv(logicalSize).componentWiseMul(sf::Vector2f(pixels.getSize()));
      const sf::Vector2u sample{static_cast<unsigned int>(std::floor(pixel.x)), static_cast<unsigned int>(std::floor(pixel.y))};
      requireBitmapFlash(body, absent, pixels, muzzle, stem);
      metadata.push_back({{"file", file.filename().string()}, {"stance", shotNames[index]},
          {"sourceHeight", WorldRenderer::WeaponHeight(shooter.stance)}, {"impactHeight", WorldRenderer::ShotImpactHeight(world.shots[0], world)},
          {"muzzleSample", {sample.x, sample.y}}, {"atlasFrame", selected.index}, {"expectedVisualDirection", visualDirection}, {"muzzleFromBitmap", true}, {"blocked", blocked}});
    }
    world.agents[0].actionPose = ActionPose::None; world.agents[0].actionPoseTime = 0.f;
    WorldState fogWorld;
    fogWorld.phase = GamePhase::Mission; fogWorld.size = {700.f, 600.f};
    Agent observer;
    observer.id = 1; observer.name = "VISIBILIDAD"; observer.position = {200.f, 300.f}; observer.facing = {1.f, 0.f};
    fogWorld.agents.push_back(observer);
    fogWorld.obstacles = {{{{290.f, 100.f}, {20.f, 400.f}}, CoverKind::High, true, "Muro opaco"}};
    Guard hidden;
    hidden.id = 1; hidden.position = {350.f, 300.f}; hidden.state = GuardState::Alert; hidden.detectedTime = 0.5f;
    fogWorld.guards.push_back(hidden);
    terminal.position = {360.f, 320.f}; fogWorld.objects.push_back(terminal);
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
    door.id = 91; door.kind = ObjectKind::Door; door.position = {300.f, 300.f};
    fogWorld.objects.push_back(door); fogWorld.guards.push_back(hidden);
    if (Visibility::GuardVisible(fogWorld, observer, hidden)) throw std::runtime_error("La puerta cerrada permite ver al enemigo");
    capture(renderer, fogWorld, output / "world-fog-door-closed.png");
    fogWorld.objects[0].open = true;
    if (!Visibility::GuardVisible(fogWorld, observer, hidden)) throw std::runtime_error("Abrir la puerta no actualiza la visión del corredor");
    capture(renderer, fogWorld, output / "world-fog-door-open.png");
    metadata.push_back({{"file", "world-fog-door-open.png"}, {"visibleAfterOpeningDoor", true}});
    NoTrace::TestFixture::LegacyPlane(world);
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
    for (std::size_t posture = 0; posture < stanceNames.size(); ++posture) for (int frame = 0; frame < 4; ++frame) {
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
      capture(renderer, sprites, output / file);
      for (std::size_t actorIndex = 1; actorIndex < sprites.agents.size(); ++actorIndex) {
        auto &actor = sprites.agents[actorIndex]; actor.moving = false;
        actor.actionPose = ActionPose::Shoot; actor.actionPoseTime = GameplayConfig::AttackAnimationDuration * (0.98f - static_cast<float>(frame) * 0.25f);
      }
      const auto body = capture(renderer, sprites, output / ("world-sprites-shoot-weapon-" + std::string(stanceNames[posture]) + "-" + std::to_string(frame) + ".png"));
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
        const auto selected = WorldRenderer::SelectActorSprite(actor);
        const auto muzzle = bitmapMuzzle(selected, actor.position, actor.facing, actor.stance, sprites.agents[0].position, renderer.GetZoom(), false, WorldRenderer::SelectBodySprite(actor));
        requireBitmapFlash(body, absent, firing, muzzle, file + " dirección=" + std::to_string(actorIndex - 1));
      }
      sprites.shots.clear();
      metadata.push_back({{"file", file}, {"directions", 8}, {"designedAnimationFrame", frame}, {"flashUsesSeparateShootPose", true}});
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
      capture(renderer, sprites, output / ("world-guard-locomotion-" + std::to_string(movingFrame + 1) + ".png"));
      for (auto &actor : sprites.guards) {
        actor.meleeAttack = false; actor.attackTime = GameplayConfig::AttackAnimationDuration * (0.98f - static_cast<float>((movingFrame + 1) % 4) * 0.25f);
      }
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
        const auto selected = WorldRenderer::SelectGuardSprite(actor, sprites);
        const auto muzzle = bitmapMuzzle(selected, actor.position, actor.facing, Stance::Standing, sprites.agents[0].position, renderer.GetZoom(), true);
        requireBitmapFlash(body, absent, firing, muzzle, stem + " dirección=" + std::to_string(actor.id - 1));
      }
      sprites.shots.clear(); sprites.guards.clear();
      metadata.push_back({{"file", stem + "-flash.png"}, {"guardDirections", 8}, {"shootingFrame", (movingFrame + 1) % 4}, {"flashUsesSeparateShootPose", true}});
    }
    Agent hiddenShooter;
    hiddenShooter.id = 2; hiddenShooter.hidden = true; hiddenShooter.position = {520.f, 400.f};
    hiddenShooter.actionPose = ActionPose::Shoot; hiddenShooter.actionPoseTime = GameplayConfig::AttackAnimationDuration;
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
    NoTrace::TestFixture::LegacyPlane(animationWorld);
    animationWorld.phase = GamePhase::Mission; animationWorld.mode = SimulationMode::Operation; animationWorld.size = {700.f, 600.f}; animationWorld.objects.clear(); animationWorld.obstacles.clear();
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
      NoTrace::TestFixture::LegacyPlane(continuousFog);
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
          for (int distance = static_cast<int>(VisibilityConfig::StandingRange - PresentationConfig::FogFeatherWidth + 1.f); distance < static_cast<int>(VisibilityConfig::StandingRange); ++distance) {
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
    NoTrace::TestFixture::LegacyPlane(continuousFog);
    continuousFog.objects.clear(); continuousFog.obstacles.clear(); continuousFog.agents[0] = fogAgent;
    const std::array<const char *, 4> eyeStates{{"quiet", "yellow", "orange", "confirmed"}};
    const std::array<SecurityAlert, 4> eyeAlerts{{SecurityAlert::Calm, SecurityAlert::Yellow, SecurityAlert::Orange, SecurityAlert::Red}};
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
      continuousFog.securityAlert = eyeAlerts[state];
      std::vector<std::uint32_t> reference;
      for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
        renderer.ResetZoom();
        if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
        const auto file = output / ("world-eye-" + std::string(eyeStates[state]) + "-" + zoomNames[zoomIndex] + ".png");
        const auto pixels = capture(renderer, continuousFog, file);
        const auto crop = eyeCrop(pixels);
        if (zoomIndex == 0) {
          reference = crop;
          if (!previousEye.empty() && previousEye == crop) throw std::runtime_error("El ojo no cambia de representación al cambiar la alerta global autoritativa");
          previousEye = crop;
        } else if (reference != crop) throw std::runtime_error("El zoom del mundo desplaza o escala el icono de vigilancia del HUD");
        metadata.push_back({{"file", file.filename().string()}, {"securityAlert", static_cast<int>(eyeAlerts[state])}, {"confirmed", state == 3}, {"fixedHudAcrossZoom", true}});
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
    const auto pupil = (PresentationConfig::DetectionHudCenter + sf::Vector2f{-4.f, 0.f}).componentWiseDiv(context.window.GetLogicalSize()).componentWiseMul(sf::Vector2f(eyeMenuHidden.getSize()));
    if (eyeRedacted.getPixel(sf::Vector2u(pupil)) != PresentationConfig::Red || eyeMenuHidden.getPixel(sf::Vector2u(pupil)) == PresentationConfig::Red || eyeCrop(eyeMenuHidden) == eyeCrop(eyeRedacted)) {
      throw std::runtime_error("El ojo de vigilancia permanece detrás de los menús o pausa");
    }
    metadata.push_back({{"file", "world-eye-menu-hidden.png"}, {"hudVisible", false}, {"eyeSuppressedForMenus", true}});
    WorldState inspectedFurniture = furniture;
    NoTrace::TestFixture::LegacyPlane(inspectedFurniture);
    inspectedFurniture.visibility = {}; inspectedFurniture.objects.clear(); inspectedFurniture.obstacles.clear();
    inspectedFurniture.agents[0].position = {350.f, 420.f};
    Interactable inspectionTable;
    inspectionTable.id = 50; inspectionTable.kind = ObjectKind::Hideout; inspectionTable.hideoutKind = HideoutKind::UnderTable; inspectionTable.position = {350.f, 340.f};
    inspectedFurniture.objects.push_back(inspectionTable);
    const auto restingTable = capture(renderer, inspectedFurniture, output / "world-table-resting.png");
    inspectedFurniture.objects[0].inspectionProgress = 0.5f;
    const auto inspectingTable = capture(renderer, inspectedFurniture, output / "world-table-inspecting.png");
    inspectedFurniture.objects[0].inspectionProgress = 0.f; inspectedFurniture.objects[0].overturned = true;
    const auto liftedTable = capture(renderer, inspectedFurniture, output / "world-table-lifted.png");
    if (identical(restingTable, inspectingTable) || identical(inspectingTable, liftedTable)) throw std::runtime_error("La mesa no representa la transición lógica de inspección y levantamiento");
    metadata.push_back({{"file", "world-table-inspecting.png"}, {"inspectionProgress", 0.5f}, {"distinctLiftedState", true}});
    WorldState partialHideout;
    partialHideout.phase = GamePhase::Mission; partialHideout.size = {1200.f, 1000.f};
    fogAgent.position = {500.f, 500.f}; fogAgent.hidden = true; fogAgent.hideoutId = inspectionTable.id; fogAgent.stance = Stance::Prone; fogAgent.weaponAim = {1.f, 0.f};
    partialHideout.agents = {fogAgent}; inspectionTable.position = fogAgent.position; inspectionTable.occupantAgent = fogAgent.id; inspectionTable.occupied = true; inspectionTable.overturned = false;
    partialHideout.objects = {inspectionTable};
    const auto tableForward = capture(renderer, partialHideout, output / "world-table-view-forward.png");
    partialHideout.agents[0].facing = {-1.f, 0.f};
    const auto tableBackward = capture(renderer, partialHideout, output / "world-table-view-backward.png");
    if (!identical(tableForward, tableBackward)) throw std::runtime_error("El cuerpo oculto altera el cono de visión dirigido por una puntería constante");
    const auto &tableObserver = partialHideout.agents[0];
    const float tableRange = VisibilityConfig::ProneRange, probeDistance = tableRange * .75f;
    const auto tableVisible = [&](sf::Vector2f offset) { return Visibility::IsGeometricallyVisible(partialHideout, tableObserver, tableObserver.position + offset, Stance::Prone); };
    const float insideCos = VisibilityConfig::UnderTableViewCosHalfAngle + .05f, outsideCos = VisibilityConfig::UnderTableViewCosHalfAngle - .05f;
    if (!tableVisible({probeDistance, 0.f}) || tableVisible({tableRange + 1.f, 0.f}) || tableVisible({-probeDistance, 0.f}) ||
        !tableVisible(sf::Vector2f{insideCos, std::sqrt(1.f - insideCos * insideCos)} * probeDistance) ||
        tableVisible(sf::Vector2f{outsideCos, std::sqrt(1.f - outsideCos * outsideCos)} * probeDistance))
      throw std::runtime_error("La visión manual bajo mesa amplía el alcance o la apertura tumbados");
    partialHideout.agents[0].weaponAim = {-1.f, 0.f};
    const auto tableTurned = capture(renderer, partialHideout, output / "world-table-view-manual-turned.png");
    if (identical(tableBackward, tableTurned)) throw std::runtime_error("Girar la puntería no gira manualmente el cono bajo la mesa");
    partialHideout.objects[0].hideoutKind = HideoutKind::Closet; partialHideout.objects[0].facing = {1.f, 0.f};
    partialHideout.agents[0].facing = {1.f, 0.f};
    const auto closetClosed = capture(renderer, partialHideout, output / "world-closet-view-closed.png");
    partialHideout.objects[0].open = true;
    const auto closetOpen = capture(renderer, partialHideout, output / "world-closet-view-open.png");
    if (identical(closetClosed, closetOpen)) throw std::runtime_error("Abrir el armario no modifica su abertura ni su visión exterior limitada");
    metadata.push_back({{"file", "world-table-view-forward.png"}, {"allDirectionsVisible", false}, {"manualCone", true}, {"weaponAimIndependentOfBody", true},
        {"range", tableRange}, {"cosHalfAngle", VisibilityConfig::UnderTableViewCosHalfAngle}, {"hiddenSilhouetteOmitted", true}});
    metadata.push_back({{"file", "world-table-view-manual-turned.png"}, {"manualConeRotates", true}, {"rangeAndAperturePreserved", true}});
    metadata.push_back({{"file", "world-closet-view-open.png"}, {"closedAndOpenDiffer", true}, {"limitedAperture", partialHideout.objects[0].hideoutAperture}});
    renderer.ResetZoom();
    WorldState tactical;
    tactical.phase = GamePhase::Base; tactical.size = {900.f, 800.f}; tactical.elapsed = 1.f;
    Agent fighter; fighter.id = 1; fighter.position = {430.f, 430.f}; fighter.facing = {1.f, 0.f}; fighter.name = "CQC";
    tactical.agents = {fighter};
    Guard opponent; opponent.id = 9; opponent.position = {460.f, 430.f}; opponent.facing = {1.f, 0.f}; opponent.lastKnown = opponent.position;
    tactical.guards = {opponent};
    for (const bool kick : {false, true}) {
      const float duration = kick ? CqcConfig::KickCooldown : CqcConfig::PunchCooldown;
      const float windup = kick ? CqcConfig::KickWindup : CqcConfig::PunchWindup;
      const std::array<float, 4> times{{duration, duration - windup - 0.01f, duration * 0.35f, 0.05f}};
      const std::array<const char *, 4> phases{{"windup", "impact", "follow-through", "recovery"}};
      sf::Image preceding;
      std::set<std::size_t> selectedFrames;
      for (std::size_t phase = 0; phase < times.size(); ++phase) {
        auto &actor = tactical.agents[0]; actor.cqcAttackTime = times[phase]; actor.cqcNextKick = !kick; actor.cqcStrikePending = phase == 0;
        actor.actionPose = phase == 3 ? ActionPose::Recover : kick ? ActionPose::Kick : ActionPose::Punch; actor.actionPoseTime = times[phase];
        tactical.guards[0].staggerRemaining = phase == 1 ? CqcConfig::PunchStagger : 0.f;
        const auto file = "world-cqc-" + std::string(kick ? "kick-" : "punch-") + phases[phase] + ".png";
        const auto pixels = capture(renderer, tactical, output / file);
        const auto selected = WorldRenderer::SelectActorSprite(actor);
        if (!selected.operative || !selectedFrames.insert(selected.index).second) throw std::runtime_error("Las cuatro fases CQC reutilizan la misma celda del bitmap operativo");
        if (phase > 0 && identical(preceding, pixels)) throw std::runtime_error("Las fases de golpe CQC no cambian extremidad ni reacción del objetivo");
        if (phase == 1) {
          auto resting = tactical.guards[0]; resting.staggerRemaining = 0.f;
          if (WorldRenderer::SelectGuardSprite(resting, tactical).index == WorldRenderer::SelectGuardSprite(tactical.guards[0], tactical).index)
            throw std::runtime_error("El impacto no selecciona la reacción real del bitmap del objetivo");
        }
        preceding = pixels;
        metadata.push_back({{"file", file}, {"action", kick ? "kick" : "punch"}, {"phase", phases[phase]}, {"remaining", times[phase]}, {"atlasFrame", selected.index}, {"distinctBitmapPhases", true}});
      }
    }
    opponent.position = fighter.position + fighter.facing * CqcConfig::GripSeparation;
    tactical.guards[0].position = opponent.position;
    tactical.agents[0] = fighter; tactical.agents[0].cqcHoldingGuard = opponent.id;
    tactical.guards[0].staggerRemaining = 0.f; tactical.guards[0].heldBy = fighter.id;
    sf::Image precedingGrip;
    for (unsigned int stage = 0; stage < CqcConfig::RequiredPulses; ++stage) {
      tactical.agents[0].cqcProgress = stage; tactical.agents[0].actionPose = stage == 0 ? ActionPose::Grab : ActionPose::Choke;
      tactical.agents[0].actionPoseTime = GameplayConfig::AttackAnimationDuration * (stage == 1 ? 0.5f : 0.2f);
      tactical.guards[0].cqcState = stage == 0 ? CqcState::Grabbed : stage == 1 ? CqcState::KnockedDown : CqcState::Restrained;
      const auto file = "world-cqc-grip-" + std::to_string(stage) + ".png"; const auto pixels = capture(renderer, tactical, output / file);
      if (stage > 0 && identical(precedingGrip, pixels)) throw std::runtime_error("El agarre resuelve la asfixia al primer toque o pierde sus fases");
      precedingGrip = pixels;
      metadata.push_back({{"file", file}, {"gripStage", stage}, {"targetRemainsInFront", true}, {"physicalPositionUnchanged", tactical.guards[0].position == opponent.position}});
    }
    tactical.agents[0] = fighter; tactical.guards[0] = opponent; tactical.guards[0].state = GuardState::Incapacitated;
    tactical.guards[0].incapacityRemaining = GameplayConfig::GuardIncapacityDuration - 0.08f;
    const auto falling = capture(renderer, tactical, output / "world-cqc-falling.png");
    tactical.guards[0].incapacityRemaining = GameplayConfig::GuardIncapacityDuration - PresentationConfig::CqcFallDuration;
    const auto fallen = capture(renderer, tactical, output / "world-cqc-unconscious.png");
    if (identical(falling, fallen)) throw std::runtime_error("La pérdida de consciencia omite la transición final de caída");
    metadata.push_back({{"file", "world-cqc-unconscious.png"}, {"fallTransition", true}, {"unconsciousIndicator", true}});
    tactical.guards.clear(); Agent partner = fighter; partner.id = 2; partner.position = opponent.position; partner.cqcHeldBy = fighter.id;
    tactical.agents.push_back(partner); tactical.agents[0].cqcHoldingAgent = partner.id; tactical.agents[0].actionPose = ActionPose::Grab;
    const auto friendlyGrab = capture(renderer, tactical, output / "world-cqc-ally-grab.png");
    tactical.agents[0].actionPose = ActionPose::Choke; tactical.agents[0].actionPoseTime = GameplayConfig::AttackAnimationDuration * 0.5f; tactical.agents[0].cqcProgress = 1;
    const auto friendlyChoke = capture(renderer, tactical, output / "world-cqc-ally-choke.png");
    if (identical(friendlyGrab, friendlyChoke)) throw std::runtime_error("El agarre de un aliado no anima su fase de sometimiento");
    metadata.push_back({{"file", "world-cqc-ally-choke.png"}, {"friendlyGripSharesAnimation", true}});
    WorldState occludedStrike;
    occludedStrike.phase = GamePhase::Mission; occludedStrike.size = {900.f, 800.f};
    auto strikingAgent = fighter; strikingAgent.position = {350.f, 300.f}; strikingAgent.cqcNextKick = true; strikingAgent.cqcStrikePending = false;
    strikingAgent.cqcAttackTime = CqcConfig::PunchCooldown - CqcConfig::PunchWindup - 0.01f; strikingAgent.actionPose = ActionPose::Punch; strikingAgent.actionPoseTime = strikingAgent.cqcAttackTime;
    occludedStrike.agents = {strikingAgent}; occludedStrike.obstacles = {wall({370.f, 100.f}, {20.f, 400.f})};
    auto occludedTarget = opponent; occludedTarget.position = {400.f, 300.f}; occludedStrike.guards = {occludedTarget};
    const auto concealedStrike = capture(renderer, occludedStrike, output / "world-cqc-wall-hidden-target.png");
    occludedStrike.guards.clear();
    const auto redactedStrike = capture(renderer, occludedStrike, output / "world-cqc-wall-redacted.png");
    if (!identical(concealedStrike, redactedStrike)) throw std::runtime_error("El pase final de CQC revela un objetivo oculto al otro lado de una pared");
    metadata.push_back({{"file", "world-cqc-wall-hidden-target.png"}, {"identicalToRedacted", true}, {"foregroundOcclusionPreserved", true}});
    WorldState acoustic;
    acoustic.phase = GamePhase::Mission; acoustic.size = {900.f, 800.f};
    observer.position = {350.f, 300.f}; observer.facing = {1.f, 0.f}; acoustic.agents = {observer};
    acoustic.obstacles = {wall({390.f, 100.f}, {20.f, 400.f})}; hidden.position = {463.f, 311.f}; acoustic.guards = {hidden};
    const auto silentFog = capture(renderer, acoustic, output / "world-noise-fog-silent.png");
    acoustic.noises = {{{463.f, 311.f}, 180.f, GameplayConfig::NoiseDuration * 0.8f, observer.id, 0}};
    const auto ownNoise = capture(renderer, acoustic, output / "world-noise-own-through-fog.png");
    if (identical(silentFog, ownNoise)) throw std::runtime_error("La niebla oculta el ruido propio originado detrás de la pared");
    acoustic.guards.clear();
    const auto anonymousNoise = capture(renderer, acoustic, output / "world-noise-own-redacted.png");
    if (!identical(ownNoise, anonymousNoise)) throw std::runtime_error("El ruido percibido revela la entidad o el icono del enemigo oculto");
    acoustic.noises[0].actor = 2; acoustic.noises[0].radius = 300.f;
    const auto hostNoise = capture(renderer, acoustic, output / "world-noise-foreign-host.png");
    acoustic.visibility = Visibility::BuildGrid(acoustic, acoustic.agents[0]); acoustic.visibility.authoritative = true;
    acoustic.noises[0].actor = 0; acoustic.noises[0].position = {448.f, 288.f};
    const auto clientNoise = capture(renderer, acoustic, output / "world-noise-foreign-client.png");
    if (!identical(hostNoise, clientNoise)) throw std::runtime_error("La señal auditiva ajena usa una ubicación distinta entre anfitrión y cliente");
    metadata.push_back({{"file", "world-noise-own-through-fog.png"}, {"audibleOverFog", true}, {"hiddenEnemyOmitted", true}});
    metadata.push_back({{"file", "world-noise-foreign-client.png"}, {"approximatePosition", {448.f, 288.f}}, {"hostClientIdentical", true}, {"sourceIdentityAbsent", true}});
    tactical.agents = {fighter}; tactical.guards = {opponent}; tactical.guards[0].position = {460.f, 400.f}; tactical.guards[0].state = GuardState::Suspicion;
    sf::Image precedingIndicator;
    for (const auto progress : {0.25f, 0.5f, 0.9f, 1.f}) {
      tactical.guards[0].suspicion = progress; tactical.guards[0].detectedTime = progress == 1.f ? 0.5f : 0.f;
      tactical.guards[0].state = progress == 1.f ? GuardState::Alert : GuardState::Suspicion;
      const auto file = "world-guard-eye-" + std::to_string(static_cast<int>(progress * 100.f)) + ".png";
      const auto pixels = capture(renderer, tactical, output / file);
      if (progress > 0.25f && identical(precedingIndicator, pixels)) throw std::runtime_error("El ojo del guardia conserva un icono estático durante la detección");
      precedingIndicator = pixels;
      metadata.push_back({{"file", file}, {"guardSuspicion", progress}, {"segments", WorldRenderer::GuardDetectionFor(tactical.guards[0]).segments}, {"confirmedExclamation", progress == 1.f}});
    }
    sf::Vector2f iconExtent;
    for (const float steps : {-100.f, 100.f}) {
      renderer.ResetZoom(); renderer.AdjustZoom(steps);
      const auto file = steps < 0.f ? "world-guard-eye-zoom-min.png" : "world-guard-eye-zoom-max.png";
      const auto pixels = capture(renderer, tactical, output / file);
      const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(context.window.GetLogicalSize());
      const auto center = WorldRenderer::ProjectGround(tactical.guards[0].position, fighter.position, context.window.GetLogicalSize(), renderer.GetZoom()) -
          sf::Vector2f{0.f, PresentationConfig::ActorHeight * renderer.GetZoom() + PresentationConfig::IconOffset};
      sf::Vector2f minimum{10000.f, 10000.f}, maximum{-10000.f, -10000.f};
      for (int y = -18; y <= 18; ++y) for (int x = -20; x <= 30; ++x) {
        const auto point = (center + sf::Vector2f{static_cast<float>(x), static_cast<float>(y)}).componentWiseMul(scale);
        const auto color = pixels.getPixel(sf::Vector2u(point));
        if (color != PresentationConfig::Red && color != PresentationConfig::Cyan) continue;
        minimum.x = std::min(minimum.x, static_cast<float>(x)); minimum.y = std::min(minimum.y, static_cast<float>(y));
        maximum.x = std::max(maximum.x, static_cast<float>(x)); maximum.y = std::max(maximum.y, static_cast<float>(y));
      }
      const auto extent = maximum - minimum;
      if (extent.x <= 0.f || extent.y <= 0.f) throw std::runtime_error("El ojo y la exclamación desaparecen al cambiar de zoom");
      if (steps > 0.f && ((extent - iconExtent).length() > 4.f)) throw std::runtime_error("El ojo sobre el guardia aumenta de tamaño con el mundo y pierde legibilidad");
      iconExtent = extent;
      metadata.push_back({{"file", file}, {"fixedLogicalIconScale", PresentationConfig::GuardEyeScale}, {"capturedExtent", {extent.x, extent.y}}, {"zoom", renderer.GetZoom()}});
    }
    renderer.ResetZoom(); tactical.guards.clear();
    Interactable camera; camera.id = 80; camera.kind = ObjectKind::Camera; camera.position = {480.f, 360.f}; camera.name = "CÁMARA";
    camera.facing = {1.f, 0.f}; tactical.objects = {camera}; const auto sweepEast = capture(renderer, tactical, output / "world-camera-sweep-east.png");
    tactical.objects[0].facing = {0.f, 1.f}; const auto sweepSouth = capture(renderer, tactical, output / "world-camera-sweep-south.png");
    if (identical(sweepEast, sweepSouth)) throw std::runtime_error("El barrido real no cambia el cañón ni el cono perceptivo de la cámara");
    tactical.objects[0].camera.state = CameraState::Disabled; capture(renderer, tactical, output / "world-camera-disabled.png");
    metadata.push_back({{"file", "world-camera-sweep-south.png"}, {"perceptionFacingChangesCone", true}, {"range", camera.camera.range}, {"cosHalfAngle", camera.camera.cosHalfAngle}});
    Interactable explosive; explosive.id = 81; explosive.kind = ObjectKind::Explosive; explosive.position = {490.f, 430.f}; explosive.name = "EXPLOSIVO";
    tactical.objects = {explosive}; const auto armedHidden = capture(renderer, tactical, output / "world-trap-armed-hidden.png");
    tactical.objects.clear(); const auto withoutTrap = capture(renderer, tactical, output / "world-trap-redacted.png");
    if (!identical(armedHidden, withoutTrap)) throw std::runtime_error("El explosivo armado revela su cuerpo o marcador antes de detectarse");
    explosive.trap.state = TrapState::Detected; tactical.objects = {explosive}; capture(renderer, tactical, output / "world-trap-detected.png");
    tactical.objects[0].trap.state = TrapState::Disarmed; capture(renderer, tactical, output / "world-trap-disarmed.png");
    tactical.objects[0].trap.state = TrapState::Detonated; tactical.objects[0].trap.effectRemaining = DevicesConfig::ExplosionEffectDuration * 0.8f;
    tactical.obstacles = {wall({530.f, 320.f}, {20.f, 240.f})};
    const auto blast = capture(renderer, tactical, output / "world-trap-explosion.png");
    tactical.objects[0].trap.effectRemaining = 0.f; const auto afterBlast = capture(renderer, tactical, output / "world-trap-after-explosion.png");
    if (identical(blast, afterBlast)) throw std::runtime_error("La detonación no presenta un efecto temporal distinto de la trampa consumida");
    metadata.push_back({{"file", "world-trap-armed-hidden.png"}, {"identicalToRedacted", true}});
    metadata.push_back({{"file", "world-trap-explosion.png"}, {"temporaryBlast", true}, {"solidWallsClipEffect", true}});
    Simulation trainingSimulation; trainingSimulation.ResetBase();
    auto trainingWorld = trainingSimulation.GetState();
    if (!IsTraining(trainingWorld) || trainingWorld.objects.empty() || trainingWorld.guards.empty() ||
        !std::ranges::all_of(trainingWorld.guards, [](const Guard &guard) { return guard.trainingDummy; })) throw std::runtime_error("La sala de espera no conserva una arena real con dispositivos y dianas sin enemigos");
    renderer.ResetZoom();
    capture(renderer, trainingWorld, output / "world-training-cio-stations.png");
    const auto trainingTarget = std::ranges::find_if(trainingWorld.guards, [](const Guard &guard) { return guard.trainingDummy; });
    const auto targetIndex = static_cast<std::size_t>(trainingTarget - trainingWorld.guards.begin());
    auto &trainingAgent = trainingWorld.agents[0];
    trainingAgent.position = trainingTarget->position - sf::Vector2f{45.f, 0.f}; trainingAgent.facing = {1.f, 0.f};
    trainingWorld.visibility = {}; trainingWorld.guards[targetIndex].health = CombatConfig::MaxGuardHealth; trainingWorld.guards[targetIndex].trainingFeedback = 0.f;
    const auto pristineDummy = capture(renderer, trainingWorld, output / "world-training-arena-dummy-full.png");
    trainingWorld.guards[targetIndex].health = 37.f; trainingWorld.guards[targetIndex].trainingHits = 4;
    trainingWorld.guards[targetIndex].trainingFeedback = 1.f; trainingWorld.guards[targetIndex].staggerRemaining = CqcConfig::PunchStagger;
    const auto damagedDummy = capture(renderer, trainingWorld, output / "world-training-dummy-damaged.png");
    if (identical(pristineDummy, damagedDummy)) throw std::runtime_error("La diana no muestra daño, reacción o barra de vida al recibir impactos");
    trainingAgent.cqcHoldingGuard = trainingTarget->id; trainingAgent.actionPose = ActionPose::Choke;
    trainingAgent.actionPoseTime = GameplayConfig::AttackAnimationDuration * 0.5f; trainingAgent.cqcProgress = 1;
    trainingWorld.guards[targetIndex].heldBy = trainingAgent.id; trainingWorld.guards[targetIndex].cqcState = CqcState::KnockedDown;
    capture(renderer, trainingWorld, output / "world-training-dummy-choke.png");
    metadata.push_back({{"file", "world-training-arena-dummy-full.png"}, {"actualLobbyLayout", true}, {"worldSize", {trainingWorld.size.x, trainingWorld.size.y}}, {"enemyGuards", 0}, {"healthBarOnlyDummy", true}});
    metadata.push_back({{"file", "world-training-dummy-damaged.png"}, {"health", 37}, {"hits", 4}, {"distinctFromPristine", true}, {"distinctWoodAndFabricModel", true}});
    WorldState trainingPrivacy;
    trainingPrivacy.phase = GamePhase::Base; trainingPrivacy.mode = SimulationMode::Training; trainingPrivacy.size = {900.f, 800.f};
    fighter.position = {350.f, 300.f}; fighter.facing = {1.f, 0.f}; trainingPrivacy.agents = {fighter};
    trainingPrivacy.obstacles = {wall({390.f, 100.f}, {20.f, 400.f})};
    Guard hiddenDummy; hiddenDummy.id = 90; hiddenDummy.position = {490.f, 300.f}; hiddenDummy.trainingDummy = true;
    hiddenDummy.trainingFeedback = 1.f; hiddenDummy.health = 17.f; trainingPrivacy.guards = {hiddenDummy};
    camera.position = {490.f, 340.f}; camera.facing = {-1.f, 0.f}; trainingPrivacy.objects = {camera};
    const auto trainingHidden = capture(renderer, trainingPrivacy, output / "world-training-visible-through-wall.png");
    trainingPrivacy.guards.clear(); trainingPrivacy.objects.clear();
    const auto trainingRedacted = capture(renderer, trainingPrivacy, output / "world-training-visible-redacted.png");
    if (identical(trainingHidden, trainingRedacted)) throw std::runtime_error("La sala de espera mantiene ocultación de misiones en lugar de mostrar la arena sin niebla");
    Agent hiddenTrainee = fighter; hiddenTrainee.id = 2; hiddenTrainee.hidden = true; hiddenTrainee.position = hiddenDummy.position;
    trainingPrivacy.agents.push_back(hiddenTrainee);
    const auto hiddenTraineePixels = capture(renderer, trainingPrivacy, output / "world-training-concealed-agent.png");
    if (!identical(trainingRedacted, hiddenTraineePixels)) throw std::runtime_error("Eliminar la niebla de prácticas revela un agente que sigue escondido");
    trainingPrivacy.agents.pop_back();
    trainingPrivacy.obstacles.clear(); opponent.position = {390.f, 300.f}; opponent.health = CombatConfig::MaxGuardHealth;
    opponent.trainingDummy = false; opponent.trainingFeedback = 1.f; opponent.trainingHits = 4; opponent.state = GuardState::Patrol; trainingPrivacy.guards = {opponent};
    const auto healthyEnemy = capture(renderer, trainingPrivacy, output / "world-training-real-guard-full-no-bar.png");
    trainingPrivacy.guards[0].health = 17.f;
    const auto damagedEnemy = capture(renderer, trainingPrivacy, output / "world-training-real-guard-damaged-no-bar.png");
    if (!identical(healthyEnemy, damagedEnemy)) throw std::runtime_error("La barra exclusiva de dianas se dibuja sobre un guardia real");
    metadata.push_back({{"file", "world-training-visible-through-wall.png"}, {"trainingUsesFog", false}, {"identicalToRedacted", false}, {"concealedAgentRemainsHidden", true}});
    metadata.push_back({{"file", "world-training-real-guard-damaged-no-bar.png"}, {"guardHealthDoesNotChangeImage", true}, {"dummyBarNotAppliedToEnemies", true}});
    struct ClipFixture { const char *name; std::function<void(Agent &, float)> configure; std::size_t frames = 4; };
    const auto locomotion = [](SpriteConfig::Clip clip, Stance stance, bool moving, bool running = false) {
      return [clip, stance, moving, running](Agent &actor, float phase) {
        const auto &animation = SpriteConfig::Animations.at(static_cast<std::size_t>(clip));
        actor.stance = stance; actor.moving = moving; actor.running = running;
        actor.animationTime = phase * static_cast<float>(animation.count) / animation.fps;
      };
    };
    const auto action = [](ActionPose pose, Stance stance = Stance::Standing) {
      return [pose, stance](Agent &actor, float phase) {
        actor.stance = stance; actor.actionPose = pose;
        const bool transition = pose == ActionPose::CoverEnter || pose == ActionPose::HideExit;
        actor.actionPoseTime = (transition ? GameplayConfig::StanceTransitionDuration : GameplayConfig::AttackAnimationDuration) * (1.f - phase);
      };
    };
    const auto reload = [](Stance stance) { return [stance](Agent &actor, float phase) { actor.stance = stance; actor.reloadRemaining = WeaponConfig::ReloadDuration * (1.f - phase); }; };
    const auto aim = [](Stance stance) { return [stance](Agent &actor, float phase) { actor.stance = stance; actor.aiming = true; actor.animationTime = phase; }; };
    const std::vector<ClipFixture> clips{
      {"idle", locomotion(SpriteConfig::Clip::Idle, Stance::Standing, false), 8},
      {"walk", locomotion(SpriteConfig::Clip::Walk, Stance::Standing, true)},
      {"run", locomotion(SpriteConfig::Clip::Run, Stance::Standing, true, true)},
      {"crouch-walk", locomotion(SpriteConfig::Clip::CrouchWalk, Stance::Crouched, true)},
      {"crouch-run", locomotion(SpriteConfig::Clip::CrouchRun, Stance::Crouched, true, true)},
      {"crawl", locomotion(SpriteConfig::Clip::Crawl, Stance::Prone, true), 8},
      {"crouch-idle", locomotion(SpriteConfig::Clip::CrouchIdle, Stance::Crouched, false), 1},
      {"prone-idle", locomotion(SpriteConfig::Clip::ProneIdle, Stance::Prone, false), 1},
      {"crouch-down", [](Agent &actor, float phase) { actor.stance = Stance::Crouched; actor.transitionFrom = Stance::Standing; actor.transitionTime = GameplayConfig::StanceTransitionDuration * (1.f - phase); }, 2},
      {"crouch-up", [](Agent &actor, float phase) { actor.transitionFrom = Stance::Crouched; actor.transitionTime = GameplayConfig::StanceTransitionDuration * (1.f - phase); }, 2},
      {"aim-standing", aim(Stance::Standing)}, {"aim-crouched", aim(Stance::Crouched)}, {"aim-prone", aim(Stance::Prone)},
      {"shoot-standing", action(ActionPose::Shoot)}, {"shoot-crouched", action(ActionPose::Shoot, Stance::Crouched)}, {"shoot-prone", action(ActionPose::Shoot, Stance::Prone)},
      {"reload-standing", reload(Stance::Standing)}, {"reload-crouched", reload(Stance::Crouched)}, {"reload-prone", reload(Stance::Prone)},
      {"punch", [](Agent &actor, float phase) { actor.cqcNextKick = true; actor.cqcAttackTime = CqcConfig::PunchCooldown * (1.f - phase); }},
      {"kick", [](Agent &actor, float phase) { actor.cqcNextKick = false; actor.cqcAttackTime = CqcConfig::KickCooldown * (1.f - phase); }},
      {"dive", [](Agent &actor, float phase) {
        actor.maneuver.kind = ManeuverKind::Dive; actor.maneuver.time = ManeuverConfig::Duration(ManeuverKind::Dive) * phase;
        const float time = actor.maneuver.time;
        actor.maneuver.phase = time < ManeuverConfig::DiveStart ? ManeuverPhase::Start : time < ManeuverConfig::DiveStart + ManeuverConfig::DiveTravel ? ManeuverPhase::Travel :
            time < ManeuverConfig::DiveStart + ManeuverConfig::DiveTravel + ManeuverConfig::DiveLand ? ManeuverPhase::Land : ManeuverPhase::Recover;
        actor.stance = actor.maneuver.phase == ManeuverPhase::Start ? Stance::Standing : Stance::Prone;
      }},
      {"roll", [](Agent &actor, float phase) {
        actor.stance = Stance::Crouched; actor.maneuver.kind = ManeuverKind::Roll; actor.maneuver.time = ManeuverConfig::Duration(ManeuverKind::Roll) * phase;
        const float time = actor.maneuver.time;
        actor.maneuver.phase = time < ManeuverConfig::RollStart ? ManeuverPhase::Start : time < ManeuverConfig::RollStart + ManeuverConfig::RollTravel ? ManeuverPhase::Travel : ManeuverPhase::Recover;
      }},
      {"lie-down", [](Agent &actor, float phase) { actor.stance = Stance::Prone; actor.transitionFrom = Stance::Standing; actor.transitionTime = GameplayConfig::StanceTransitionDuration * (1.f - phase); }},
      {"stand-up", [](Agent &actor, float phase) { actor.transitionFrom = Stance::Prone; actor.transitionTime = GameplayConfig::StanceTransitionDuration * (1.f - phase); }},
      {"throw", action(ActionPose::ThrowDecoy)}, {"cover-enter", action(ActionPose::CoverEnter)}, {"hide-exit", action(ActionPose::HideExit)},
      {"interact", [](Agent &actor, float phase) {
        const auto &animation = SpriteConfig::Animations.at(static_cast<std::size_t>(SpriteConfig::Clip::Interact));
        actor.puzzle.target = 90; actor.puzzle.kind = PuzzleKind::Door; actor.puzzle.family = PuzzleFamily::Arrows; actor.puzzle.length = 3; actor.puzzle.sequence = {1, 2, 3};
        actor.actionPose = ActionPose::Interact; actor.actionPoseTime = GameplayConfig::StanceTransitionDuration;
        actor.animationTime = phase * static_cast<float>(animation.count) / animation.fps;
      }, 8},
      {"fall", [](Agent &actor, float phase) { actor.incapacitated = true; actor.actionPose = ActionPose::Fall; actor.actionPoseTime = GameplayConfig::AttackAnimationDuration * (1.f - phase); }}
    };
    const std::array<float, 4> clipPhases{{0.01f, 0.31f, 0.65f, 0.94f}};
    const auto facingFor = [](unsigned int direction) {
      const float angle = static_cast<float>(direction) * std::numbers::pi_v<float> * 0.25f;
      const float horizontal = std::sin(angle) / PresentationConfig::GroundProjection.x, vertical = std::cos(angle) / PresentationConfig::GroundProjection.y;
      return CombatGeometry::Unit({horizontal + vertical, vertical - horizontal});
    };
    // El escenario cubre el viewport completo sin dibujar cuatro veces más suelo fuera de cámara.
    WorldState matrix; matrix.phase = GamePhase::Base; matrix.size = {1000.f, 1000.f};
    Agent matrixObserver; matrixObserver.id = 1; matrixObserver.position = {500.f, 500.f}; matrixObserver.hidden = true;
    std::array<sf::Image, 3> bareFloors;
    matrix.agents = {matrixObserver};
    for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
      renderer.ResetZoom(); if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
      bareFloors[zoomIndex] = capture(renderer, matrix, output / ("world-shadow-floor-" + std::string(zoomNames[zoomIndex]) + ".png"));
    }
    for (const auto &clip : clips) for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
      renderer.ResetZoom(); if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
      std::array<std::set<std::size_t>, 8> framesByDirection;
      sf::Image precedingPhase, initialPhase;
      for (std::size_t phaseIndex = 0; phaseIndex < clip.frames; ++phaseIndex) {
        const float phase = clip.frames == 8 ? (static_cast<float>(phaseIndex) + .1f) / 8.f : clip.frames == 4 ? clipPhases[phaseIndex] : clip.frames == 1 ? 0.f : phaseIndex == 0 ? 0.01f : 0.94f;
        matrix.agents = {matrixObserver};
        nlohmann::json selected = nlohmann::json::array();
        for (unsigned int direction = 0; direction < 8; ++direction) {
          Agent actor; actor.id = direction + 2; actor.facing = facingFor(direction);
          const auto logical = context.window.GetLogicalSize();
          const sf::Vector2f feet{logical.x * (0.15f + static_cast<float>(direction % 4) * 0.2f), logical.y * (0.4f + static_cast<float>(direction / 4) * 0.33f)};
          actor.position = WorldRenderer::UnprojectGround(feet, matrixObserver.position, logical, renderer.GetZoom());
          clip.configure(actor, phase); actor.maneuver.direction = actor.facing;
          const auto frame = WorldRenderer::SelectActorSprite(actor);
          if (!frame.operative || frame.index >= operativeBitmapFrames.size() || WorldRenderer::SpriteDirection(actor.facing) != direction)
            throw std::runtime_error("La matriz de ocho direcciones no selecciona una celda operativa válida");
          framesByDirection[direction].insert(frame.index);
          const auto ground = WorldRenderer::ProjectGround(actor.position, matrixObserver.position, logical);
          const auto placement = WorldRenderer::PlaceSprite(ground, actor.facing, actor.stance, false, frame, operativeBitmapFrames[frame.index].rectangle.size);
          const auto shadow = placement.Map(operativeSupports[frame.index].anchor);
          const auto focus = WorldRenderer::ProjectGround(matrixObserver.position, matrixObserver.position, logical);
          const auto screenShadow = focus + (shadow - focus) * renderer.GetZoom();
          selected.push_back({{"direction", direction}, {"frame", frame.index}, {"stance", static_cast<int>(actor.stance)}, {"ground", {actor.position.x, actor.position.y}},
            {"shadowAnchor", {screenShadow.x, screenShadow.y}}, {"shadowGroundHeight", shadow.y - ground.y}});
          matrix.agents.push_back(actor);
        }
        const auto name = "world-clips-" + std::string(clip.name) + "-" + zoomNames[zoomIndex] + "-phase-" + std::to_string(phaseIndex) + ".png";
        const auto pixels = capture(renderer, matrix, output / name);
        for (auto &actor : selected) {
          const auto logical = context.window.GetLogicalSize();
          const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(logical);
          const sf::Vector2f shadow{actor["shadowAnchor"][0].get<float>(), actor["shadowAnchor"][1].get<float>()};
          unsigned int shadowPixels = 0;
          for (float y = 2.f; y <= 4.f; y += 1.f) for (float x = -4.f; x <= 4.f; x += 1.f) {
            const auto point = sf::Vector2u((shadow + sf::Vector2f{x, y} * renderer.GetZoom()).componentWiseMul(scale));
            if (point.x < pixels.getSize().x && point.y < pixels.getSize().y && pixels.getPixel(point) != bareFloors[zoomIndex].getPixel(point)) ++shadowPixels;
          }
          if (!shadowPixels || actor["shadowGroundHeight"].get<float>() != 0.f) throw std::runtime_error("La sombra pierde el apoyo real o flota fuera del suelo en " + name);
          actor["verifiedShadowPixels"] = shadowPixels;
        }
        if (phaseIndex > 0 && identical(precedingPhase, pixels)) throw std::runtime_error("La matriz conserva el mismo bitmap en dos fases de " + std::string(clip.name));
        if (clip.frames == 8 && phaseIndex == 0) initialPhase = pixels;
        precedingPhase = pixels;
        metadata.push_back({{"file", name}, {"clip", clip.name}, {"directions", 8}, {"phase", phase}, {"expectedDistinctFrames", clip.frames}, {"zoom", renderer.GetZoom()},
            {"nativeSpriteScale", PresentationConfig::ActorHeight / SpriteConfig::StandingPixels}, {"renderedSpriteScale", renderer.GetZoom() * PresentationConfig::ActorHeight / SpriteConfig::StandingPixels},
            {"consecutiveLoopPhases", clip.frames == 8}, {"loopClosureVerified", clip.frames == 8}, {"selectedFrames", selected}});
      }
      if (clip.frames == 8 && identical(precedingPhase, initialPhase)) throw std::runtime_error("El cierre 7→0 no avanza el ciclo consecutivo de " + std::string(clip.name));
      for (const auto &frames : framesByDirection) if (frames.size() != clip.frames) throw std::runtime_error("Una dirección no recorre las celdas PNG previstas de " + std::string(clip.name));
    }
    for (std::size_t zoomIndex = 0; zoomIndex < zoomNames.size(); ++zoomIndex) {
      renderer.ResetZoom(); if (zoomIndex != 1) renderer.AdjustZoom(zoomIndex == 0 ? -100.f : 100.f);
      for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
        WorldState clean; clean.phase = GamePhase::Base; clean.size = matrix.size;
        Agent local; local.id = 1; local.name = "AGENTE"; local.position = matrixObserver.position; local.stance = stance;
        local.facing = facingFor(static_cast<unsigned int>(stance) * 3); local.moving = true; local.animationTime = 0.4f;
        clean.agents = {local};
        const auto name = "world-clean-local-" + std::to_string(static_cast<int>(stance)) + "-" + zoomNames[zoomIndex] + ".png";
        const auto pixels = capture(renderer, clean, output / name);
        renderer.SetNamesVisible(true);
        const auto named = capture(renderer, clean, output / ("world-tab-local-" + std::to_string(static_cast<int>(stance)) + "-" + zoomNames[zoomIndex] + ".png"));
        renderer.SetNamesVisible(false);
        const auto logical = context.window.GetLogicalSize();
        const auto feet = WorldRenderer::ProjectGround(local.position, local.position, logical, renderer.GetZoom());
        const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(logical);
        unsigned int selectionPixels = 0, statusPixels = 0;
        for (float y = 6.f; y <= 15.f; y += 0.5f) for (float x = -25.f; x <= 25.f; x += 0.5f) {
          const auto point = sf::Vector2u((feet + sf::Vector2f{x, y} * renderer.GetZoom()).componentWiseMul(scale));
          if (point.x < pixels.getSize().x && point.y < pixels.getSize().y && pixels.getPixel(point) == PresentationConfig::Green) ++selectionPixels;
        }
        for (float y = -82.f; y <= -44.f; y += 0.5f) for (float x = -50.f; x <= 50.f; x += 0.5f) {
          const auto point = sf::Vector2u((feet + sf::Vector2f{x, y} * renderer.GetZoom()).componentWiseMul(scale));
          if (point.x < named.getSize().x && point.y < named.getSize().y && named.getPixel(point) == PresentationConfig::Green) ++statusPixels;
        }
        if (selectionPixels != 0) throw std::runtime_error("Reaparecen los cuadrados verdes de selección bajo el agente local en " + name);
        if (statusPixels == 0) throw std::runtime_error("TAB no conserva el nombre verde del agente local en " + name);
        metadata.push_back({{"file", name}, {"localSelectionPixels", selectionPixels}, {"tabNamePixels", statusPixels}, {"stance", static_cast<int>(stance)}, {"zoom", renderer.GetZoom()}});
      }
    }
    renderer.ResetZoom();
    for (unsigned int direction = 0; direction < 8; ++direction) for (std::size_t phaseIndex = 0; phaseIndex < clipPhases.size(); ++phaseIndex) {
      WorldState entering; entering.phase = GamePhase::Base; entering.size = {900.f, 800.f};
      Interactable closet; closet.id = 71; closet.kind = ObjectKind::Hideout; closet.hideoutKind = HideoutKind::Closet; closet.position = {450.f, 400.f}; closet.facing = {1.f, 0.f};
      Agent actor; actor.id = 1; actor.position = closet.position; actor.facing = facingFor(direction); actor.hidden = true; actor.hideoutId = closet.id;
      actor.hideoutExit = WorldGeometry::HideoutFrontPosition(closet, GameplayConfig::AgentRadius); actor.actionPose = ActionPose::HideEnter;
      actor.actionPoseTime = GameplayConfig::StanceTransitionDuration * (1.f - clipPhases[phaseIndex]); closet.occupantAgent = actor.id; closet.occupied = true;
      entering.agents = {actor}; entering.objects = {closet};
      const auto name = "world-clips-hide-enter-direction-" + std::to_string(direction) + "-phase-" + std::to_string(phaseIndex) + ".png";
      const auto pixels = capture(renderer, entering, output / name);
      entering.agents[0].actionPoseTime = 0.f;
      const auto absent = capture(renderer, entering, output / ("world-clips-hide-enter-concealed-" + std::to_string(direction) + "-" + std::to_string(phaseIndex) + ".png"));
      if (identical(pixels, absent)) throw std::runtime_error("La entrada real del agente local se oculta antes de terminar la transición del armario");
      metadata.push_back({{"file", name}, {"clip", "hide-enter"}, {"direction", direction}, {"phase", clipPhases[phaseIndex]}, {"localHiddenTransitionVisible", true}, {"zoom", renderer.GetZoom()}});
    }
    renderer.ResetZoom();
    std::ofstream(output / "world-fixtures.json") << metadata.dump(2);
    return metadata;
  }
}
