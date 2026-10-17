#include "NoTraceWorldRenderer.h"

#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTracePresentationConfig.h"
#include "Core/EngineContext.h"
#include "NoTraceDistractionGeometry.h"
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Text.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <fstream>
#include <stdexcept>

namespace NoTrace {
  namespace {
    using namespace PresentationConfig;

    std::vector<sf::IntRect> LoadFrames(const char *file, std::size_t expected, const sf::Texture *texture) {
      if (!texture) throw std::runtime_error("No se encuentra el atlas de personajes de NO TRACE");
      std::ifstream input(file);
      std::vector<sf::IntRect> frames;
      int x, y, width, height;
      while (input >> x >> y >> width >> height) {
        if (x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > static_cast<int>(texture->getSize().x) || y + height > static_cast<int>(texture->getSize().y)) {
          throw std::runtime_error("Fotograma fuera de los límites del atlas de NO TRACE");
        }
        frames.emplace_back(sf::Vector2i{x, y}, sf::Vector2i{width, height});
      }
      if (frames.size() != expected) throw std::runtime_error("La tabla de fotogramas de NO TRACE está incompleta");
      return frames;
    }

    std::vector<sf::Vector2f> LoadMuzzles(const char *file, const std::vector<sf::IntRect> &frames) {
      std::ifstream input(file);
      std::vector<sf::Vector2f> muzzles;
      float x, y;
      while (input >> x >> y) {
        const auto index = muzzles.size();
        const bool absent = x == -1.f && y == -1.f;
        if (index >= frames.size() || !std::isfinite(x) || !std::isfinite(y) ||
            (!absent && (x < 0.f || y < 0.f || x >= frames[index].size.x || y >= frames[index].size.y))) {
          throw std::runtime_error("Boca de arma fuera de los límites del fotograma de NO TRACE");
        }
        muzzles.push_back({x, y});
      }
      if (!input.eof() || muzzles.size() != frames.size()) throw std::runtime_error("La tabla de bocas de arma de NO TRACE está incompleta");
      return muzzles;
    }

    sf::Color Shade(sf::Color color, float factor) {
      return {static_cast<std::uint8_t>(std::clamp(color.r * factor, 0.f, 255.f)),
              static_cast<std::uint8_t>(std::clamp(color.g * factor, 0.f, 255.f)),
              static_cast<std::uint8_t>(std::clamp(color.b * factor, 0.f, 255.f)), color.a};
    }

    sf::Vector2f RayEnd(sf::Vector2f origin, sf::Vector2f delta, const WorldState &world) {
      float nearest = 1.f;
      const auto clip = [&](sf::FloatRect bounds) {
        float entry = 0.f;
        if (WorldGeometry::SegmentRect(origin, origin + delta, bounds, &entry)) nearest = std::min(nearest, std::max(0.f, entry));
      };
      for (const auto &obstacle : world.obstacles) {
        if (WorldGeometry::BlocksVision(obstacle, Stance::Standing)) clip(obstacle.bounds);
      }
      for (const auto &object : world.objects) {
        if (WorldGeometry::BlocksVision(object, Stance::Standing)) clip(WorldGeometry::ObjectFootprint(object));
      }
      return origin + delta * nearest;
    }
  }

  WorldRenderer::WorldRenderer(EngineContext &context)
      : ctx_(context), font_(context.resources.FetchFont("Relidux.otf")),
        actorAtlas_(context.resources.FetchTexture(PresentationConfig::ActorAtlas)), proneAtlas_(context.resources.FetchTexture(PresentationConfig::ProneAtlas)),
        actorFrames_(LoadFrames(PresentationConfig::ActorFrameTable, PresentationConfig::ActorAtlasRows * PresentationConfig::ActorAtlasColumns, actorAtlas_)),
        proneFrames_(LoadFrames(PresentationConfig::ProneFrameTable, PresentationConfig::ProneAtlasRows * PresentationConfig::ProneAtlasColumns, proneAtlas_)),
        actorMuzzles_(LoadMuzzles(PresentationConfig::ActorMuzzleTable, actorFrames_)), proneMuzzles_(LoadMuzzles(PresentationConfig::ProneMuzzleTable, proneFrames_)) {
    actorAtlas_->setSmooth(false); proneAtlas_->setSmooth(false);
  }

