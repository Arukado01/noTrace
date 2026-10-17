#include "NoTraceWorldRenderer.h"

#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTracePresentationConfig.h"
#include "Core/EngineContext.h"
#include "Core/EngineConfig.h"
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>

namespace NoTrace {
  namespace {
    using namespace PresentationConfig;

    sf::Color Shade(sf::Color color, float factor) {
      return {static_cast<std::uint8_t>(std::clamp(color.r * factor, 0.f, 255.f)),
              static_cast<std::uint8_t>(std::clamp(color.g * factor, 0.f, 255.f)),
              static_cast<std::uint8_t>(std::clamp(color.b * factor, 0.f, 255.f)), color.a};
    }

    sf::Vector2f RayEnd(sf::Vector2f origin, sf::Vector2f delta, const WorldState &world) {
      float nearest = 1.f;
      const auto clip = [&](sf::FloatRect bounds) {
        float entry = 0.f;
        float exit = 1.f;
        const auto axis = [&](float start, float direction, float minimum, float maximum) {
          if (std::abs(direction) < 0.0001f) return start >= minimum && start <= maximum;
          float first = (minimum - start) / direction;
          float second = (maximum - start) / direction;
          if (first > second) std::swap(first, second);
          entry = std::max(entry, first);
          exit = std::min(exit, second);
          return entry <= exit;
        };
        if (axis(origin.x, delta.x, bounds.position.x, bounds.position.x + bounds.size.x) &&
            axis(origin.y, delta.y, bounds.position.y, bounds.position.y + bounds.size.y) && exit > 0.f) {
          nearest = std::min(nearest, std::max(0.f, entry));
        }
      };
      for (const auto &obstacle : world.obstacles) {
        if (obstacle.cover == CoverKind::High) clip(obstacle.bounds);
      }
      for (const auto &object : world.objects) {
        if (object.kind == ObjectKind::Door && !object.open) {
          clip({object.position - sf::Vector2f{12.f, 30.f}, {24.f, 60.f}});
        }
      }
      return origin + delta * nearest;
    }
  }

  WorldRenderer::WorldRenderer(EngineContext &context)
      : ctx_(context), font_(context.resources.FetchFont("Relidux.otf")) {}

