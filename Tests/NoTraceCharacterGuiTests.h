#pragma once

#include "Core/EngineContext.h"
#include "NoTraceWorldRenderer.h"
#include "Config/NoTraceAudioConfig.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string_view>

namespace EngineTests {
  template <typename Capture>
  nlohmann::json RunCharacterRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    WorldRenderer renderer(context);
    WorldState world; world.phase = GamePhase::Base; world.size = {1400.f, 1400.f};
    Agent observer; observer.id = 1; observer.position = {700.f, 700.f}; observer.hidden = true;
    struct Case { const char *name; std::function<void(Agent &, float, WorldState &)> configure; bool animated = true, repeatedSelection = false, allowIdenticalPixels = false; };
    const auto pose = [](ActionPose action, bool dead = false) {
      return [action, dead](Agent &actor, float phase, WorldState &) {
        actor.actionPose = action; actor.actionPoseTime = GameplayConfig::AttackAnimationDuration * (1.f - phase); actor.alive = !dead;
      };
    };
    const auto gesture = [](ActionPose pose, Stance stance) {
      return [pose, stance](Agent &actor, float phase, WorldState &) {
        actor.stance = stance;
        actor.actionPose = pose; actor.actionPoseTime = WeaponConfig::EquipmentGestureDuration * (1.f - phase);
      };
    };
    const auto traversal = [](TraversalMode mode) {
      return [mode](Agent &actor, float phase, WorldState &) { actor.traversal.mode = mode; actor.traversal.progress = phase; actor.animationTime = phase * (mode == TraversalMode::Hang ? 2.f : 1.f); };
    };
    const auto coverEntry = [](Stance stance) {
      return [stance](Agent &actor, float phase, WorldState &) {
        actor.stance = stance; actor.cover.active = true; actor.cover.normal = actor.facing;
        actor.actionPose = ActionPose::CoverEnter; actor.actionPoseTime = GameplayConfig::StanceTransitionDuration * (1.f - phase);
      };
    };
    const auto revive = [](Stance stance) {
      return [stance](Agent &actor, float phase, WorldState &) {
        actor.stance = stance; actor.transitionFrom = stance; actor.transitionTime = 0.f;
        actor.actionPose = ActionPose::Revive; actor.actionPoseTime = GameplayConfig::StanceTransitionDuration * (1.f - phase - .0125f);
      };
    };
    const auto gripPair = [](float distance, bool choke) {
      return [distance, choke](Agent &actor, float phase, WorldState &state) {
        Guard victim; victim.id = 100 + actor.id; victim.position = actor.position + actor.facing * distance; victim.facing = actor.facing;
        victim.heldBy = actor.id; victim.cqcState = CqcState::Grabbed; actor.cqcHoldingGuard = victim.id;
        actor.animationTime = phase * (8.f / 6.f); actor.actionPose = choke ? ActionPose::Choke : ActionPose::Grab;
        actor.actionPoseTime = GameplayConfig::AttackAnimationDuration * (1.f - phase); state.guards.push_back(victim);
      };
    };
    const auto loop = [](SpriteConfig::Clip clip, Stance stance, bool moving = false, bool running = false, bool aiming = false) {
      return [clip, stance, moving, running, aiming](Agent &actor, float phase, WorldState &) {
        actor.stance = stance; actor.moving = moving; actor.running = running; actor.aiming = aiming;
        actor.animationTime = (phase * 8.f + .125f) / SpriteConfig::Animations[static_cast<std::size_t>(clip)].fps;
      };
    };
    const auto stanceTransition = [](Stance from, Stance to) {
      return [from, to](Agent &actor, float phase, WorldState &) {
        actor.stance = to; actor.transitionFrom = from;
        actor.transitionTime = GameplayConfig::StanceTransitionDuration * (1.f - phase - .0125f);
      };
    };
    const std::vector<Case> cases{
      {"idle-control", loop(SpriteConfig::Clip::Idle, Stance::Standing), true, false, true},
      {"walk-control", loop(SpriteConfig::Clip::Walk, Stance::Standing, true)},
      {"run-control", loop(SpriteConfig::Clip::Run, Stance::Standing, true, true)},
      {"crouch-walk-control", loop(SpriteConfig::Clip::CrouchWalk, Stance::Crouched, true)},
      {"prone-idle", loop(SpriteConfig::Clip::ProneIdle, Stance::Prone), true, false, true},
      {"prone-crawl", loop(SpriteConfig::Clip::Crawl, Stance::Prone, true)},
      {"prone-aim", loop(SpriteConfig::Clip::Aim, Stance::Prone, false, false, true), true, false, true},
      {"prone-shoot", [](Agent &actor, float phase, WorldState &) {
        actor.stance = Stance::Prone; actor.aiming = true;
        actor.actionPose = ActionPose::Shoot; actor.actionPoseTime = GameplayConfig::AttackAnimationDuration * (1.f - phase - .0125f);
      }, true, false, true},
      {"prone-reload", [](Agent &actor, float phase, WorldState &) {
        actor.stance = Stance::Prone; actor.aiming = true;
        actor.reloadRemaining = WeaponConfig::ReloadDuration * (1.f - phase - .0125f);
      }, true, false, true},
      {"prone-throw", [](Agent &actor, float phase, WorldState &) {
        actor.stance = Stance::Prone; actor.actionPose = ActionPose::ThrowDecoy;
        actor.actionPoseTime = GameplayConfig::AttackAnimationDuration * (1.f - phase - .0125f);
      }, true, false, true},
      {"lie-down", stanceTransition(Stance::Standing, Stance::Prone)},
      {"stand-up", stanceTransition(Stance::Prone, Stance::Standing)},
      {"unconscious-agent", [](Agent &actor, float phase, WorldState &) {
        actor.incapacitated = true;
        actor.animationTime = (phase * 8.f + .125f) / SpriteConfig::Animations[static_cast<std::size_t>(SpriteConfig::Clip::Unconscious)].fps;
      }, true, false, true},
      {"unconscious-guard", [](Agent &actor, float, WorldState &state) {
        actor.hidden = true;
        Guard guard; guard.id = 100 + actor.id; guard.position = actor.position; guard.facing = actor.facing;
        guard.state = GuardState::Incapacitated; guard.incapacityRemaining = GameplayConfig::GuardIncapacityDuration * .5f;
        state.guards.push_back(guard);
      }, false},
      {"fall-guard", [](Agent &actor, float phase, WorldState &state) {
        actor.hidden = true;
        Guard guard; guard.id = 100 + actor.id; guard.position = actor.position; guard.facing = actor.facing;
        guard.state = GuardState::Incapacitated;
        guard.incapacityRemaining = GameplayConfig::GuardIncapacityDuration - PresentationConfig::CqcFallDuration * (phase + .0125f);
        state.guards.push_back(guard);
      }},
      {"push", pose(ActionPose::Push)}, {"death", pose(ActionPose::Fall, true)},
      {"drag", [](Agent &actor, float phase, WorldState &) { actor.draggingGuard = 0; actor.moving = true; actor.animationTime = phase; }},
      {"held-victim", [](Agent &actor, float phase, WorldState &) { actor.cqcHeldBy = 1; actor.animationTime = phase * 2.f; }},
      {"grip-pair28", gripPair(28.f, false)}, {"choke-pair28", gripPair(28.f, true)},
      {"cover-enter-standing", coverEntry(Stance::Standing)}, {"cover-enter-crouched", coverEntry(Stance::Crouched)},
      {"revive-standing", revive(Stance::Standing)}, {"revive-crouched", revive(Stance::Crouched)},
      {"revive-prone", revive(Stance::Prone)},
      {"cqc-release-recover", [](Agent &actor, float phase, WorldState &) {
        actor.actionPose = ActionPose::Recover; actor.actionPoseTime = CqcConfig::PunchRecovery * (1.f - phase);
      }, false},
      {"guard-wake", [](Agent &actor, float phase, WorldState &state) {
        actor.hidden = true;
        Guard guard; guard.id = 100 + actor.id; guard.position = actor.position; guard.facing = actor.facing;
        guard.state = GuardState::Search; guard.wakeRemaining = GameplayConfig::StanceTransitionDuration * (1.f - phase - .0125f);
        state.guards.push_back(guard);
      }},
      {"suppressor-standing", gesture(ActionPose::SuppressorAttach, Stance::Standing)},
      {"suppressor-crouched", gesture(ActionPose::SuppressorAttach, Stance::Crouched)},
      {"suppressor-prone", gesture(ActionPose::SuppressorAttach, Stance::Prone)},
      {"switch-standing", gesture(ActionPose::WeaponSwitch, Stance::Standing)},
      {"switch-prone", gesture(ActionPose::WeaponSwitch, Stance::Prone)},
      {"traversal-attach", traversal(TraversalMode::Attach)}, {"traversal-hang", traversal(TraversalMode::Hang)},
      {"traversal-shimmy", traversal(TraversalMode::Shimmy)}, {"traversal-climb", traversal(TraversalMode::Climb)},
      {"traversal-transfer", traversal(TraversalMode::Transfer)}, {"traversal-drop", traversal(TraversalMode::Drop)},
      {"nonlethal-prone", [](Agent &actor, float phase, WorldState &) {
        actor.stance = Stance::Prone; actor.aiming = true; actor.selectedWeapon = WeaponKind::NonLethal;
        actor.actionPose = ActionPose::Shoot; actor.actionPoseTime = GameplayConfig::AttackAnimationDuration * (1.f - phase);
      }}
    };
    int checks = 0;
    nlohmann::json evidence = nlohmann::json::array();
    const auto different = [](const sf::Image &first, const sf::Image &last) {
      const auto bytes = static_cast<std::size_t>(first.getSize().x) * first.getSize().y * 4;
      return first.getSize() != last.getSize() || !std::equal(first.getPixelsPtr(), first.getPixelsPtr() + bytes, last.getPixelsPtr());
    };
    world.agents = {observer};
    const auto empty = capture(renderer, world, output / "characters-empty-baseline.png", false);
    for (const auto &scenario : cases) {
      sf::Image preceding, initial;
      nlohmann::json precedingPoses;
      for (std::size_t phaseIndex = 0; phaseIndex < 8; ++phaseIndex) {
        const float phase = static_cast<float>(phaseIndex) / 8.f;
        world.agents = {observer}; world.sounds.clear(); world.guards.clear();
        nlohmann::json poses = nlohmann::json::array();
        for (std::size_t direction = 0; direction < 8; ++direction) {
          Agent actor; actor.id = static_cast<PlayerId>(direction + 2);
          const float angle = static_cast<float>(direction) * std::numbers::pi_v<float> * .25f;
          actor.facing = CombatGeometry::Unit({std::sin(angle) / PresentationConfig::GroundProjection.x + std::cos(angle) / PresentationConfig::GroundProjection.y,
              -std::sin(angle) / PresentationConfig::GroundProjection.x + std::cos(angle) / PresentationConfig::GroundProjection.y});
          actor.weaponAim = actor.facing;
          const auto logical = context.window.GetLogicalSize();
          const sf::Vector2f screen{logical.x * (.14f + static_cast<float>(direction % 4) * .24f), logical.y * (.56f + static_cast<float>(direction / 4) * .30f)};
          actor.position = WorldRenderer::UnprojectGround(screen, observer.position, logical, renderer.GetZoom());
          scenario.configure(actor, phase, world);
          const bool guardCase = std::string_view(scenario.name) == "guard-wake" || std::string_view(scenario.name) == "unconscious-guard" || std::string_view(scenario.name) == "fall-guard";
          const auto selected = guardCase ? WorldRenderer::SelectGuardSprite(world.guards.back(), world) : WorldRenderer::SelectBodySprite(actor);
          if (!renderer.CharacterSprites().Metadata(selected.index).valid) throw std::runtime_error("El renderer seleccionó pose no autorada");
          const bool weaponLayer = WorldRenderer::HasWeaponLayer(actor);
          const auto upper = weaponLayer ? WorldRenderer::SelectWeaponSprite(actor) : selected;
          if (!renderer.CharacterSprites().Metadata(upper.index).valid) throw std::runtime_error("El renderer seleccionó una capa superior no autorada");
          poses.push_back({{"direction", direction}, {"frame", selected.index}, {"weaponCandidateFrame", weaponLayer ? nlohmann::json(upper.index) : nlohmann::json(nullptr)}, {"position", {actor.position.x, actor.position.y}}, {"stance", static_cast<int>(actor.stance)}});
          world.agents.push_back(actor);
        }
        const auto file = "characters-" + std::string(scenario.name) + "-phase-" + std::to_string(phaseIndex) + ".png";
        const auto before = world.agents;
        const auto beforeGuards = world.guards;
        if (phaseIndex == 0 && (std::string_view(scenario.name).starts_with("revive-") || std::string_view(scenario.name) == "guard-wake")) {
          auto prior = world;
          for (auto &actor : prior.agents) if (actor.id != observer.id) { actor.incapacitated = true; actor.transitionTime = 0.f; actor.actionPose = ActionPose::None; actor.actionPoseTime = 0.f; }
          for (auto &guard : prior.guards) { guard.state = GuardState::Incapacitated; guard.wakeRemaining = 0.f; guard.incapacityRemaining = 10.f; }
          capture(renderer, prior, output / ("characters-" + std::string(scenario.name) + "-prior.png"), false);
        }
        const auto pixels = capture(renderer, world, output / file, false);
        ++checks;
        if (std::string_view(scenario.name) == "cover-enter-crouched" || std::string_view(scenario.name) == "revive-crouched" || std::string_view(scenario.name) == "revive-prone") {
          const auto logical = context.window.GetLogicalSize();
          const auto pixelScale = sf::Vector2f(pixels.getSize()).componentWiseDiv(logical);
          const bool prone = std::string_view(scenario.name) == "revive-prone";
          const bool recovery = std::string_view(scenario.name).starts_with("revive-");
          for (std::size_t actor = 1; actor < world.agents.size(); ++actor) {
            const auto feet = WorldRenderer::ProjectGround(world.agents[actor].position, observer.position, logical, renderer.GetZoom());
            const float halfWidth = prone ? 58.f : 36.f;
            const auto top = (feet - sf::Vector2f{halfWidth, CombatConfig::StandingHeight + PresentationConfig::PixelSize} * renderer.GetZoom()).componentWiseMul(pixelScale);
            const auto bottom = (feet + sf::Vector2f{halfWidth * renderer.GetZoom(), -2.f * renderer.GetZoom()}).componentWiseMul(pixelScale);
            const auto frameIndex = poses[actor - 1]["frame"].template get<std::size_t>();
            const auto &metadata = renderer.CharacterSprites().Metadata(frameIndex);
            const float sourceTop = (metadata.art[static_cast<std::size_t>(CharacterArt::Layer::Full)].offset.y - metadata.anchor.y) * CharacterArt::Scale();
            // La inclinación desplaza la huella proyectada; Recovery conserva la escala autorada y su cota3D validada aparte.
            const float highest = (feet.y + (recovery ? sourceTop - PresentationConfig::PixelSize : -(CombatConfig::CrouchedHeight + PresentationConfig::PixelSize)) * renderer.GetZoom()) * pixelScale.y;
            unsigned int changed = 0;
            for (int y = std::max(0, static_cast<int>(top.y)); y < std::min(static_cast<int>(pixels.getSize().y), static_cast<int>(bottom.y)); ++y)
              for (int x = std::max(0, static_cast<int>(top.x)); x < std::min(static_cast<int>(pixels.getSize().x), static_cast<int>(bottom.x)); ++x) {
                const sf::Vector2u point{static_cast<unsigned int>(x), static_cast<unsigned int>(y)};
                if (pixels.getPixel(point) == empty.getPixel(point)) continue;
                ++changed;
                if (static_cast<float>(y) < highest) throw std::runtime_error("La pose agachada o tendida excede su volumen autorado");
              }
            if (!changed) throw std::runtime_error("La pose agachada o tendida pierde el personaje visible");
            ++checks;
          }
        }
        for (std::size_t actor = 0; actor < before.size(); ++actor) if (before[actor].position != world.agents[actor].position || before[actor].elevation != world.agents[actor].elevation)
          throw std::runtime_error("La presentación de personajes altera la posición física");
        for (std::size_t guard = 0; guard < beforeGuards.size(); ++guard) if (beforeGuards[guard].position != world.guards[guard].position || beforeGuards[guard].elevation != world.guards[guard].elevation)
          throw std::runtime_error("La incorporación dibujada altera la posición física del guardia");
        if (phaseIndex > 0) {
          ++checks;
          const bool changed = different(preceding, pixels);
          if (scenario.animated ? !changed && !scenario.allowIdenticalPixels && (!scenario.repeatedSelection || precedingPoses != poses) : changed)
            throw std::runtime_error("La secuencia runtime no corresponde a su evolución esperada: " + std::string(scenario.name));
        }
        if (phaseIndex == 0) initial = pixels;
        preceding = pixels;
        precedingPoses = poses;
        evidence.push_back({{"file", file}, {"case", scenario.name}, {"phase", phase}, {"directions", 8}, {"poses", poses}, {"animated", scenario.animated}, {"allowIdenticalPixels", scenario.allowIdenticalPixels}, {"zoom", renderer.GetZoom()}});
      }
      if (scenario.repeatedSelection && !different(initial, preceding)) throw std::runtime_error("La incorporación hacia agachado no avanza entre inicio y destino");
    }
    nlohmann::json report{{"status", "PASS"}, {"checks", checks}, {"cases", cases.size()}, {"captures", evidence.size()}, {"atlasPoses", SpriteConfig::AuthoredFrameCount},
      {"scope", "Render real de estados autoritativos preparados; no sustituye prueba de controles ni partida LAN manual"},
      {"selectionScope", "frame registra SelectBodySprite o SelectGuardSprite; weaponCandidateFrame registra SelectWeaponSprite sólo si HasWeaponLayer; no enumera overrides privados de gestos/equipamiento"}, {"evidence", evidence}};
    std::ofstream file(output / "character-fixtures.json"); file << report.dump(2);
    return report;
  }
}