  sf::Vector2f WorldRenderer::CameraGround(const WorldState &world, PlayerId localPlayer) const {
    const auto player = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) {
      return agent.id == localPlayer;
    });
    return player == world.agents.end() ? world.size * 0.5f : player->position;
  }

  sf::Vector2f WorldRenderer::GroundToScreen(sf::Vector2f ground, sf::Vector2f camera) const {
    return ProjectGround(ground, camera, ctx_.window.GetLogicalSize());
  }

  sf::Vector2f WorldRenderer::ScreenToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const {
    return UnprojectGround(logical, CameraGround(world, localPlayer), ctx_.window.GetLogicalSize(), zoom_);
  }

  sf::Vector2f WorldRenderer::AimToGround(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const {
    return AimAt(logical, world, localPlayer).ground;
  }

  WorldRenderer::AimSolution WorldRenderer::AimAt(sf::Vector2f logical, const WorldState &world, PlayerId localPlayer) const {
    return ResolveAim(logical, world, localPlayer, ctx_.window.GetLogicalSize(), zoom_);
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

  void WorldRenderer::Floor(const WorldState &world, sf::Vector2f camera, const VisibilityGrid &visibility) const {
    using namespace PresentationConfig;
    sf::VertexArray tiles(sf::PrimitiveType::Triangles);
    sf::VertexArray grid(sf::PrimitiveType::Lines);
    const bool fog = world.phase == GamePhase::Mission;
    const float tile = fog && visibility.cellSize > 0.f ? visibility.cellSize : FloorTileSize;
    const int columns = static_cast<int>(std::ceil(world.size.x / tile));
    const int rows = static_cast<int>(std::ceil(world.size.y / tile));
    for (int x = 0; x < columns; ++x) for (int y = 0; y < rows; ++y) {
      const sf::Vector2f start{x * tile, y * tile};
      const sf::Vector2f end{std::min(start.x + tile, world.size.x), std::min(start.y + tile, world.size.y)};
      if (fog && !VisibleCell(visibility, (start + end) * 0.5f)) continue;
      const auto a = GroundToScreen(start, camera);
      const auto b = GroundToScreen({end.x, start.y}, camera);
      const auto c = GroundToScreen(end, camera);
      const auto d = GroundToScreen({start.x, end.y}, camera);
      const auto color = ((static_cast<int>(start.x / FloorTileSize) + static_cast<int>(start.y / FloorTileSize)) % 2 == 0) ? PresentationConfig::Floor : FloorAlternate;
      for (const auto point : {a, b, c, a, c, d}) tiles.append({point, color});
      for (const auto point : {a, b, a, d}) grid.append({point, FloorGrid});
    }
    ctx_.renderer.Draw(tiles);
    ctx_.renderer.Draw(grid);
    const auto a = GroundToScreen({}, camera);
    const auto b = GroundToScreen({world.size.x, 0.f}, camera);
    const auto c = GroundToScreen(world.size, camera);
    const auto d = GroundToScreen({0.f, world.size.y}, camera);
    if (!fog) {
      PixelLine(a, b, WallTop, 4.f); PixelLine(b, c, WallTop, 4.f);
      PixelLine(c, d, WallTop, 4.f); PixelLine(d, a, WallTop, 4.f);
    }
    for (float x = 72.f; x < world.size.x; x += 144.f) {
      if (fog && !VisibleCell(visibility, {x, 24.f})) continue;
      const auto light = GroundToScreen({x, 24.f}, camera);
      PixelRect(light - sf::Vector2f{14.f, 2.f}, {28.f, 4.f}, {93, 139, 153});
    }
    if (world.phase == GamePhase::Base) {
      const auto center = GroundToScreen(world.size * 0.5f, camera);
      Label("C I O   /   SECTOR 07", center - sf::Vector2f{0.f, 28.f}, {83, 108, 122});
    }
  }

  void WorldRenderer::Fog(const WorldState &world, sf::Vector2f camera, const VisibilityGrid &visibility) const {
    if (world.phase != GamePhase::Mission || visibility.cellSize <= 0.f) return;
    sf::VertexArray mask(sf::PrimitiveType::Triangles);
    for (unsigned int x = 0; x < visibility.columns; ++x) for (unsigned int y = 0; y < visibility.rows; ++y) {
      const sf::Vector2f start{x * visibility.cellSize, y * visibility.cellSize};
      const sf::Vector2f end{std::min(start.x + visibility.cellSize, world.size.x), std::min(start.y + visibility.cellSize, world.size.y)};
      if (VisibleCell(visibility, (start + end) * 0.5f)) continue;
      const auto a = GroundToScreen(start, camera), b = GroundToScreen({end.x, start.y}, camera);
      const auto c = GroundToScreen(end, camera), d = GroundToScreen({start.x, end.y}, camera);
      for (const auto point : {a, b, c, a, c, d}) mask.append({point, PresentationConfig::Fog});
    }
    ctx_.renderer.Draw(mask);
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
    const bool tactical = !WorldGeometry::Properties(object).decorative;
    auto accent = tactical ? (object.completed ? Green : Cyan) : WallTop;
    if (tactical && !object.active) accent = {81, 96, 105};
    const auto cabinet = [&](float height) {
      Box(ObjectFootprint(object), height, camera, WallTop, WallLeft, WallRight);
    };
    switch (object.kind) {
    case ObjectKind::Operations:
      cabinet(26.f);
      Polygon({feet + sf::Vector2f{-32.f, -26.f}, feet + sf::Vector2f{0.f, -40.f},
               feet + sf::Vector2f{32.f, -26.f}, feet + sf::Vector2f{0.f, -12.f}}, {39, 112, 132});
      PixelLine(feet + sf::Vector2f{-22.f, -26.f}, feet + sf::Vector2f{22.f, -26.f}, Cyan);
      break;
    case ObjectKind::Armory:
      cabinet(58.f);
      for (float x : {-18.f, 0.f, 18.f}) {
        PixelRect(feet + sf::Vector2f{x, -52.f}, {4.f, 30.f}, Outline);
        PixelRect(feet + sf::Vector2f{x - 4.f, -36.f}, {12.f, 4.f}, Light);
      }
      break;
    case ObjectKind::Intelligence:
      cabinet(42.f);
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
      if (!object.open) Box({object.position - sf::Vector2f{10.f, 24.f}, {20.f, 48.f}}, 58.f, camera, WallTop, WallLeft, WallRight);
      PixelRect(feet + sf::Vector2f{-5.f, -52.f}, {10.f, 4.f}, tactical ? (object.open ? Green : Amber) : WallTop);
      break;
    case ObjectKind::Camera: {
      PixelLine(feet, feet - sf::Vector2f{0.f, 46.f}, WallTop, 4.f);
      PixelRect(feet - sf::Vector2f{10.f, 50.f}, {20.f, 10.f}, Light);
      const sf::Vector2f direction{(object.facing.x - object.facing.y) * 14.f, (object.facing.x + object.facing.y) * 7.f};
      PixelLine(feet - sf::Vector2f{0.f, 44.f}, feet - sf::Vector2f{0.f, 44.f} + direction, tactical ? (object.active ? Red : Outline) : WallTop, 4.f);
      break;
    }
    case ObjectKind::Hideout: {
      const auto type = WorldGeometry::HideoutType(object);
      const auto bounds = ObjectFootprint(object);
      const float height = WorldGeometry::ObjectHeight(object);
      if (type == HideoutKind::UnderTable) {
        for (const float x : {bounds.position.x + 3.f, bounds.position.x + bounds.size.x - 9.f}) for (const float y : {bounds.position.y + 3.f, bounds.position.y + bounds.size.y - 9.f}) {
          Box({{x, y}, {6.f, 6.f}}, height, camera, WallTop, WallLeft, WallRight);
        }
        const sf::Vector2f rise{0.f, height};
        const auto a = GroundToScreen(bounds.position, camera) - rise;
        const auto b = GroundToScreen(bounds.position + sf::Vector2f{bounds.size.x, 0.f}, camera) - rise;
        const auto c = GroundToScreen(bounds.position + bounds.size, camera) - rise;
        const auto d = GroundToScreen(bounds.position + sf::Vector2f{0.f, bounds.size.y}, camera) - rise;
        Polygon({d, c, c + sf::Vector2f{0.f, 5.f}, d + sf::Vector2f{0.f, 5.f}}, CrateLeft);
        Polygon({b, c, c + sf::Vector2f{0.f, 5.f}, b + sf::Vector2f{0.f, 5.f}}, CrateRight);
        Polygon({a, b, c, d}, CrateTop);
        PixelLine(d, c, Light); PixelLine(c, b, Outline);
      } else if (type == HideoutKind::Closet) {
        cabinet(height);
        PixelRect(feet + sf::Vector2f{-17.f, -height + 7.f}, {34.f, height - 10.f}, Outline);
        PixelRect(feet + sf::Vector2f{-15.f, -height + 9.f}, {30.f, height - 14.f}, WallLeft);
        PixelLine(feet + sf::Vector2f{0.f, -height + 9.f}, feet - sf::Vector2f{0.f, 7.f}, Outline);
        PixelRect(feet + sf::Vector2f{-6.f, -height * 0.5f}, {3.f, 7.f}, Light);
        PixelRect(feet + sf::Vector2f{4.f, -height * 0.5f}, {3.f, 7.f}, Light);
      } else {
        cabinet(height);
        PixelRect(feet + sf::Vector2f{-15.f, -height - 2.f}, {30.f, 4.f}, Outline);
        PixelRect(feet + sf::Vector2f{-7.f, -height - 5.f}, {14.f, 3.f}, WallTop);
        for (const float y : {-30.f, -24.f, -18.f, -12.f}) PixelRect(feet + sf::Vector2f{-18.f, y}, {30.f, 2.f}, Outline);
        PixelRect(feet + sf::Vector2f{-17.f, 1.f}, {6.f, 6.f}, Outline); PixelRect(feet + sf::Vector2f{12.f, 1.f}, {6.f, 6.f}, Outline);
      }
      if (tactical) PixelRect(feet + sf::Vector2f{10.f, -height + 8.f}, {5.f, 5.f}, WorldGeometry::HideoutOccupied(object) ? Amber : Green);
      accent = Green;
      break;
    }
    case ObjectKind::CoopSwitch:
      cabinet(26.f);
      PixelRect(feet + sf::Vector2f{-15.f, -38.f}, {12.f, 10.f}, accent);
      PixelRect(feet + sf::Vector2f{5.f, -38.f}, {12.f, 10.f}, accent);
      if (tactical) {
        PixelRect(feet + sf::Vector2f{-20.f, -48.f}, {40.f, 4.f}, Outline);
        PixelRect(feet + sf::Vector2f{-20.f, -48.f}, {40.f * std::clamp(object.progress / GameplayConfig::CoopHoldDuration, 0.f, 1.f), 4.f}, Amber);
      }
      break;
    case ObjectKind::AlarmPanel:
      cabinet(42.f);
      PixelRect(feet + sf::Vector2f{-9.f, -48.f}, {18.f, 12.f}, tactical ? (object.active ? Red : Green) : Light);
      PixelRect(feet + sf::Vector2f{-5.f, -28.f}, {10.f, 8.f}, Outline);
      break;
    case ObjectKind::Terminal:
    case ObjectKind::Secondary:
      cabinet(30.f);
      PixelRect(feet + sf::Vector2f{-18.f, -50.f}, {36.f, 24.f}, Outline);
      PixelRect(feet + sf::Vector2f{-14.f, -46.f}, {28.f, 16.f}, Shade(accent, 0.55f));
      PixelRect(feet + sf::Vector2f{-10.f, -40.f}, {16.f, 2.f}, accent);
      PixelRect(feet + sf::Vector2f{-10.f, -34.f}, {10.f, 2.f}, accent);
      break;
    }
    if (tactical && (nearby || object.kind == ObjectKind::Deployment || object.kind == ObjectKind::Extraction)) {
      const auto occupied = WorldGeometry::HideoutType(object) != HideoutKind::None && WorldGeometry::HideoutOccupied(object) ? " / OCUPADO" : "";
      Label((object.completed ? "[OK] " : "") + object.name + occupied, feet + sf::Vector2f{0.f, 24.f}, accent);
    }
  }

  WorldRenderer::SpriteFrame WorldRenderer::AgentFrame(const Agent &agent, const WorldState &world, bool local) const {
    const bool grappling = std::any_of(world.guards.begin(), world.guards.end(), [&agent](const Guard &guard) { return guard.heldBy == agent.id; });
    return SelectSprite(agent.facing, agent.stance, agent.animationTime, agent.moving, agent.running, !agent.alive || agent.incapacitated,
        grappling, agent.cover.active, agent.cover.peekSide != PeekSide::None, false, agent.actionPoseTime > 0.f || (local && decoyPreviewActive_));
  }

  WorldRenderer::SpriteFrame WorldRenderer::GuardFrame(const Guard &guard, const WorldState &world) const {
    const bool down = guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained;
    const auto destination = guard.state == GuardState::Patrol && !guard.patrol.empty() ? guard.patrol[guard.waypoint % guard.patrol.size()] : guard.lastKnown;
    const bool observedMovement = world.visibility.authoritative ? guard.stepTime > 0.f :
        (destination - guard.position).lengthSquared() > GameplayConfig::InvestigationStopDistance * GameplayConfig::InvestigationStopDistance;
    const bool moving = !down && guard.cqcState == CqcState::None && observedMovement;
    return SelectSprite(guard.facing, Stance::Standing, world.elapsed + guard.id * 0.25f, moving, guard.state == GuardState::Alert, down,
        guard.cqcState == CqcState::Grabbed || (guard.meleeAttack && guard.attackTime > 0.f), false, false, true);
  }

  std::optional<sf::Vector2f> WorldRenderer::SourceMuzzle(const WorldState &world, const ShotTrace &shot, sf::Vector2f camera, const Agent *observer) const {
    const auto muzzle = [&](sf::Vector2f position, sf::Vector2f facing, Stance stance, SpriteFrame frame) -> std::optional<sf::Vector2f> {
      const auto &sockets = frame.prone ? proneMuzzles_ : actorMuzzles_;
      const auto &rectangles = frame.prone ? proneFrames_ : actorFrames_;
      const auto socket = sockets.at(frame.index);
      if (socket.x < 0.f) return std::nullopt;
      return PlaceSprite(GroundToScreen(position, camera), facing, stance, false, frame, rectangles.at(frame.index).size).Map(socket);
    };
    if (shot.actor != 0) {
      const auto agent = std::find_if(world.agents.begin(), world.agents.end(), [&shot](const Agent &candidate) { return candidate.id == shot.actor; });
      if (agent == world.agents.end() || agent->hidden || agent->extracted || !agent->connected || !agent->alive || agent->incapacitated) return std::nullopt;
      if (world.phase == GamePhase::Mission && observer && agent->id != observer->id && !Visibility::IsVisible(world, *observer, agent->position, agent->stance)) return std::nullopt;
      return muzzle(agent->position, agent->facing, agent->stance, AgentFrame(*agent, world, observer && agent->id == observer->id));
    }
    const auto guard = std::find_if(world.guards.begin(), world.guards.end(), [&shot](const Guard &candidate) { return shot.guardId != 0 && candidate.id == shot.guardId; });
    if (guard == world.guards.end() || guard->bodyHidden || guard->storedIn != 0 || guard->state == GuardState::Dead || guard->state == GuardState::Incapacitated ||
        guard->cqcState == CqcState::KnockedDown || guard->cqcState == CqcState::Restrained) return std::nullopt;
    if (world.phase == GamePhase::Mission && observer && !Visibility::GuardVisible(world, *observer, *guard)) return std::nullopt;
    return muzzle(guard->position, guard->facing, Stance::Standing, GuardFrame(*guard, world));
  }

  void WorldRenderer::Humanoid(sf::Vector2f feet, sf::Vector2f facing, Stance stance, sf::Color uniform, SpriteFrame frame, bool down, bool guard) const {
    const auto &rect = frame.prone ? proneFrames_.at(frame.index) : actorFrames_.at(frame.index);
    const auto placement = PlaceSprite(feet, facing, stance, down, frame, rect.size);
    for (float y = -4.f; y <= 4.f; y += PresentationConfig::PixelSize) {
      const float width = (stance == Stance::Prone || down ? 34.f : 24.f) * (1.f - y * y / 36.f);
      PixelRect(feet + sf::Vector2f{-width * 0.5f, y}, {width, PresentationConfig::PixelSize}, {0, 0, 0, 80});
    }
    sf::Sprite sprite(*(frame.prone ? proneAtlas_ : actorAtlas_), rect);
    sprite.setOrigin(placement.origin);
    sprite.setPosition(placement.position);
    sprite.setScale(placement.scale);
    const auto base = PresentationConfig::AgentUniforms[0];
    const auto tint = [&](std::uint8_t value, std::uint8_t reference) { return static_cast<std::uint8_t>(std::clamp(value * 255.f / reference, 80.f, 255.f)); };
    sprite.setColor(guard ? sf::Color::White : sf::Color{tint(uniform.r, base.r), tint(uniform.g, base.g), tint(uniform.b, base.b)});
    if (down) sprite.setColor({180, 180, 180});
    ctx_.renderer.Draw(sprite);
  }
  void WorldRenderer::AgentArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local) const {
    using namespace PresentationConfig;
    if (agent.extracted || agent.hidden) return;
    const auto feet = GroundToScreen(agent.position, camera);
    auto uniform = AgentUniforms[(agent.id - 1) % AgentUniforms.size()];
    if (!agent.connected || agent.hidden) uniform = Shade(uniform, 0.55f);
    if (local && agent.alive) {
      PixelLine(feet + sf::Vector2f{-18.f, 8.f}, feet + sf::Vector2f{-8.f, 12.f}, Green);
      PixelLine(feet + sf::Vector2f{8.f, 12.f}, feet + sf::Vector2f{18.f, 8.f}, Green);
    }
    Humanoid(feet, agent.facing, agent.stance, uniform, AgentFrame(agent, world, local), !agent.alive || agent.incapacitated);
    if (!agent.alive || agent.incapacitated) PixelRect(feet + sf::Vector2f{-2.f, -6.f}, {6.f, 4.f}, agent.alive ? Amber : Red);
    if (local || agent.hidden || !agent.connected) {
      Label(!agent.connected ? "SIN CONEXIÓN" : (agent.hidden ? "OCULTO" : agent.name), feet - sf::Vector2f{0.f, 72.f}, local ? Green : Light);
    }
    if (agent.draggingGuard >= 0) PixelLine(feet, feet - sf::Vector2f{18.f, 8.f}, Amber, 2.f);
  }

  void WorldRenderer::GuardArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    if (guard.bodyHidden || guard.storedIn != 0) return;
    const auto feet = GroundToScreen(guard.position, camera);
    const bool body = guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated;
    const bool down = body || guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained;
    Humanoid(feet, guard.facing, Stance::Standing, {126, 99, 77}, GuardFrame(guard, world), down, true);
    if (guard.meleeAttack && guard.attackTime > 0.f && !down) {
      const auto target = GroundToScreen(guard.position + guard.facing * GameplayConfig::GuardAttackDistance, camera) - sf::Vector2f{0.f, ShotHeight};
      PixelLine(feet - sf::Vector2f{0.f, ShotHeight}, target, Amber, 4.f);
      PixelLine(target - sf::Vector2f{4.f, 6.f}, target + sf::Vector2f{4.f, 6.f}, Light);
    }
    if (body) PixelRect(feet + sf::Vector2f{-2.f, -6.f}, {6.f, 4.f}, guard.state == GuardState::Dead ? Red : Amber);
  }

  void WorldRenderer::GuardIcon(const Guard &guard, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    const auto indicator = IndicatorFor(guard);
    if (indicator == GuardIndicator::None) return;
    const float height = guard.state == GuardState::Incapacitated ? CombatConfig::ProneHeight : ActorHeight;
    const auto center = GroundToScreen(guard.position, camera) - sf::Vector2f{0.f, height + IconOffset};
    PixelRect(center - sf::Vector2f{IconHalfSize + 2.f, IconHalfSize + 2.f}, {IconHalfSize * 2.f + 4.f, IconHalfSize * 2.f + 4.f}, Outline);
    const auto at = [&](float x, float y) { return center + sf::Vector2f{x, y}; };
    switch (indicator) {
    case GuardIndicator::Suspicion:
      PixelLine(at(0.f, -8.f), at(8.f, 0.f), Amber); PixelLine(at(8.f, 0.f), at(0.f, 8.f), Amber);
      PixelLine(at(0.f, 8.f), at(-8.f, 0.f), Amber); PixelLine(at(-8.f, 0.f), at(0.f, -8.f), Amber);
      PixelRect(at(-2.f, -2.f), {4.f, 4.f}, Amber);
      break;
    case GuardIndicator::Confirmed:
      for (const float side : {-1.f, 1.f}) {
        PixelLine(at(side * 8.f, -8.f), at(side * 8.f, 8.f), Cyan);
        PixelLine(at(side * 8.f, -8.f), at(side * 4.f, -8.f), Cyan);
        PixelLine(at(side * 8.f, 8.f), at(side * 4.f, 8.f), Cyan);
      }
      PixelRect(at(-3.f, -3.f), {6.f, 6.f}, Light);
      break;
    case GuardIndicator::Combat:
      PixelLine(at(-7.f, 6.f), at(0.f, -6.f), Red, 4.f); PixelLine(at(0.f, -6.f), at(7.f, 6.f), Red, 4.f);
      PixelLine(at(-5.f, 7.f), at(5.f, 7.f), Amber);
      break;
    case GuardIndicator::Unconscious:
      PixelLine(at(-7.f, -3.f), at(-2.f, 1.f), Violet); PixelLine(at(-2.f, 1.f), at(4.f, -3.f), Violet);
      PixelRect(at(-6.f, 5.f), {6.f, 2.f}, Violet); PixelRect(at(3.f, 5.f), {4.f, 2.f}, Violet);
      break;
    default: break;
    }
  }

  void WorldRenderer::Vision(const Guard &guard, const WorldState &world, sf::Vector2f camera) const {
    if (guard.bodyHidden || guard.storedIn != 0 || guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.cqcState != CqcState::None) return;
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

  void WorldRenderer::Combat(const WorldState &world, sf::Vector2f camera, const Agent *observer) const {
    using namespace PresentationConfig;
    for (const auto &shot : world.shots) {
      if (shot.remaining <= 0.f) continue;
      const auto direction = shot.to - shot.from;
      // La boca sigue el fotograma; el impacto conserva la altura resuelta por el anfitrión.
      const auto muzzle = SourceMuzzle(world, shot, camera, observer);
      const auto start = muzzle.value_or(GroundToScreen(shot.from, camera) - sf::Vector2f{0.f, shot.fromHeight});
      const auto end = GroundToScreen(shot.to, camera) - sf::Vector2f{0.f, shot.toHeight};
      auto color = shot.lethal ? Amber : Cyan;
      color.a = static_cast<std::uint8_t>(std::clamp(shot.remaining / GameplayConfig::ShotTraceDuration, 0.f, 1.f) * 230.f);
      const auto visible = [&](sf::Vector2f point) { return world.phase != GamePhase::Mission || (observer && Visibility::IsVisible(world, *observer, point)); };
      const int pieces = std::max(1, static_cast<int>(std::ceil(direction.length() / FogEffectStep)));
      for (int piece = 0; piece < pieces; ++piece) {
        const float first = static_cast<float>(piece) / pieces, last = static_cast<float>(piece + 1) / pieces;
        if (visible(shot.from + direction * first) && visible(shot.from + direction * last)) PixelLine(start + (end - start) * first, start + (end - start) * last, color);
      }
      if (muzzle && visible(shot.from)) PixelRect(start - sf::Vector2f{3.f, 3.f}, {6.f, 6.f}, Light);
      if ((shot.hit || shot.blocked) && visible(shot.to)) {
        const auto impact = shot.hit && shot.lethal ? Red : color;
        PixelLine(end - sf::Vector2f{6.f, 0.f}, end + sf::Vector2f{6.f, 0.f}, impact);
        PixelLine(end - sf::Vector2f{0.f, 6.f}, end + sf::Vector2f{0.f, 6.f}, impact);
      }
    }
  }

  void WorldRenderer::DecoyPreview(const WorldState &world, sf::Vector2f camera, const Agent *observer) const {
    if (!decoyPreviewActive_ || !observer || observer->hidden || !observer->alive || observer->incapacitated) return;
    const bool authoritative = world.visibility.authoritative && world.visibility.observer == observer->id;
    if (authoritative && !observer->decoyGuide.active) return;
    const auto &guide = observer->decoyGuide;
    const auto trajectory = authoritative ? DistractionGeometry::DecoyTrajectory{observer->position, guide.destination, guide.distance, guide.travelled, guide.blocked, guide.valid}
        : DistractionGeometry::TraceDecoy(world, observer->position, decoyPreviewDirection_, decoyPreviewDistance_);
    const auto maximum = authoritative ? DistractionGeometry::DecoyTrajectory{observer->position, guide.maximum, DistractionConfig::MaximumDecoyDistance, guide.maximumTravelled, guide.maximumBlocked, true}
        : DistractionGeometry::TraceDecoy(world, observer->position, decoyPreviewDirection_, DistractionConfig::MaximumDecoyDistance);
    const auto visible = [&](sf::Vector2f point) { return Visibility::IsVisible(world, *observer, point); };
    auto color = !trajectory.valid ? PresentationConfig::Red : trajectory.blocked ? PresentationConfig::Amber : PresentationConfig::Cyan;
    const auto delta = trajectory.to - trajectory.from;
    const int segments = std::max(1, static_cast<int>(std::ceil(trajectory.travelled / PresentationConfig::DecoyGuideStep)));
    for (int segment = 0; segment < segments; ++segment) {
      const auto from = trajectory.from + delta * (static_cast<float>(segment) / segments);
      const auto to = trajectory.from + delta * ((static_cast<float>(segment) + 0.65f) / segments);
      if (visible(from) && visible(to)) PixelLine(GroundToScreen(from, camera), GroundToScreen(to, camera), color);
    }
    const auto marker = [&](sf::Vector2f point, float size, sf::Color tint) {
      if (!visible(point)) return;
      const auto center = GroundToScreen(point, camera);
      PixelLine(center + sf::Vector2f{-size, 0.f}, center + sf::Vector2f{0.f, -size * 0.5f}, tint);
      PixelLine(center + sf::Vector2f{0.f, -size * 0.5f}, center + sf::Vector2f{size, 0.f}, tint);
      PixelLine(center + sf::Vector2f{size, 0.f}, center + sf::Vector2f{0.f, size * 0.5f}, tint);
      PixelLine(center + sf::Vector2f{0.f, size * 0.5f}, center + sf::Vector2f{-size, 0.f}, tint);
    };
    marker(maximum.to, PresentationConfig::DecoyMarkerSize * 1.5f, {145, 163, 172});
    marker(trajectory.to, PresentationConfig::DecoyMarkerSize, color);
    if (visible(maximum.to)) Label(std::string(maximum.blocked ? "LÍMITE " : "MÁX ") + std::to_string(static_cast<int>(maximum.travelled)), GroundToScreen(maximum.to, camera) + sf::Vector2f{0.f, 12.f}, PresentationConfig::Light, 11);
  }

  void WorldRenderer::Footsteps(const WorldState &world, sf::Vector2f camera, const Agent *observer) const {
    if (!observer) return;
    for (const auto &cue : world.footsteps) {
      if (!Visibility::FootstepAudible(world, *observer, cue)) continue;
      const float angle = cue.direction * std::numbers::pi_v<float> * 0.25f;
      const sf::Vector2f forward{std::cos(angle), std::sin(angle)}, side{-forward.y, forward.x};
      auto color = PresentationConfig::Amber;
      color.a = static_cast<std::uint8_t>(std::clamp(cue.remaining / DistractionConfig::FootstepLifetime, 0.f, 1.f) * 170.f);
      for (const float offset : {-1.f, 1.f}) {
        const auto heel = cue.position + side * offset * 5.f - forward * offset * 3.f;
        const auto toe = heel + forward * PresentationConfig::FootstepMarkLength;
        PixelLine(GroundToScreen(heel, camera), GroundToScreen(toe, camera), color, 3.f);
        PixelLine(GroundToScreen(toe - side * 2.f, camera), GroundToScreen(toe + side * 2.f, camera), color, 2.f);
      }
    }
  }

  void WorldRenderer::Minimap(const WorldState &world, PlayerId localPlayer, const VisibilityGrid &visibility) const {
    using namespace PresentationConfig;
    const auto origin = MinimapPosition + MinimapGroundOffset;
    const auto scale = MinimapGroundSize.componentWiseDiv(world.size);
    const auto map = [&](sf::Vector2f position) { return origin + position.componentWiseMul(scale); };
    PixelRect(MinimapPosition - sf::Vector2f{2.f, 2.f}, MinimapSize + sf::Vector2f{4.f, 4.f}, WallLeft);
    PixelRect(MinimapPosition, MinimapSize, {7, 14, 23, 240});
    Label(world.phase == GamePhase::Base ? "PLANO / BASE CIO" : "PLANO / OPERACIÓN", MinimapPosition + sf::Vector2f{120.f, 4.f}, Light, MinimapTextSize);
    const auto player = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
    const bool fog = world.phase == GamePhase::Mission;
    const auto visible = [&](sf::Vector2f point) { return !fog || (player != world.agents.end() && Visibility::IsVisible(world, *player, point)); };
    PixelRect(origin, MinimapGroundSize, fog ? PresentationConfig::Fog : sf::Color{22, 33, 42});
    if (fog) for (unsigned int x = 0; x < visibility.columns; ++x) for (unsigned int y = 0; y < visibility.rows; ++y) {
      const sf::Vector2f position{x * visibility.cellSize, y * visibility.cellSize};
      if (!VisibleCell(visibility, position + sf::Vector2f{visibility.cellSize * 0.5f, visibility.cellSize * 0.5f})) continue;
      const sf::Vector2f size{std::min(visibility.cellSize, world.size.x - position.x), std::min(visibility.cellSize, world.size.y - position.y)};
      PixelRect(map(position), size.componentWiseMul(scale), {22, 33, 42});
    }
    for (const auto &obstacle : world.obstacles) {
      if (fog && (player == world.agents.end() || !FootprintVisible(world, *player, obstacle.bounds))) continue;
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
      if (WorldGeometry::Properties(object).decorative) continue;
      if (fog && (player == world.agents.end() || !Visibility::ObjectVisible(world, *player, object))) continue;
      const auto point = map(object.position);
      PixelRect(point - sf::Vector2f{4.f, 4.f}, {8.f, 8.f}, Outline);
      PixelRect(point - sf::Vector2f{2.f, 2.f}, {4.f, 4.f}, object.completed || !object.active ? Shade(markerColor(object.kind), 0.45f) : markerColor(object.kind));
    }
    for (const auto &guard : world.guards) {
      if (guard.bodyHidden || guard.storedIn != 0 || (fog && (player == world.agents.end() || !Visibility::GuardVisible(world, *player, guard)))) continue;
      PixelRect(map(guard.position) - sf::Vector2f{2.f, 2.f}, {4.f, 4.f}, guard.state == GuardState::Incapacitated ? Amber : guard.state == GuardState::Dead ? WallTop : Red);
    }
    for (const auto &agent : world.agents) {
      if (!agent.connected || agent.extracted) continue;
      if (agent.id != localPlayer && (agent.hidden || !visible(agent.position))) continue;
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
    PixelRect({}, ctx_.window.GetLogicalSize(), PresentationConfig::Void);
    ctx_.renderer.SetView(WorldView(ctx_.window.GetLogicalSize(), zoom_));
    const auto camera = CameraGround(world, localPlayer);
    const auto player = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
    const Agent *observer = player == world.agents.end() ? nullptr : &*player;
    const bool fog = world.phase == GamePhase::Mission;
    const auto visible = [&](sf::Vector2f point, Stance stance = Stance::Standing) { return !fog || (observer && Visibility::IsVisible(world, *observer, point, stance)); };
    const auto objectVisible = [&](const Interactable &object) { return !fog || (observer && Visibility::ObjectVisible(world, *observer, object)); };
    const auto guardVisible = [&](const Guard &guard) { return !guard.bodyHidden && guard.storedIn == 0 && (!fog || (observer && Visibility::GuardVisible(world, *observer, guard))); };
    const auto obstacleVisible = [&](const Obstacle &obstacle) { return !fog || (observer && FootprintVisible(world, *observer, obstacle.bounds)); };
    const auto visibility = observer ? Visibility::BuildGrid(world, *observer) : VisibilityGrid{};
    Floor(world, camera, visibility);
    for (const auto &obstacle : world.obstacles) if (obstacle.cover == CoverKind::Hide && obstacleVisible(obstacle)) ObstacleArt(obstacle, camera);
    for (const auto &guard : world.guards) if (guardVisible(guard)) Vision(guard, world, camera);
    for (const auto &object : world.objects) {
      if (object.kind == ObjectKind::Camera && object.active && !WorldGeometry::Properties(object).decorative && objectVisible(object)) {
        FieldOfView(object.position, object.facing, GameplayConfig::CameraDistance, {221, 67, 77, 26}, world, camera);
      }
    }
    for (const auto &noise : world.noises) if (visible(noise.position)) Noise(noise, camera);
    Fog(world, camera, visibility);
    Footsteps(world, camera, observer);
    DecoyPreview(world, camera, observer);
    struct Item { int kind; std::size_t index; };
    std::vector<Item> items;
    std::vector<sf::FloatRect> footprints;
    const auto add = [&](int kind, std::size_t index, sf::FloatRect footprint) {
      items.push_back({kind, index});
      footprints.push_back(footprint);
    };
    for (std::size_t index = 0; index < world.obstacles.size(); ++index) {
      if (world.obstacles[index].cover != CoverKind::Hide && obstacleVisible(world.obstacles[index])) add(0, index, world.obstacles[index].bounds);
    }
    for (std::size_t index = 0; index < world.objects.size(); ++index) {
      if (objectVisible(world.objects[index])) add(1, index, ObjectFootprint(world.objects[index]));
    }
    for (std::size_t index = 0; index < world.guards.size(); ++index) {
      if (guardVisible(world.guards[index])) add(2, index, {world.guards[index].position, {0.f, 0.f}});
    }
    for (std::size_t index = 0; index < world.agents.size(); ++index) {
      const auto &agent = world.agents[index];
      if (agent.hidden || agent.extracted) continue;
      if (agent.id == localPlayer || visible(agent.position, agent.stance)) add(3, index, {agent.position, {0.f, 0.f}});
    }
    for (const auto index : GroundDrawOrder(footprints)) {
      const auto &item = items[index];
      switch (item.kind) {
      case 0: ObstacleArt(world.obstacles[item.index], camera); break;
      case 1: ObjectArt(world.objects[item.index], camera, world.phase == GamePhase::Base ||
                        (world.objects[item.index].position - camera).lengthSquared() < GameplayConfig::InteractionDistance * GameplayConfig::InteractionDistance * 4.f); break;
      case 2: GuardArt(world.guards[item.index], world, camera); break;
      case 3: AgentArt(world.agents[item.index], world, camera, world.agents[item.index].id == localPlayer); break;
      }
    }
    for (const auto &guard : world.guards) if (guardVisible(guard)) GuardIcon(guard, camera);
    Combat(world, camera, observer);
    ctx_.renderer.ResetView();
    Minimap(world, localPlayer, visibility);
  }
}