  sf::Vector2f WorldRenderer::CameraGround(const WorldState &world, PlayerId localPlayer) const {
    const auto player = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) {
      return agent.id == localPlayer;
    });
    return player == world.agents.end() ? world.size * 0.5f : player->position;
  }

  sf::Vector2f WorldRenderer::GroundToScreen(sf::Vector2f ground, sf::Vector2f camera) const {
    return ProjectGround(ground, camera, gConfig.windowSize);
  }

  sf::Vector2f WorldRenderer::ScreenToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const {
    return UnprojectGround(logical, CameraGround(world, localPlayer), gConfig.windowSize);
  }

  void WorldRenderer::Polygon(std::initializer_list<sf::Vector2f> points, sf::Color color) const {
    sf::ConvexShape shape(points.size());
    std::size_t index = 0;
    for (const auto point : points) shape.setPoint(index++, SnapToPixels(point));
    shape.setFillColor(color);
    ctx_.renderer.Draw(shape);
  }

  void WorldRenderer::PixelRect(sf::Vector2f position, sf::Vector2f size, sf::Color color) const {
    sf::RectangleShape rectangle(SnapToPixels(size));
    rectangle.setPosition(SnapToPixels(position));
    rectangle.setFillColor(color);
    ctx_.renderer.Draw(rectangle);
  }

  void WorldRenderer::PixelLine(sf::Vector2f from, sf::Vector2f to, sf::Color color, float thickness) const {
    from = SnapToPixels(from);
    to = SnapToPixels(to);
    const auto delta = to - from;
    const int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(delta.x), std::abs(delta.y)) / PresentationConfig::PixelSize)));
    sf::VertexArray pixels(sf::PrimitiveType::Triangles);
    for (int step = 0; step <= steps; ++step) {
      const auto point = SnapToPixels(from + delta * (static_cast<float>(step) / static_cast<float>(steps)));
      const auto a = point - sf::Vector2f{thickness * 0.5f, thickness * 0.5f};
      const auto b = a + sf::Vector2f{thickness, 0.f};
      const auto c = a + sf::Vector2f{thickness, thickness};
      const auto d = a + sf::Vector2f{0.f, thickness};
      for (const auto vertex : {a, b, c, a, c, d}) pixels.append({vertex, color});
    }
    ctx_.renderer.Draw(pixels);
  }

  void WorldRenderer::Label(const std::string &text, sf::Vector2f position, sf::Color color, unsigned int size) const {
    if (!font_) return;
    sf::Text label(*font_, sf::String::fromUtf8(text.begin(), text.end()), size);
    const auto bounds = label.getLocalBounds();
    position = SnapToPixels(position - sf::Vector2f{bounds.size.x * 0.5f, 0.f});
    PixelRect(position - sf::Vector2f{5.f, 1.f}, bounds.size + sf::Vector2f{10.f, 9.f}, {5, 9, 16, 220});
    label.setPosition(position);
    label.setFillColor(color);
    ctx_.renderer.Draw(label);
  }

  void WorldRenderer::Box(sf::FloatRect bounds, float height, sf::Vector2f camera, sf::Color top, sf::Color left, sf::Color right) const {
    const auto a = GroundToScreen(bounds.position, camera);
    const auto b = GroundToScreen(bounds.position + sf::Vector2f{bounds.size.x, 0.f}, camera);
    const auto c = GroundToScreen(bounds.position + bounds.size, camera);
    const auto d = GroundToScreen(bounds.position + sf::Vector2f{0.f, bounds.size.y}, camera);
    const sf::Vector2f rise{0.f, height};
    Polygon({d, c, c - rise, d - rise}, left);
    Polygon({c, b, b - rise, c - rise}, right);
    Polygon({a - rise, b - rise, c - rise, d - rise}, top);
    PixelLine(d, c, PresentationConfig::Outline);
    PixelLine(c, b, PresentationConfig::Outline);
    PixelLine(d - rise, c - rise, Shade(top, 1.2f));
    PixelLine(c - rise, b - rise, Shade(top, 0.7f));
    PixelLine(c, c - rise, Shade(right, 0.7f));
  }

  void WorldRenderer::Floor(const WorldState &world, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    PixelRect({}, gConfig.windowSize, Void);
    sf::VertexArray tiles(sf::PrimitiveType::Triangles);
    sf::VertexArray grid(sf::PrimitiveType::Lines);
    const int columns = static_cast<int>(std::ceil(world.size.x / FloorTileSize));
    const int rows = static_cast<int>(std::ceil(world.size.y / FloorTileSize));
    for (int x = 0; x < columns; ++x) for (int y = 0; y < rows; ++y) {
      const sf::Vector2f start{x * FloorTileSize, y * FloorTileSize};
      const sf::Vector2f end{std::min(start.x + FloorTileSize, world.size.x), std::min(start.y + FloorTileSize, world.size.y)};
      const auto a = GroundToScreen(start, camera);
      const auto b = GroundToScreen({end.x, start.y}, camera);
      const auto c = GroundToScreen(end, camera);
      const auto d = GroundToScreen({start.x, end.y}, camera);
      const auto color = ((x + y) % 2 == 0) ? PresentationConfig::Floor : FloorAlternate;
      for (const auto point : {a, b, c, a, c, d}) tiles.append({point, color});
      for (const auto point : {a, b, a, d}) grid.append({point, FloorGrid});
    }
    ctx_.renderer.Draw(tiles);
    ctx_.renderer.Draw(grid);
    const auto a = GroundToScreen({}, camera);
    const auto b = GroundToScreen({world.size.x, 0.f}, camera);
    const auto c = GroundToScreen(world.size, camera);
    const auto d = GroundToScreen({0.f, world.size.y}, camera);
    PixelLine(a, b, WallTop, 4.f);
    PixelLine(b, c, WallTop, 4.f);
    PixelLine(c, d, WallTop, 4.f);
    PixelLine(d, a, WallTop, 4.f);
    for (float x = 72.f; x < world.size.x; x += 144.f) {
      const auto light = GroundToScreen({x, 24.f}, camera);
      PixelRect(light - sf::Vector2f{14.f, 2.f}, {28.f, 4.f}, {93, 139, 153});
    }
    if (world.phase == GamePhase::Base) {
      const auto center = GroundToScreen(world.size * 0.5f, camera);
      Label("C I O   /   SECTOR 07", center - sf::Vector2f{0.f, 28.f}, {83, 108, 122});
    }
  }

  void WorldRenderer::ObstacleArt(const Obstacle &obstacle, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    if (obstacle.cover == CoverKind::Hide) {
      const auto center = GroundToScreen(obstacle.bounds.position + obstacle.bounds.size * 0.5f, camera);
      Polygon({center + sf::Vector2f{-40.f, 0.f}, center + sf::Vector2f{0.f, -20.f},
               center + sf::Vector2f{40.f, 0.f}, center + sf::Vector2f{0.f, 20.f}}, {25, 50, 46});
      return;
    }
    const bool low = obstacle.cover == CoverKind::Low;
    const float height = low ? LowCoverHeight : HighCoverHeight;
    Box(obstacle.bounds, height, camera, low ? CrateTop : WallTop, low ? CrateLeft : WallLeft, low ? CrateRight : WallRight);
    const auto front = GroundToScreen(obstacle.bounds.position + obstacle.bounds.size, camera);
    PixelRect(front - sf::Vector2f{12.f, height - 8.f}, {8.f, 4.f}, low ? Amber : Cyan);
    if (obstacle.bounds.size.x > 80.f) {
      const float y = obstacle.bounds.position.y + obstacle.bounds.size.y;
      for (float x = obstacle.bounds.position.x + 24.f; x < obstacle.bounds.position.x + obstacle.bounds.size.x; x += 48.f) {
        const auto seam = GroundToScreen({x, y}, camera);
        PixelLine(seam - sf::Vector2f{0.f, height - 6.f}, seam - sf::Vector2f{0.f, 4.f}, Shade(low ? CrateLeft : WallLeft, 0.8f));
      }
    }
  }

  void WorldRenderer::ObjectArt(const Interactable &object, sf::Vector2f camera, bool nearby) const {
    using namespace PresentationConfig;
    const auto feet = GroundToScreen(object.position, camera);
    auto accent = object.completed ? Green : Cyan;
    if (!object.active) accent = {81, 96, 105};
    const auto cabinet = [&](sf::Vector2f size, float height) {
      Box({object.position - size * 0.5f, size}, height, camera, WallTop, WallLeft, WallRight);
    };
    switch (object.kind) {
    case ObjectKind::Operations:
      cabinet({78.f, 48.f}, 26.f);
      Polygon({feet + sf::Vector2f{-32.f, -26.f}, feet + sf::Vector2f{0.f, -40.f},
               feet + sf::Vector2f{32.f, -26.f}, feet + sf::Vector2f{0.f, -12.f}}, {39, 112, 132});
      PixelLine(feet + sf::Vector2f{-22.f, -26.f}, feet + sf::Vector2f{22.f, -26.f}, Cyan);
      break;
    case ObjectKind::Armory:
      cabinet({70.f, 26.f}, 58.f);
      for (float x : {-18.f, 0.f, 18.f}) {
        PixelRect(feet + sf::Vector2f{x, -52.f}, {4.f, 30.f}, Outline);
        PixelRect(feet + sf::Vector2f{x - 4.f, -36.f}, {12.f, 4.f}, Light);
      }
      break;
    case ObjectKind::Intelligence:
      cabinet({60.f, 38.f}, 42.f);
      PixelRect(feet + sf::Vector2f{-24.f, -62.f}, {48.f, 28.f}, Outline);
      PixelRect(feet + sf::Vector2f{-20.f, -58.f}, {40.f, 20.f}, {26, 80, 95});
      for (float y : {-54.f, -48.f, -42.f}) PixelRect(feet + sf::Vector2f{-16.f, y}, {24.f, 2.f}, Cyan);
      break;
    case ObjectKind::Deployment:
    case ObjectKind::Extraction: {
      const auto color = object.kind == ObjectKind::Extraction ? Green : Amber;
      const sf::Vector2f a = GroundToScreen(object.position - sf::Vector2f{50.f, 50.f}, camera);
      const sf::Vector2f b = GroundToScreen(object.position + sf::Vector2f{50.f, -50.f}, camera);
      const sf::Vector2f c = GroundToScreen(object.position + sf::Vector2f{50.f, 50.f}, camera);
      const sf::Vector2f d = GroundToScreen(object.position + sf::Vector2f{-50.f, 50.f}, camera);
      PixelLine(a, b, color, 4.f); PixelLine(b, c, color, 4.f); PixelLine(c, d, color, 4.f); PixelLine(d, a, color, 4.f);
      PixelRect(feet + sf::Vector2f{-18.f, -16.f}, {6.f, 30.f}, Shade(color, 0.65f));
      PixelRect(feet + sf::Vector2f{12.f, -16.f}, {6.f, 30.f}, Shade(color, 0.65f));
      PixelRect(feet + sf::Vector2f{-12.f, -4.f}, {24.f, 6.f}, Shade(color, 0.65f));
      break;
    }
    case ObjectKind::Door:
      Box({object.position - sf::Vector2f{12.f, 32.f}, {24.f, 6.f}}, 66.f, camera, WallTop, WallLeft, WallRight);
      Box({object.position + sf::Vector2f{-12.f, 26.f}, {24.f, 6.f}}, 66.f, camera, WallTop, WallLeft, WallRight);
      if (!object.open) cabinet({20.f, 48.f}, 58.f);
      PixelRect(feet + sf::Vector2f{-5.f, -52.f}, {10.f, 4.f}, object.open ? Green : Amber);
      break;
    case ObjectKind::Camera: {
      PixelLine(feet, feet - sf::Vector2f{0.f, 46.f}, WallTop, 4.f);
      PixelRect(feet - sf::Vector2f{10.f, 50.f}, {20.f, 10.f}, Light);
      const sf::Vector2f direction{(object.facing.x - object.facing.y) * 14.f, (object.facing.x + object.facing.y) * 7.f};
      PixelLine(feet - sf::Vector2f{0.f, 44.f}, feet - sf::Vector2f{0.f, 44.f} + direction, object.active ? Red : Outline, 4.f);
      break;
    }
    case ObjectKind::Hideout:
      cabinet({48.f, 48.f}, HideoutHeight);
      for (float y : {-30.f, -24.f, -18.f, -12.f}) PixelRect(feet + sf::Vector2f{-18.f, y}, {30.f, 2.f}, Outline);
      accent = Green;
      break;
    case ObjectKind::CoopSwitch:
      cabinet({44.f, 34.f}, 26.f);
      PixelRect(feet + sf::Vector2f{-15.f, -38.f}, {12.f, 10.f}, accent);
      PixelRect(feet + sf::Vector2f{5.f, -38.f}, {12.f, 10.f}, accent);
      PixelRect(feet + sf::Vector2f{-20.f, -48.f}, {40.f, 4.f}, Outline);
      PixelRect(feet + sf::Vector2f{-20.f, -48.f}, {40.f * std::clamp(object.progress / GameplayConfig::CoopHoldDuration, 0.f, 1.f), 4.f}, Amber);
      break;
    case ObjectKind::AlarmPanel:
      cabinet({28.f, 24.f}, 42.f);
      PixelRect(feet + sf::Vector2f{-9.f, -48.f}, {18.f, 12.f}, object.active ? Red : Green);
      PixelRect(feet + sf::Vector2f{-5.f, -28.f}, {10.f, 8.f}, Outline);
      break;
    case ObjectKind::Terminal:
    case ObjectKind::Secondary:
      cabinet({38.f, 28.f}, 30.f);
      PixelRect(feet + sf::Vector2f{-18.f, -50.f}, {36.f, 24.f}, Outline);
      PixelRect(feet + sf::Vector2f{-14.f, -46.f}, {28.f, 16.f}, Shade(accent, 0.55f));
      PixelRect(feet + sf::Vector2f{-10.f, -40.f}, {16.f, 2.f}, accent);
      PixelRect(feet + sf::Vector2f{-10.f, -34.f}, {10.f, 2.f}, accent);
      break;
    }
    if (nearby || object.kind == ObjectKind::Deployment || object.kind == ObjectKind::Extraction) {
      Label((object.completed ? "[OK] " : "") + object.name, feet + sf::Vector2f{0.f, 24.f}, accent);
    }
  }

  void WorldRenderer::Humanoid(sf::Vector2f feet, sf::Vector2f facing, Stance stance, sf::Color uniform,
                              float animation, bool moving, bool running, bool down, bool grappling) const {
    using namespace PresentationConfig;
    const float side = facing.x - facing.y >= 0.f ? 1.f : -1.f;
    const auto dark = Shade(uniform, 0.55f);
    const auto light = Shade(uniform, 1.35f);
    const auto at = [&](float x, float y, float width, float height, sf::Color color) {
      PixelRect(feet + sf::Vector2f{x * side - width * 0.5f, y}, {width, height}, color);
    };
    Polygon({feet + sf::Vector2f{-16.f, 0.f}, feet + sf::Vector2f{0.f, -6.f},
             feet + sf::Vector2f{18.f, 0.f}, feet + sf::Vector2f{0.f, 7.f}}, {0, 0, 0, 100});
    const auto frame = static_cast<std::size_t>(std::max(0.f, animation) * (running ? 13.f : 8.f)) % WalkStride.size();
    const float stride = moving ? WalkStride[frame] * (running ? 1.5f : 1.f) : 0.f;
    if (down || stance == Stance::Prone) {
      at(-14.f - stride, -6.f, 18.f, 6.f, Outline);
      at(-14.f - stride, -8.f, 16.f, 4.f, dark);
      at(-10.f + stride, 0.f, 18.f, 4.f, Outline);
      at(-10.f + stride, -2.f, 16.f, 4.f, uniform);
      at(4.f, -12.f, 24.f, 14.f, Outline);
      at(4.f, -10.f, 20.f, 10.f, uniform);
      at(2.f, -12.f, 10.f, 8.f, dark);
      at(20.f, -10.f, 12.f, 10.f, Outline);
      at(21.f, -10.f, 10.f, 6.f, light);
      at(23.f, -4.f, 6.f, 4.f, Skin);
      at(17.f + stride, 4.f, 18.f, 4.f, dark);
      return;
    }
    const bool crouched = stance == Stance::Crouched;
    const float lift = crouched ? 12.f : 0.f;
    at(-5.f + stride, -16.f + lift * 0.4f, 6.f, 16.f - lift * 0.4f, Outline);
    at(-5.f + stride, -14.f + lift * 0.4f, 4.f, 12.f - lift * 0.4f, dark);
    at(5.f - stride, -16.f + lift * 0.4f, 6.f, 16.f - lift * 0.4f, Outline);
    at(5.f - stride, -14.f + lift * 0.4f, 4.f, 12.f - lift * 0.4f, uniform);
    at(-5.f + stride + 2.f, -2.f, 10.f, 4.f, Outline);
    at(5.f - stride + 2.f, -2.f, 10.f, 4.f, Outline);
    at(-8.f, -34.f + lift, 10.f, 16.f, Outline);
    at(-8.f, -32.f + lift, 6.f, 12.f, dark);
    at(0.f, -34.f + lift, 18.f, 20.f, Outline);
    at(0.f, -32.f + lift, 14.f, 16.f, uniform);
    at(0.f, -28.f + lift, 14.f, 4.f, dark);
    at(-2.f, -32.f + lift, 2.f, 14.f, light);
    at(2.f, -20.f + lift, 12.f, 4.f, dark);
    const float hand = grappling ? 18.f : 10.f;
    const float armLift = grappling ? -7.f : (moving ? -stride * 0.5f : 0.f);
    at(8.f, -30.f + lift + armLift, 6.f, 12.f, Outline);
    at(8.f, -30.f + lift + armLift, 4.f, 8.f, uniform);
    at(hand, -24.f + lift + armLift, grappling ? 16.f : 10.f, 4.f, dark);
    at(hand + 3.f, -24.f + lift + armLift, 4.f, 4.f, Skin);
    at(1.f, -ActorHeight + lift, 12.f, 14.f, Outline);
    at(2.f, -ActorHeight + 2.f + lift, 10.f, 8.f, Skin);
    at(0.f, -ActorHeight + lift, 14.f, 6.f, dark);
    at(-2.f, -ActorHeight + lift, 8.f, 2.f, light);
    at(5.f, -ActorHeight + 6.f + lift, 6.f, 2.f, Outline);
    if (!grappling) {
      const sf::Vector2f aim{facing.x - facing.y, (facing.x + facing.y) * 0.5f};
      const float length = std::hypot(aim.x, aim.y);
      const auto muzzle = feet + sf::Vector2f{12.f * side, -22.f + lift};
      PixelLine(muzzle, muzzle + aim * (16.f / std::max(length, 0.01f)), Outline, 4.f);
      PixelLine(muzzle, muzzle + aim * (12.f / std::max(length, 0.01f)), {99, 115, 119});
    }
  }

  void WorldRenderer::AgentArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local) const {
    using namespace PresentationConfig;
    if (agent.extracted) return;
    const auto feet = GroundToScreen(agent.position, camera);
    auto uniform = AgentUniforms[(agent.id - 1) % AgentUniforms.size()];
    if (!agent.connected || agent.hidden) uniform = Shade(uniform, 0.55f);
    const bool grappling = std::any_of(world.guards.begin(), world.guards.end(), [&agent](const Guard &guard) {
      return guard.heldBy == agent.id;
    });
    Humanoid(feet, agent.facing, agent.stance, uniform, agent.animationTime, agent.moving, agent.running,
             !agent.alive || agent.incapacitated, grappling);
    if (!agent.alive || agent.incapacitated) PixelRect(feet + sf::Vector2f{-2.f, -6.f}, {6.f, 4.f}, agent.alive ? Amber : Red);
    if (local || agent.hidden || !agent.connected) {
      Label(!agent.connected ? "SIN CONEXIÓN" : (agent.hidden ? "OCULTO" : agent.name), feet - sf::Vector2f{0.f, 72.f}, local ? Green : Light);
    }
    if (agent.draggingGuard >= 0) PixelLine(feet, feet - sf::Vector2f{18.f, 8.f}, Amber, 2.f);
  }

  void WorldRenderer::GuardArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    if (guard.bodyHidden) return;
    const auto feet = GroundToScreen(guard.position, camera);
    const bool body = guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated;
    const bool down = body || guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained;
    const bool alert = guard.state == GuardState::Alert;
    const auto destination = guard.state == GuardState::Patrol && !guard.patrol.empty() ? guard.patrol[guard.waypoint % guard.patrol.size()] : guard.lastKnown;
    const bool moving = !down && guard.cqcState == CqcState::None &&
                        (destination - guard.position).lengthSquared() > GameplayConfig::InvestigationStopDistance * GameplayConfig::InvestigationStopDistance;
    Humanoid(feet, guard.facing, Stance::Standing, {126, 99, 77}, world.elapsed + guard.id * 0.25f,
             moving, alert, down, guard.cqcState == CqcState::Grabbed);
    if (body) {
      PixelRect(feet + sf::Vector2f{-2.f, -6.f}, {6.f, 4.f}, guard.state == GuardState::Dead ? Red : Amber);
      Label(guard.state == GuardState::Dead ? "ELIMINADO" : "INCAPACITADO", feet + sf::Vector2f{0.f, 12.f}, guard.state == GuardState::Dead ? Red : Amber);
      return;
    }
    if (guard.state != GuardState::Patrol || guard.cqcState != CqcState::None) {
      std::string state = "SOSPECHA";
      if (guard.state == GuardState::Investigate) state = "INVESTIGA";
      if (guard.state == GuardState::Search) state = "BUSCA";
      if (alert) state = "ALERTA";
      if (guard.cqcState == CqcState::Grabbed) state = "AGARRADO";
      if (guard.cqcState == CqcState::KnockedDown) state = "DERRIBADO";
      if (guard.cqcState == CqcState::Restrained) state = "INMOVILIZADO";
      Label(state, feet - sf::Vector2f{0.f, 72.f}, alert ? Red : Amber);
    }
    if (guard.suspicion > 0.f) {
      PixelRect(feet - sf::Vector2f{18.f, 54.f}, {36.f, 4.f}, Outline);
      PixelRect(feet - sf::Vector2f{18.f, 54.f}, {36.f * std::clamp(guard.suspicion, 0.f, 1.f), 4.f}, alert ? Red : Amber);
    }
  }

  void WorldRenderer::Vision(const Guard &guard, const WorldState &world, sf::Vector2f camera) const {
    if (guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.cqcState != CqcState::None) return;
    auto color = guard.state == GuardState::Alert ? PresentationConfig::Red : PresentationConfig::Amber;
    color.a = guard.state == GuardState::Alert ? 34 : 20;
    FieldOfView(guard.position, guard.facing, GameplayConfig::VisionDistance * (world.alarm ? GameplayConfig::AlarmVisionMultiplier : 1.f), color, world, camera);
  }

  void WorldRenderer::FieldOfView(sf::Vector2f position, sf::Vector2f facing, float range, sf::Color color, const WorldState &world, sf::Vector2f camera) const {
    const float angle = std::atan2(facing.y, facing.x);
    const float halfAngle = std::acos(GameplayConfig::VisionCosHalfAngle);
    const auto origin = GroundToScreen(position, camera);
    sf::VertexArray cone(sf::PrimitiveType::Triangles);
    sf::Vector2f previous;
    for (int step = 0; step <= PresentationConfig::VisionSegments; ++step) {
      const float rayAngle = angle - halfAngle + halfAngle * 2.f * step / PresentationConfig::VisionSegments;
      const sf::Vector2f delta{std::cos(rayAngle) * range, std::sin(rayAngle) * range};
      const auto end = GroundToScreen(RayEnd(position, delta, world), camera);
      if (step > 0) {
        cone.append({origin, color}); cone.append({previous, color}); cone.append({end, color});
      }
      previous = end;
    }
    ctx_.renderer.Draw(cone);
  }

  void WorldRenderer::Noise(const NoiseEvent &noise, sf::Vector2f camera) const {
    auto color = PresentationConfig::Amber;
    color.a = static_cast<std::uint8_t>(std::clamp(noise.remaining / GameplayConfig::NoiseDuration, 0.f, 1.f) * 150.f);
    const float radius = noise.radius * (1.f - std::clamp(noise.remaining / GameplayConfig::NoiseDuration, 0.f, 1.f) * 0.35f);
    sf::VertexArray ring(sf::PrimitiveType::Lines);
    for (int step = 0; step < PresentationConfig::NoiseCircleSegments; ++step) {
      for (int endpoint = 0; endpoint <= 1; ++endpoint) {
        const float angle = 2.f * std::numbers::pi_v<float> * (step + endpoint) / PresentationConfig::NoiseCircleSegments;
        ring.append({GroundToScreen(noise.position + sf::Vector2f{std::cos(angle), std::sin(angle)} * radius, camera), color});
      }
    }
    ctx_.renderer.Draw(ring);
  }

  void WorldRenderer::Minimap(const WorldState &world, PlayerId localPlayer) const {
    using namespace PresentationConfig;
    const auto origin = MinimapPosition + MinimapGroundOffset;
    const auto scale = MinimapGroundSize.componentWiseDiv(world.size);
    const auto map = [&](sf::Vector2f position) { return origin + position.componentWiseMul(scale); };
    PixelRect(MinimapPosition - sf::Vector2f{2.f, 2.f}, MinimapSize + sf::Vector2f{4.f, 4.f}, WallLeft);
    PixelRect(MinimapPosition, MinimapSize, {7, 14, 23, 240});
    Label(world.phase == GamePhase::Base ? "PLANO / BASE CIO" : "PLANO / OPERACIÓN", MinimapPosition + sf::Vector2f{120.f, 4.f}, Light, MinimapTextSize);
    PixelRect(origin, MinimapGroundSize, {22, 33, 42});
    for (const auto &obstacle : world.obstacles) {
      PixelRect(map(obstacle.bounds.position), obstacle.bounds.size.componentWiseMul(scale),
                obstacle.cover == CoverKind::Low ? CrateTop : obstacle.cover == CoverKind::Hide ? sf::Color{38, 76, 62} : WallTop);
    }
    const auto markerColor = [](ObjectKind kind) {
      switch (kind) {
      case ObjectKind::Terminal: case ObjectKind::Operations: return Cyan;
      case ObjectKind::Secondary: case ObjectKind::Armory: return Amber;
      case ObjectKind::CoopSwitch: case ObjectKind::Intelligence: return Violet;
      case ObjectKind::Extraction: case ObjectKind::Deployment: return Green;
      case ObjectKind::Camera: case ObjectKind::AlarmPanel: return Red;
      default: return Light;
      }
    };
    for (const auto &object : world.objects) {
      const auto point = map(object.position);
      PixelRect(point - sf::Vector2f{4.f, 4.f}, {8.f, 8.f}, Outline);
      PixelRect(point - sf::Vector2f{2.f, 2.f}, {4.f, 4.f}, object.completed || !object.active ? Shade(markerColor(object.kind), 0.45f) : markerColor(object.kind));
    }
    const auto camera = CameraGround(world, localPlayer);
    for (const auto &guard : world.guards) {
      const auto delta = guard.position - camera;
      if (guard.bodyHidden || delta.lengthSquared() > GameplayConfig::VisionDistance * GameplayConfig::VisionDistance ||
          (RayEnd(camera, delta, world) - guard.position).lengthSquared() > 1.f) continue;
      PixelRect(map(guard.position) - sf::Vector2f{2.f, 2.f}, {4.f, 4.f}, guard.state == GuardState::Incapacitated ? Amber : guard.state == GuardState::Dead ? WallTop : Red);
    }
    for (const auto &agent : world.agents) {
      if (!agent.connected || agent.extracted) continue;
      const auto point = map(agent.position);
      const auto color = agent.id == localPlayer ? Green : Cyan;
      PixelRect(point - sf::Vector2f{4.f, 4.f}, {8.f, 8.f}, Outline);
      PixelRect(point - sf::Vector2f{2.f, 2.f}, {4.f, 4.f}, !agent.alive ? WallTop : agent.incapacitated ? Amber : color);
      const auto unicodeName = sf::String::fromUtf8(agent.name.begin(), agent.name.end()).substring(0, 9).toUtf8();
      const std::string shortName(unicodeName.begin(), unicodeName.end());
      const float nameX = std::clamp(point.x, origin.x + 35.f, origin.x + MinimapGroundSize.x - 35.f);
      const float nameY = std::clamp(point.y - 15.f - (agent.id % 4) * 11.f, origin.y, origin.y + MinimapGroundSize.y - 18.f);
      Label(std::to_string(agent.id) + " " + shortName, {nameX, nameY}, color, MinimapTextSize);
    }
    const bool base = world.phase == GamePhase::Base;
    const std::array colors{Cyan, Amber, Violet, Green};
    const std::array<std::string, 4> names = base ? std::array<std::string, 4>{"OPERACIONES", "ARMERÍA", "INTELIGENCIA", "DESPLIEGUE"}
                                               : std::array<std::string, 4>{"PRINCIPAL", "SECUNDARIO", "REPETIDOR", "EXTRACCIÓN"};
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto position = MinimapPosition + sf::Vector2f{12.f + static_cast<float>(index % 2) * 118.f, 151.f + static_cast<float>(index / 2) * 19.f};
      PixelRect(position + sf::Vector2f{0.f, 4.f}, {6.f, 6.f}, colors[index]);
      Label(names[index], position + sf::Vector2f{60.f, 0.f}, colors[index], MinimapTextSize);
    }
  }

  void WorldRenderer::Render(const WorldState &world, PlayerId localPlayer) const {
    ctx_.renderer.ResetView();
    const auto camera = CameraGround(world, localPlayer);
    Floor(world, camera);
    for (const auto &guard : world.guards) Vision(guard, world, camera);
    for (const auto &object : world.objects) {
      if (object.kind == ObjectKind::Camera && object.active) {
        FieldOfView(object.position, object.facing, GameplayConfig::CameraDistance, {221, 67, 77, 26}, world, camera);
      }
    }
    for (const auto &noise : world.noises) Noise(noise, camera);
    struct Item { float depth; int kind; std::size_t index; };
    std::vector<Item> items;
    for (std::size_t index = 0; index < world.obstacles.size(); ++index) {
      const auto end = world.obstacles[index].bounds.position + world.obstacles[index].bounds.size;
      items.push_back({end.x + end.y, 0, index});
    }
    for (std::size_t index = 0; index < world.objects.size(); ++index) {
      const auto position = world.objects[index].position;
      items.push_back({position.x + position.y + 4.f, 1, index});
    }
    for (std::size_t index = 0; index < world.guards.size(); ++index) {
      const auto position = world.guards[index].position;
      items.push_back({position.x + position.y, 2, index});
    }
    for (std::size_t index = 0; index < world.agents.size(); ++index) {
      const auto position = world.agents[index].position;
      items.push_back({position.x + position.y + 1.f, 3, index});
    }
    std::stable_sort(items.begin(), items.end(), [](const Item &left, const Item &right) { return left.depth < right.depth; });
    for (const auto &item : items) {
      switch (item.kind) {
      case 0: ObstacleArt(world.obstacles[item.index], camera); break;
      case 1: ObjectArt(world.objects[item.index], camera, world.phase == GamePhase::Base ||
                        (world.objects[item.index].position - camera).lengthSquared() < GameplayConfig::InteractionDistance * GameplayConfig::InteractionDistance * 4.f); break;
      case 2: GuardArt(world.guards[item.index], world, camera); break;
      case 3: AgentArt(world.agents[item.index], world, camera, world.agents[item.index].id == localPlayer); break;
      }
    }
    const auto player = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
    if (player != world.agents.end() && player->alive && !player->extracted) {
      const auto position = GroundToScreen(player->position, camera);
      PixelLine(position + sf::Vector2f{-18.f, 8.f}, position + sf::Vector2f{-8.f, 12.f}, PresentationConfig::Green);
      PixelLine(position + sf::Vector2f{8.f, 12.f}, position + sf::Vector2f{18.f, 8.f}, PresentationConfig::Green);
    }
    Minimap(world, localPlayer);
  }
}
