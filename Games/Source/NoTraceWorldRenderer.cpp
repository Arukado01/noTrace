#include "NoTraceWorldRenderer.h"

#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceAudioConfig.h"
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
#include <format>
#include <stdexcept>

namespace NoTrace {
  namespace {
    using namespace PresentationConfig;

    class ActorLayer final : public sf::Drawable {
      const sf::Texture &texture_;
      sf::VertexArray vertices_{sf::PrimitiveType::Triangles};
      void draw(sf::RenderTarget &target, sf::RenderStates states) const override {
        states.texture = &texture_; target.draw(vertices_, states);
      }
    public:
      ActorLayer(const sf::Texture &texture, sf::IntRect rect, WorldRenderer::SpritePlacement placement, sf::Vector2f pivot, sf::Vector2f normal, bool upper, sf::Color color) : texture_(texture) {
        std::vector<sf::Vector2f> polygon{{0.f,0.f},{static_cast<float>(rect.size.x),0.f},sf::Vector2f(rect.size),{0.f,static_cast<float>(rect.size.y)}};
        std::vector<sf::Vector2f> clipped;
        const auto distance = [&](sf::Vector2f point) { return (point-pivot).dot(normal) * (upper ? 1.f : -1.f); };
        for (std::size_t index=0;index<polygon.size();++index) {
          const auto from=polygon[index], to=polygon[(index+1)%polygon.size()]; const float a=distance(from), b=distance(to);
          if (a>=0.f) clipped.push_back(from);
          if ((a>=0.f)!=(b>=0.f)) clipped.push_back(from+(to-from)*(a/(a-b)));
        }
        for (std::size_t index=1;index+1<clipped.size();++index) for (const auto point:{clipped[0],clipped[index],clipped[index+1]})
          vertices_.append({placement.Map(point),color,sf::Vector2f(rect.position)+point});
      }
    };

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

    std::vector<WorldRenderer::GroundSupport> LoadSupports(const sf::Texture &texture, const std::vector<sf::IntRect> &frames) {
      const auto image = texture.copyToImage();
      std::vector<WorldRenderer::GroundSupport> supports(frames.size());
      for (std::size_t first = 0; first < frames.size(); first += 4) {
        const auto support = WorldRenderer::MeasureGroundSupport(image, {frames.at(first), frames.at(first + 1), frames.at(first + 2), frames.at(first + 3)});
        std::fill_n(supports.begin() + first, 4, support);
      }
      return supports;
    }

    template <typename Id>
    WorldRenderer::SpriteFrame StableFrame(WorldRenderer::SpriteFrame frame, sf::Vector2f facing, Id id, std::unordered_map<Id, std::size_t> &directions) {
      if (!frame.operative) return frame;
      const auto original = frame.index / SpriteConfig::Columns % SpriteConfig::Directions;
      if (WorldRenderer::SpriteDirection(facing) != original) { directions[id] = original; return frame; }
      const auto previous = directions.find(id);
      const auto direction = WorldRenderer::StableSpriteDirection(facing, previous == directions.end() ? std::nullopt : std::optional{previous->second});
      frame.index = frame.index - original * SpriteConfig::Columns + direction * SpriteConfig::Columns;
      directions[id] = direction;
      return frame;
    }

    sf::Color Shade(sf::Color color, float factor) {
      return {static_cast<std::uint8_t>(std::clamp(color.r * factor, 0.f, 255.f)),
              static_cast<std::uint8_t>(std::clamp(color.g * factor, 0.f, 255.f)),
              static_cast<std::uint8_t>(std::clamp(color.b * factor, 0.f, 255.f)), color.a};
    }

    sf::Vector2f RayEnd(sf::Vector2f origin, sf::Vector2f delta, const WorldState &world, bool solid = false, std::uint32_t ignoredObject = 0) {
      float nearest = 1.f;
      const auto clip = [&](sf::FloatRect bounds) {
        float entry = 0.f;
        if (WorldGeometry::SegmentRect(origin, origin + delta, bounds, &entry)) nearest = std::min(nearest, std::max(0.f, entry));
      };
      for (const auto &obstacle : world.obstacles) {
        if (solid ? WorldGeometry::BlocksMovement(obstacle) : WorldGeometry::BlocksVision(obstacle, Stance::Standing)) clip(obstacle.bounds);
      }
      for (const auto &object : world.objects) {
        if (ignoredObject != 0 && object.id == ignoredObject) continue;
        if (solid ? WorldGeometry::BlocksMovement(object) : WorldGeometry::BlocksVision(object, Stance::Standing)) clip(WorldGeometry::ObjectFootprint(object));
      }
      return origin + delta * nearest;
    }

    template <typename Project>
    sf::VertexArray FogMask(const WorldState &world, const VisibilityGrid &visibility, Project project) {
      sf::VertexArray mask(sf::PrimitiveType::Triangles);
      const auto quad = [&](sf::Vector2f a, sf::Vector2f b, sf::Vector2f c, sf::Vector2f d, sf::Color inner, sf::Color outer) {
        for (const auto &vertex : std::array<sf::Vertex, 6>{{{project(a), inner}, {project(b), inner}, {project(c), outer},
            {project(a), inner}, {project(c), outer}, {project(d), outer}}}) mask.append(vertex);
      };
      if (visibility.visualRadii.size() != VisibilityConfig::VisualRayCount) {
        quad({}, {world.size.x, 0.f}, world.size, {0.f, world.size.y}, PresentationConfig::Fog, PresentationConfig::Fog);
        return mask;
      }
      const float turn = 2.f * std::numbers::pi_v<float>;
      std::vector<float> angles;
      for (std::size_t index = 0; index < visibility.visualRadii.size(); ++index) angles.push_back(turn * index / visibility.visualRadii.size());
      for (const auto corner : {sf::Vector2f{}, sf::Vector2f{world.size.x, 0.f}, world.size, sf::Vector2f{0.f, world.size.y}}) {
        const auto delta = corner - visibility.visualOrigin;
        const float angle = std::atan2(delta.y, delta.x);
        angles.push_back(angle < 0.f ? angle + turn : angle);
      }
      std::sort(angles.begin(), angles.end());
      angles.erase(std::unique(angles.begin(), angles.end(), [](float first, float last) { return std::abs(first - last) < 0.00001f; }), angles.end());
      const auto edge = [&](sf::Vector2f direction) {
        float distance = world.size.length() * 2.f;
        if (direction.x > 0.00001f) distance = std::min(distance, (world.size.x - visibility.visualOrigin.x) / direction.x);
        if (direction.x < -0.00001f) distance = std::min(distance, -visibility.visualOrigin.x / direction.x);
        if (direction.y > 0.00001f) distance = std::min(distance, (world.size.y - visibility.visualOrigin.y) / direction.y);
        if (direction.y < -0.00001f) distance = std::min(distance, -visibility.visualOrigin.y / direction.y);
        return std::max(0.f, distance);
      };
      std::vector<sf::Vector2f> boundary, exterior, interior;
      for (const float angle : angles) {
        const sf::Vector2f direction{std::cos(angle), std::sin(angle)};
        const float mapEdge = edge(direction);
        boundary.push_back(visibility.visualOrigin + direction * std::min(mapEdge, WorldRenderer::FogBoundaryRadius(visibility, angle)));
        exterior.push_back(visibility.visualOrigin + direction * mapEdge);
      }
      for (std::size_t index = 0; index < boundary.size(); ++index) {
        const auto point = boundary[index], before = point - boundary[(index + boundary.size() - 1) % boundary.size()], after = boundary[(index + 1) % boundary.size()] - point;
        const auto normal = [](sf::Vector2f direction) { return direction.lengthSquared() > 0.f ? sf::Vector2f{-direction.y, direction.x} / direction.length() : sf::Vector2f{}; };
        const auto firstNormal = normal(before), lastNormal = normal(after);
        auto offset = (firstNormal + lastNormal) * (FogFeatherWidth / std::max(0.1f, 1.f + firstNormal.dot(lastNormal)));
        const float maximum = std::min(FogFeatherWidth * 2.f, (point - visibility.visualOrigin).length() * 0.95f);
        if (offset.length() > maximum) offset *= maximum / offset.length();
        auto inner = point + offset;
        const auto delta = inner - visibility.visualOrigin;
        const float allowed = WorldRenderer::FogBoundaryRadius(visibility, std::atan2(delta.y, delta.x));
        if (delta.length() > allowed && delta.lengthSquared() > 0.f) inner = visibility.visualOrigin + delta / delta.length() * std::max(0.f, allowed - FogFeatherWidth);
        interior.push_back(inner);
      }
      for (std::size_t sector = 0; sector < boundary.size(); ++sector) {
        const auto next = (sector + 1) % boundary.size();
        const auto firstPoint = [&](float blend) { return interior[sector] + (boundary[sector] - interior[sector]) * blend; };
        const auto lastPoint = [&](float blend) { return interior[next] + (boundary[next] - interior[next]) * blend; };
        const auto tint = [](float blend) {
          auto color = PresentationConfig::Fog;
          color.a = static_cast<std::uint8_t>(255.f * blend * blend * (3.f - 2.f * blend));
          return color;
        };
        for (unsigned int band = 0; band < FogFeatherBands; ++band) {
          const float inner = static_cast<float>(band) / FogFeatherBands, outer = static_cast<float>(band + 1) / FogFeatherBands;
          quad(firstPoint(inner), lastPoint(inner), lastPoint(outer), firstPoint(outer), tint(inner), tint(outer));
        }
        quad(firstPoint(1.f), lastPoint(1.f), exterior[next], exterior[sector], PresentationConfig::Fog, PresentationConfig::Fog);
      }
      return mask;
    }
  }

  WorldRenderer::WorldRenderer(EngineContext &context)
      : ctx_(context), font_(context.resources.FetchFont("Relidux.otf")),
        actorAtlas_(context.resources.FetchTexture(PresentationConfig::ActorAtlas)), proneAtlas_(context.resources.FetchTexture(PresentationConfig::ProneAtlas)),
        operativeAtlas_(context.resources.FetchTexture(SpriteConfig::Atlas)),
        actorFrames_(LoadFrames(PresentationConfig::ActorFrameTable, PresentationConfig::ActorAtlasRows * PresentationConfig::ActorAtlasColumns, actorAtlas_)),
        proneFrames_(LoadFrames(PresentationConfig::ProneFrameTable, PresentationConfig::ProneAtlasRows * PresentationConfig::ProneAtlasColumns, proneAtlas_)),
        actorMuzzles_(LoadMuzzles(PresentationConfig::ActorMuzzleTable, actorFrames_)), proneMuzzles_(LoadMuzzles(PresentationConfig::ProneMuzzleTable, proneFrames_)),
        operativeFrames_(LoadFrames(SpriteConfig::FrameTable, SpriteConfig::FrameCount, operativeAtlas_)),
        operativeMuzzles_(LoadMuzzles(SpriteConfig::MuzzleTable, operativeFrames_)),
        operativeSupports_(LoadSupports(*operativeAtlas_, operativeFrames_)) {
    actorAtlas_->setSmooth(false); proneAtlas_->setSmooth(false); operativeAtlas_->setSmooth(false);
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

  void WorldRenderer::Floor(const WorldState &world, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    sf::VertexArray tiles(sf::PrimitiveType::Triangles);
    sf::VertexArray grid(sf::PrimitiveType::Lines);
    const bool fog = world.phase == GamePhase::Mission;
    const float tile = FloorTileSize;
    const int columns = static_cast<int>(std::ceil(world.size.x / tile));
    const int rows = static_cast<int>(std::ceil(world.size.y / tile));
    for (int x = 0; x < columns; ++x) for (int y = 0; y < rows; ++y) {
      const sf::Vector2f start{x * tile, y * tile};
      const sf::Vector2f end{std::min(start.x + tile, world.size.x), std::min(start.y + tile, world.size.y)};
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
      const auto light = GroundToScreen({x, 24.f}, camera);
      PixelRect(light - sf::Vector2f{14.f, 2.f}, {28.f, 4.f}, {93, 139, 153});
    }
    if (world.phase == GamePhase::Base) {
      const auto center = GroundToScreen(world.size * 0.5f, camera);
      Label(IsTraining(world) ? "C I O   /   ENTRENAMIENTO" : "C I O   /   SECTOR 07", center - sf::Vector2f{0.f, 28.f}, {83, 108, 122});
    }
  }

  void WorldRenderer::Fog(const WorldState &world, sf::Vector2f camera, const VisibilityGrid &visibility) const {
    if (world.phase != GamePhase::Mission) return;
    const auto focus = LogicalCenter(ctx_.window.GetLogicalSize());
    ctx_.renderer.Draw(FogMask(world, visibility, [&](sf::Vector2f point) {
      const auto delta = point - camera;
      return focus + sf::Vector2f{(delta.x - delta.y) * PresentationConfig::GroundProjection.x, (delta.x + delta.y) * PresentationConfig::GroundProjection.y};
    }));
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
      if (object.locked) {
        PixelLine(feet + sf::Vector2f{6.f, -27.f}, feet + sf::Vector2f{6.f, -34.f}, Amber, 3.f);
        PixelLine(feet + sf::Vector2f{6.f, -34.f}, feet + sf::Vector2f{12.f, -34.f}, Amber, 3.f);
        PixelRect(feet + sf::Vector2f{3.f, -27.f}, {12.f, 10.f}, Amber);
        PixelRect(feet + sf::Vector2f{8.f, -25.f}, {2.f, 5.f}, Outline);
      }
      break;
    case ObjectKind::Camera: {
      PixelLine(feet, feet - sf::Vector2f{0.f, 46.f}, WallTop, 4.f);
      PixelRect(feet - sf::Vector2f{10.f, 50.f}, {20.f, 10.f}, Light);
      const sf::Vector2f direction{(object.facing.x - object.facing.y) * 14.f, (object.facing.x + object.facing.y) * 7.f};
      const bool operating = object.active && object.camera.state == CameraState::Operating;
      PixelLine(feet - sf::Vector2f{0.f, 44.f}, feet - sf::Vector2f{0.f, 44.f} + direction, tactical ? (operating ? Red : Outline) : WallTop, 4.f);
      if (!operating) PixelLine(feet + sf::Vector2f{-10.f, -51.f}, feet + sf::Vector2f{10.f, -39.f}, object.camera.state == CameraState::Broken ? Red : Amber, 3.f);
      break;
    }
    case ObjectKind::Hideout: {
      const auto type = WorldGeometry::HideoutType(object);
      const auto bounds = ObjectFootprint(object);
      const float height = WorldGeometry::ObjectHeight(object);
      if (type == HideoutKind::UnderTable) {
        const float tableHeight = SpatialConfig::UnderTableHeight;
        const float lifting = object.lifted ? 1.f : std::clamp(object.inspectionProgress, 0.f, 1.f);
        for (const float x : {bounds.position.x + 3.f, bounds.position.x + bounds.size.x - 9.f}) for (const float y : {bounds.position.y + 3.f, bounds.position.y + bounds.size.y - 9.f}) {
          Box({{x, y}, {6.f, 6.f}}, tableHeight, camera, WallTop, WallLeft, WallRight);
        }
        const sf::Vector2f rise{0.f, tableHeight};
        const auto a = GroundToScreen(bounds.position, camera) - rise;
        const auto b = GroundToScreen(bounds.position + sf::Vector2f{bounds.size.x, 0.f}, camera) - rise;
        const auto c = GroundToScreen(bounds.position + bounds.size, camera) - rise - sf::Vector2f{0.f, 42.f * lifting};
        const auto d = GroundToScreen(bounds.position + sf::Vector2f{0.f, bounds.size.y}, camera) - rise - sf::Vector2f{0.f, 42.f * lifting};
        Polygon({d, c, c + sf::Vector2f{0.f, 5.f}, d + sf::Vector2f{0.f, 5.f}}, CrateLeft);
        Polygon({b, c, c + sf::Vector2f{0.f, 5.f}, b + sf::Vector2f{0.f, 5.f}}, CrateRight);
        Polygon({a, b, c, d}, CrateTop);
        PixelLine(d, c, Light); PixelLine(c, b, Outline);
        if (object.inspectionProgress > 0.f && !object.lifted) {
          const auto marker = feet - sf::Vector2f{0.f, tableHeight + 18.f};
          PixelLine(marker + sf::Vector2f{-6.f, 6.f}, marker, Amber);
          PixelLine(marker, marker + sf::Vector2f{6.f, 6.f}, Amber);
          Label("INSPECCIÓN", feet + sf::Vector2f{0.f, 5.f}, Amber, 11);
        }
      } else if (type == HideoutKind::Closet) {
        cabinet(height);
        const auto normal = WorldGeometry::HideoutFrontNormal(object);
        const sf::Vector2f tangent{-normal.y, normal.x};
        const float depth = (std::abs(normal.x) * bounds.size.x + std::abs(normal.y) * bounds.size.y) * 0.5f;
        const float width = (std::abs(tangent.x) * bounds.size.x + std::abs(tangent.y) * bounds.size.y) * 0.5f;
        const auto front = object.position + normal * (depth + 0.5f);
        const auto left = GroundToScreen(front - tangent * width * 0.85f, camera), right = GroundToScreen(front + tangent * width * 0.85f, camera);
        const sf::Vector2f bottom{0.f, -4.f}, top{0.f, -height + 5.f};
        Polygon({left + top, right + top, right + bottom, left + bottom}, Outline);
        const float aperture = object.open ? std::clamp(object.hideoutAperture, 0.f, 1.f) : 0.f;
        const auto back = sf::Vector2f{0.f, -height * 0.15f};
        PixelLine(left + top + sf::Vector2f{5.f, 7.f}, right + top + sf::Vector2f{-5.f, 7.f}, WallRight, 3.f);
        PixelLine(left + bottom + back, right + bottom + back, WallRight, 3.f);
        PixelLine((left + right) * 0.5f + top + sf::Vector2f{0.f, 12.f}, (left + right) * 0.5f + top + sf::Vector2f{0.f, 21.f}, WallTop);
        const float angle = aperture * std::numbers::pi_v<float> * 0.45f;
        const auto hinge = front - tangent * width * 0.85f;
        const auto doorEnd = GroundToScreen(hinge + (tangent * std::cos(angle) + normal * std::sin(angle)) * width * 1.7f, camera);
        Polygon({left + top, doorEnd + top, doorEnd + bottom, left + bottom}, Shade(WallLeft, 1.1f));
        PixelLine(doorEnd + top, doorEnd + bottom, Light, 3.f);
        PixelRect(doorEnd * 0.82f + left * 0.18f - sf::Vector2f{2.f, height * 0.48f}, {4.f, 9.f}, Amber);
        PixelLine(left + top, right + top, WallTop, 5.f);
        PixelLine(left + top, left + bottom, WallTop, 4.f);
        PixelLine(right + top, right + bottom, WallTop, 4.f);
        PixelLine(left + bottom, right + bottom, Light, 3.f);
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
    case ObjectKind::CameraPanel:
      cabinet(42.f);
      PixelRect(feet + sf::Vector2f{-9.f, -48.f}, {18.f, 12.f}, tactical ? (object.active ? Red : Green) : Light);
      PixelRect(feet + sf::Vector2f{-5.f, -28.f}, {10.f, 8.f}, Outline);
      if (object.kind == ObjectKind::CameraPanel) {
        PixelRect(feet + sf::Vector2f{-6.f, -45.f}, {8.f, 6.f}, Cyan);
        PixelLine(feet + sf::Vector2f{2.f, -42.f}, feet + sf::Vector2f{7.f, -45.f}, Cyan);
      }
      break;
    case ObjectKind::Terminal:
    case ObjectKind::Secondary:
    case ObjectKind::Computer:
      cabinet(30.f);
      PixelRect(feet + sf::Vector2f{-18.f, -50.f}, {36.f, 24.f}, Outline);
      PixelRect(feet + sf::Vector2f{-14.f, -46.f}, {28.f, 16.f}, Shade(accent, 0.55f));
      PixelRect(feet + sf::Vector2f{-10.f, -40.f}, {16.f, 2.f}, accent);
      PixelRect(feet + sf::Vector2f{-10.f, -34.f}, {10.f, 2.f}, accent);
      if (object.kind == ObjectKind::Computer && object.puzzle.state != PuzzleState::Solved) {
        PixelRect(feet + sf::Vector2f{3.f, -44.f}, {6.f, 10.f}, Amber);
        PixelLine(feet + sf::Vector2f{1.f, -44.f}, feet + sf::Vector2f{11.f, -44.f}, Amber);
      }
      break;
    case ObjectKind::Explosive:
      if (object.trap.state == TrapState::Armed) return;
      if (object.trap.state == TrapState::Detonated) {
        Polygon({feet + sf::Vector2f{-16.f, 0.f}, feet + sf::Vector2f{0.f, -8.f}, feet + sf::Vector2f{16.f, 0.f}, feet + sf::Vector2f{0.f, 8.f}}, Outline);
        break;
      }
      Box(ObjectFootprint(object), 6.f, camera, WallTop, WallLeft, WallRight);
      PixelLine(feet + sf::Vector2f{-9.f, -8.f}, feet + sf::Vector2f{7.f, -3.f}, object.trap.state == TrapState::Disarmed ? Green : Red);
      PixelRect(feet + sf::Vector2f{3.f, -10.f}, {6.f, 4.f}, object.trap.state == TrapState::Disarmed ? Green : Amber);
      accent = object.trap.state == TrapState::Disarmed ? Green : Red;
      break;
    case ObjectKind::TrainingReset:
      cabinet(26.f);
      PixelRect(feet + sf::Vector2f{-17.f, -42.f}, {34.f, 22.f}, Outline);
      PixelLine(feet + sf::Vector2f{-9.f, -35.f}, feet + sf::Vector2f{8.f, -35.f}, Green, 3.f);
      PixelLine(feet + sf::Vector2f{8.f, -35.f}, feet + sf::Vector2f{8.f, -27.f}, Green, 3.f);
      PixelLine(feet + sf::Vector2f{8.f, -27.f}, feet + sf::Vector2f{-9.f, -27.f}, Green, 3.f);
      Polygon({feet + sf::Vector2f{-12.f, -31.f}, feet + sf::Vector2f{-5.f, -37.f}, feet + sf::Vector2f{-5.f, -25.f}}, Green);
      accent = Green;
      break;
    case ObjectKind::PuzzleNode: {
      cabinet(18.f);
      PixelRect(feet + sf::Vector2f{-10.f, -30.f}, {20.f, 16.f}, Outline);
      const auto base = feet + sf::Vector2f{0.f, -20.f};
      const auto handle = base + sf::Vector2f{object.open ? 8.f : -8.f, -12.f};
      PixelLine(base, handle, Light, 4.f);
      PixelRect(handle - sf::Vector2f{4.f, 3.f}, {8.f, 6.f}, object.open ? Green : Amber);
      const auto number = std::clamp(static_cast<int>(object.puzzle.nodeIndex), 1, 4);
      Label(std::string(1, static_cast<char>('A' + number - 1)), feet - sf::Vector2f{0.f, 52.f}, object.open ? Green : Amber, 18);
      break;
    }
    }
    if (tactical && (nearby || object.kind == ObjectKind::Deployment || object.kind == ObjectKind::Extraction)) {
      const auto occupied = WorldGeometry::HideoutType(object) != HideoutKind::None && WorldGeometry::HideoutOccupied(object) ? " / OCUPADO" : "";
      Label((object.completed ? "[OK] " : "") + object.name + occupied, feet + sf::Vector2f{0.f, 24.f}, accent);
    }
  }

  WorldRenderer::SpriteFrame WorldRenderer::AgentFrame(const Agent &agent, const WorldState &world, bool local) const {
    const auto frame = world.phase == GamePhase::Results && (!agent.alive || agent.incapacitated) ?
        ClipFrame(SpriteConfig::Clip::Unconscious, agent.facing, 0.f) : SelectBodySprite(agent, local && decoyPreviewActive_);
    auto facing = agent.facing;
    if (agent.maneuver.kind != ManeuverKind::None) facing = agent.maneuver.direction;
    else if (agent.cover.active) facing = agent.cover.normal;
    return StableFrame(frame, facing, agent.id, agentSpriteDirections_);
  }

  WorldRenderer::SpriteFrame WorldRenderer::GuardFrame(const Guard &guard, const WorldState &world) const {
    const auto frame = world.phase == GamePhase::Results && (guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated) ?
        ClipFrame(SpriteConfig::Clip::Unconscious, guard.facing, 0.f) : SelectGuardSprite(guard, world);
    return StableFrame(frame, guard.facing, guard.id, guardSpriteDirections_);
  }

  std::optional<sf::Vector2f> WorldRenderer::SourceMuzzle(const WorldState &world, const ShotTrace &shot, sf::Vector2f camera, const Agent *observer) const {
    const auto muzzle = [&](sf::Vector2f position, sf::Vector2f facing, Stance stance, SpriteFrame frame) -> std::optional<sf::Vector2f> {
      const auto &sockets = frame.operative ? operativeMuzzles_ : frame.prone ? proneMuzzles_ : actorMuzzles_;
      const auto &rectangles = frame.operative ? operativeFrames_ : frame.prone ? proneFrames_ : actorFrames_;
      const auto socket = sockets.at(frame.index);
      if (socket.x < 0.f) return std::nullopt;
      return PlaceSprite(GroundToScreen(position, camera), facing, stance, false, frame, rectangles.at(frame.index).size).Map(socket);
    };
    if (shot.actor != 0) {
      const auto agent = std::find_if(world.agents.begin(), world.agents.end(), [&shot](const Agent &candidate) { return candidate.id == shot.actor; });
      if (agent == world.agents.end() || agent->hidden || agent->extracted || !agent->connected || !agent->alive || agent->incapacitated) return std::nullopt;
      if (IsTactical(world) && observer && agent->id != observer->id && !Visibility::IsVisible(world, *observer, agent->position, agent->stance)) return std::nullopt;
      if (agent->cover.active && HasWeaponLayer(*agent)) return CoverMuzzle(*agent, GroundToScreen(agent->position,camera));
      return muzzle(agent->position, HasWeaponLayer(*agent) ? WeaponFacing(*agent) : agent->facing, agent->stance,
          HasWeaponLayer(*agent) ? SelectWeaponSprite(*agent) : AgentFrame(*agent, world, observer && agent->id == observer->id));
    }
    const auto guard = std::find_if(world.guards.begin(), world.guards.end(), [&shot](const Guard &candidate) { return shot.guardId != 0 && candidate.id == shot.guardId; });
    if (guard == world.guards.end() || guard->bodyHidden || guard->storedIn != 0 || guard->state == GuardState::Dead || guard->state == GuardState::Incapacitated ||
        guard->cqcState == CqcState::KnockedDown || guard->cqcState == CqcState::Restrained) return std::nullopt;
    if (IsTactical(world) && observer && !Visibility::GuardVisible(world, *observer, *guard)) return std::nullopt;
    return muzzle(guard->position, guard->facing, Stance::Standing, GuardFrame(*guard, world));
  }

  void WorldRenderer::Humanoid(sf::Vector2f feet, sf::Vector2f facing, Stance stance, sf::Color uniform, SpriteFrame frame, bool down, bool guard, sf::Vector2f bodyOffset, const Agent *articulated) const {
    const auto &rect = frame.operative ? operativeFrames_.at(frame.index) : frame.prone ? proneFrames_.at(frame.index) : actorFrames_.at(frame.index);
    const auto groundPlacement = PlaceSprite(feet, facing, stance, down, frame, rect.size);
    const auto placement = PlaceSprite(feet + bodyOffset, facing, stance, down, frame, rect.size);
    const auto support = frame.operative ? operativeSupports_.at(frame.index) : GroundSupport{};
    const auto shadow = frame.operative ? groundPlacement.Map(support.anchor) : groundPlacement.position;
    const float footprintWidth = std::max(stance == Stance::Prone || down ? 34.f : 24.f, support.width * std::abs(groundPlacement.scale.x));
    for (float y = -4.f; y <= 4.f; y += PresentationConfig::PixelSize) {
      const float width = footprintWidth * (1.f - y * y / 36.f);
      PixelRect(shadow + sf::Vector2f{-width * 0.5f, y}, {width, PresentationConfig::PixelSize}, {0, 0, 0, 80});
    }
    sf::Sprite sprite(*(frame.operative ? operativeAtlas_ : frame.prone ? proneAtlas_ : actorAtlas_), rect);
    sprite.setOrigin(placement.origin);
    sprite.setPosition(placement.position);
    sprite.setScale(placement.scale);
    const auto base = PresentationConfig::AgentUniforms[0];
    const auto tint = [&](std::uint8_t value, std::uint8_t reference) { return static_cast<std::uint8_t>(std::clamp(value * 255.f / reference, 80.f, 255.f)); };
    sprite.setColor(guard ? sf::Color{245, 200, 175} : sf::Color{tint(uniform.r, base.r), tint(uniform.g, base.g), tint(uniform.b, base.b)});
    if (frame.operative && !guard) {
      const auto largest = std::max({uniform.r, uniform.g, uniform.b});
      const auto channel = [largest](std::uint8_t value) { return static_cast<std::uint8_t>(160 + value * 95 / std::max(1, static_cast<int>(largest))); };
      sprite.setColor({channel(uniform.r), channel(uniform.g), channel(uniform.b)});
    }
    if (down) sprite.setColor({180, 180, 180});
    if (articulated && HasWeaponLayer(*articulated) && !articulated->cover.active && frame.operative) {
      // La cintura une dos capas del atlas: las piernas siguen WASD y el torso conserva su puntería independiente.
      const auto pivot=TorsoPivot(stance); const auto upperFrame=SelectWeaponSprite(*articulated);
      const auto upperRect=operativeFrames_.at(upperFrame.index);
      const auto upperPlacement=PlaceSprite(feet+bodyOffset,WeaponFacing(*articulated),stance,false,upperFrame,upperRect.size);
      ActorLayer lower(*operativeAtlas_,rect,placement,pivot,TorsoForward(stance,facing),false,sprite.getColor());
      ActorLayer upper(*operativeAtlas_,upperRect,upperPlacement,pivot,TorsoForward(stance,WeaponFacing(*articulated)),true,sprite.getColor());
      if (stance == Stance::Prone) { ctx_.renderer.Draw(upper); ctx_.renderer.Draw(lower); }
      else { ctx_.renderer.Draw(lower); ctx_.renderer.Draw(upper); }
    } else ctx_.renderer.Draw(sprite);
  }
  void WorldRenderer::CoverWeapon(const Agent &agent, sf::Vector2f feet, sf::Color uniform) const {
    if (!HasWeaponLayer(agent)) return;
    const auto muzzle=CoverMuzzle(agent,feet), shoulder=feet-sf::Vector2f{0.f,WeaponHeight(agent.stance)+4.f};
    const auto forward=(muzzle-shoulder).normalized(); const sf::Vector2f side{-forward.y,forward.x};
    const auto grip=muzzle-forward*12.f, elbow=shoulder-forward*3.f+side*8.f;
    PixelLine(shoulder,elbow,PresentationConfig::Outline,6.f); PixelLine(elbow,grip,PresentationConfig::Outline,6.f);
    PixelLine(shoulder,elbow,uniform,4.f); PixelLine(elbow,grip,uniform,4.f);
    PixelLine(grip-forward*3.f,muzzle,PresentationConfig::Outline,5.f);
    PixelLine(grip-forward*3.f,muzzle-forward*2.f,{83,91,88},3.f);
    PixelLine(grip,grip-side*5.f,{38,44,42},3.f);
    PixelRect(grip-sf::Vector2f{2.f,2.f},{4.f,4.f},PresentationConfig::Skin);
  }
  void WorldRenderer::AgentArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, bool local, bool deferCqc) const {
    using namespace PresentationConfig;
    const bool entering = AnimateHideoutEntry(world, agent, local);
    if (agent.extracted || (agent.hidden && !entering)) return;
    const float hideProgress = entering ? std::clamp(1.f - agent.actionPoseTime / GameplayConfig::StanceTransitionDuration, 0.f, 1.f) : 1.f;
    const auto position = entering ? agent.hideoutExit + (agent.position - agent.hideoutExit) * hideProgress : agent.position;
    const auto groundFeet = GroundToScreen(position, camera);
    auto feet = groundFeet;
    if (agent.cqcHeldBy != 0) {
      const auto holder = std::ranges::find_if(world.agents, [&](const Agent &other) { return other.id == agent.cqcHeldBy; });
      if (holder != world.agents.end()) feet += sf::Vector2f{0.f, CqcGripSway * holder->cqcProgress};
    }
    auto uniform = AgentUniforms[(agent.id - 1) % AgentUniforms.size()];
    if (!agent.connected || agent.hidden) uniform = Shade(uniform, 0.55f);
    Humanoid(groundFeet, agent.facing, agent.stance, uniform, AgentFrame(agent, world, local), !agent.alive || agent.incapacitated, false, feet - groundFeet, &agent);
    if (agent.cover.active) CoverWeapon(agent,feet,uniform);
    if (agent.cover.active && agent.actionPose == ActionPose::KnockWall && agent.actionPoseTime > 0.f) {
      const auto contact = GroundToScreen(WorldGeometry::ClosestPoint(agent.cover.anchor, agent.cover.surface), camera) - sf::Vector2f{0.f, CombatGeometry::TorsoCenterHeight(agent.stance)};
      const sf::Vector2f tangent{-agent.cover.normal.y, agent.cover.normal.x};
      const auto shoulder = GroundToScreen(agent.position + tangent * 4.f, camera) - sf::Vector2f{0.f, CombatGeometry::TorsoCenterHeight(agent.stance) + 4.f};
      const float phase = std::clamp(1.f - agent.actionPoseTime / GameplayConfig::AttackAnimationDuration, 0.f, 1.f);
      const auto wrist = contact + (shoulder - contact) * (0.35f * std::sin(phase * std::numbers::pi_v<float>));
      const sf::Vector2f elbow{shoulder.x, wrist.y};
      const sf::Color sleeve{58, 67, 54}, seam{87, 95, 77}, glove{35, 41, 35}, outline{16, 20, 17};
      PixelLine(shoulder, elbow, outline, 3.f); PixelLine(elbow, wrist, outline, 3.f);
      PixelLine(shoulder, elbow, sleeve, 2.f); PixelLine(elbow, wrist, sleeve, 2.f);
      PixelLine(shoulder - sf::Vector2f{1.f,1.f}, elbow - sf::Vector2f{1.f,1.f}, seam, 1.f);
      PixelRect(wrist - sf::Vector2f{1.f,1.f}, {2.f,2.f}, glove);
    }
    if (!deferCqc && agent.alive && !agent.incapacitated) CqcArt(agent, world, camera, feet, uniform);
    BodyStatus(agent,camera);
    if (local || agent.hidden || !agent.connected) {
      Label(!agent.connected ? "SIN CONEXIÓN" : (agent.hidden ? "OCULTO" : agent.name), feet - sf::Vector2f{0.f, 72.f}, local ? Green : Light);
    }
    if (agent.draggingGuard >= 0) PixelLine(feet, feet - sf::Vector2f{18.f, 8.f}, Amber, 2.f);
  }

  void WorldRenderer::GuardArt(const Guard &guard, const WorldState &world, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    if (guard.bodyHidden || guard.storedIn != 0) return;
    if (guard.trainingDummy) { DummyArt(guard, world, camera); return; }
    const auto groundFeet = GroundToScreen(guard.position, camera);
    auto feet = groundFeet;
    const bool body = guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated;
    const float fall = std::clamp((GameplayConfig::GuardIncapacityDuration - guard.incapacityRemaining) / CqcFallDuration, 0.f, 1.f);
    const bool falling = guard.state == GuardState::Incapacitated && fall < 0.5f;
    const bool down = (body && !falling) || (guard.heldBy == 0 && (guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained));
    const sf::Vector2f forward{(guard.facing.x - guard.facing.y) * GroundProjection.x, (guard.facing.x + guard.facing.y) * GroundProjection.y};
    if (!down && guard.staggerRemaining > 0.f) {
      const float reaction = std::sin(std::clamp(guard.staggerRemaining / CqcConfig::KickStagger, 0.f, 1.f) * std::numbers::pi_v<float>);
      feet -= forward * (CqcRecoil * reaction);
    }
    if (guard.heldBy != 0) {
      const auto holder = std::ranges::find_if(world.agents, [&](const Agent &agent) { return agent.id == guard.heldBy; });
      if (holder != world.agents.end()) feet += sf::Vector2f{0.f, CqcGripSway * holder->cqcProgress};
    }
    if (falling) feet += forward * (CqcRecoil * fall * 2.f) + sf::Vector2f{0.f, ActorHeight * fall * 0.25f};
    Humanoid(groundFeet, guard.facing, Stance::Standing, {126, 99, 77}, GuardFrame(guard, world), down, true, feet - groundFeet);
    if (guard.meleeAttack && guard.attackTime > 0.f && !down) {
      const auto target = GroundToScreen(guard.position + guard.facing * GameplayConfig::GuardAttackDistance, camera) - sf::Vector2f{0.f, ShotHeight};
      PixelLine(feet - sf::Vector2f{0.f, ShotHeight}, target, Amber, 4.f);
      PixelLine(target - sf::Vector2f{4.f, 6.f}, target + sf::Vector2f{4.f, 6.f}, Light);
    }
    if (body) PixelRect(feet + sf::Vector2f{-2.f, -6.f}, {6.f, 4.f}, guard.state == GuardState::Dead ? Red : Amber);
  }

  void WorldRenderer::DummyArt(const Guard &dummy, const WorldState &world, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    const auto base = GroundToScreen(dummy.position, camera);
    sf::Vector2f sway;
    if (dummy.staggerRemaining > 0.f) {
      const float reaction = std::sin(std::clamp(dummy.staggerRemaining / CqcConfig::KickStagger, 0.f, 1.f) * std::numbers::pi_v<float>);
      sway = {(dummy.facing.x - dummy.facing.y) * CqcRecoil * reaction, CqcRecoil * reaction};
    }
    if (dummy.heldBy != 0) {
      const auto holder = std::ranges::find_if(world.agents, [&](const Agent &agent) { return agent.id == dummy.heldBy; });
      if (holder != world.agents.end()) sway.y += CqcGripSway * holder->cqcProgress;
    }
    const auto feet = base + sway;
    const float verticalScale = DummyHeight / 76.f;
    const auto at = [&](float x, float y) { return feet + sf::Vector2f{x, y * verticalScale}; };
    Box({dummy.position - sf::Vector2f{12.f, 12.f}, {24.f, 24.f}}, 5.f, camera, WallTop, WallLeft, WallRight);
    PixelLine(base - sf::Vector2f{0.f, 5.f}, at(0.f, -68.f), Shade(DummyWood, 0.65f), 7.f);
    PixelLine(at(-28.f, -42.f), at(28.f, -42.f), DummyWood, 8.f);
    Polygon({at(-14.f, -58.f), at(14.f, -58.f), at(18.f, -40.f), at(11.f, -17.f), at(-11.f, -17.f), at(-18.f, -40.f)}, DummyCloth);
    PixelLine(at(-12.f, -57.f), at(12.f, -57.f), DummyTarget, 3.f);
    PixelRect(at(-10.f, -76.f), {20.f, 16.f * verticalScale}, DummyWood);
    PixelRect(at(-7.f, -73.f), {14.f, 10.f * verticalScale}, DummyCloth);
    const auto target = at(0.f, -38.f);
    for (const float radius : {13.f, 8.f}) {
      PixelLine(target + sf::Vector2f{-radius, 0.f}, target + sf::Vector2f{0.f, -radius}, DummyTarget);
      PixelLine(target + sf::Vector2f{0.f, -radius}, target + sf::Vector2f{radius, 0.f}, DummyTarget);
      PixelLine(target + sf::Vector2f{radius, 0.f}, target + sf::Vector2f{0.f, radius}, DummyTarget);
      PixelLine(target + sf::Vector2f{0.f, radius}, target + sf::Vector2f{-radius, 0.f}, DummyTarget);
    }
    PixelLine(target - sf::Vector2f{4.f, 0.f}, target + sf::Vector2f{4.f, 0.f}, Outline);
    PixelLine(target - sf::Vector2f{0.f, 4.f}, target + sf::Vector2f{0.f, 4.f}, Outline);
    if (dummy.trainingFeedback > 0.f) {
      auto impact = Amber; impact.a = static_cast<std::uint8_t>(std::clamp(dummy.trainingFeedback, 0.f, 1.f) * 220.f);
      PixelLine(target + sf::Vector2f{-17.f, -7.f}, target + sf::Vector2f{-12.f, -3.f}, impact);
      PixelLine(target + sf::Vector2f{12.f, 3.f}, target + sf::Vector2f{17.f, 7.f}, impact);
    }
  }

  void WorldRenderer::DummyStatus(const Guard &dummy, const WorldState &world, sf::Vector2f camera, const Agent *observer) const {
    if (!DummyHealthVisible(dummy, world, observer)) return;
    using namespace PresentationConfig;
    const auto center = ProjectGround(dummy.position, camera, ctx_.window.GetLogicalSize(), zoom_) - sf::Vector2f{0.f, DummyHeight * zoom_ + IconOffset};
    const auto origin = center - sf::Vector2f{DummyHealthBarSize.x * 0.5f, 0.f};
    PixelRect(origin - sf::Vector2f{3.f, 3.f}, DummyHealthBarSize + sf::Vector2f{6.f, 6.f}, Outline);
    PixelRect(origin, DummyHealthBarSize, WallLeft);
    const float fraction = std::clamp(dummy.health / CombatConfig::MaxGuardHealth, 1.f / CombatConfig::MaxGuardHealth, 1.f);
    PixelRect(origin, {DummyHealthBarSize.x * fraction, DummyHealthBarSize.y}, fraction > 0.3f ? Green : Amber);
    Label("DIANA " + std::to_string(static_cast<int>(std::max(1.f, dummy.health))) + " / " + std::to_string(dummy.trainingHits) + " IMPACTOS",
        center - sf::Vector2f{0.f, 22.f}, DummyTarget, MinimapTextSize);
  }

  void WorldRenderer::CqcArt(const Agent &agent, const WorldState &world, sf::Vector2f camera, sf::Vector2f feet, sf::Color uniform) const {
    using namespace PresentationConfig;
    const auto visual = CqcPoseFor(agent);
    if (!visual.gripping) return;
    sf::Vector2f forward{(agent.facing.x - agent.facing.y) * GroundProjection.x, (agent.facing.x + agent.facing.y) * GroundProjection.y};
    if (forward.lengthSquared() <= 0.f) return;
    forward /= forward.length();
    const sf::Vector2f side{-forward.y, forward.x};
    if (visual.gripping) {
      const auto viewer = std::find_if(world.agents.begin(), world.agents.end(), [camera](const Agent &candidate) { return candidate.position == camera; });
      bool targetVisible = false;
      sf::Vector2f neck = feet + forward * CqcArmReach - sf::Vector2f{0.f, ActorHeight * 0.78f};
      for (const auto &guard : world.guards) if (guard.id == agent.cqcHoldingGuard && guard.heldBy == agent.id && !guard.bodyHidden && guard.storedIn == 0 &&
          viewer != world.agents.end() && Visibility::GuardVisible(world, *viewer, guard) && CqcOverlayClear(world, agent, guard.position)) {
        targetVisible = true; neck = GroundToScreen(guard.position, camera) - sf::Vector2f{0.f, ActorHeight * 0.78f - CqcGripSway * agent.cqcProgress};
      }
      for (const auto &other : world.agents) if (other.id == agent.cqcHoldingAgent && other.cqcHeldBy == agent.id && !other.hidden &&
          viewer != world.agents.end() && (other.id == viewer->id || Visibility::IsVisible(world, *viewer, other.position, other.stance)) && CqcOverlayClear(world, agent, other.position)) {
        targetVisible = true; neck = GroundToScreen(other.position, camera) - sf::Vector2f{0.f, ActorHeight * 0.78f};
      }
      if (!targetVisible) return;
      const auto shoulder = feet - sf::Vector2f{0.f, ActorHeight * 0.7f};
      const auto squeeze = side * (CqcGripSway * visual.extension);
      PixelLine(shoulder - side * 5.f, neck - side * 5.f + squeeze, uniform, 6.f);
      PixelLine(neck - side * 5.f + squeeze, neck + side * 5.f - squeeze, Skin, 4.f);
      PixelLine(shoulder + side * 5.f, neck + side * 5.f - squeeze, Shade(uniform, 0.75f), 5.f);
      return;
    }
  }

  void WorldRenderer::ExplosionArt(const Interactable &object, const WorldState &world, sf::Vector2f camera) const {
    if (object.kind != ObjectKind::Explosive || object.trap.state != TrapState::Detonated || object.trap.effectRemaining <= 0.f) return;
    const float life = std::clamp(object.trap.effectRemaining / DevicesConfig::ExplosionEffectDuration, 0.f, 1.f);
    const float radius = object.trap.blastRadius * (1.f - life * 0.7f);
    sf::VertexArray burst(sf::PrimitiveType::Triangles);
    const auto center = GroundToScreen(object.position, camera) - sf::Vector2f{0.f, 4.f};
    for (int ray = 0; ray < PresentationConfig::NoiseCircleSegments; ++ray) {
      const auto endpoint = [&](int index) {
        const float angle = 2.f * std::numbers::pi_v<float> * index / PresentationConfig::NoiseCircleSegments;
        return GroundToScreen(RayEnd(object.position, sf::Vector2f{std::cos(angle), std::sin(angle)} * radius, world, true, object.id), camera);
      };
      auto edge = PresentationConfig::Red; edge.a = static_cast<std::uint8_t>(life * 80.f);
      auto core = PresentationConfig::Amber; core.a = static_cast<std::uint8_t>(life * 220.f);
      burst.append({center, core}); burst.append({endpoint(ray), edge}); burst.append({endpoint(ray + 1), edge});
    }
    ctx_.renderer.Draw(burst);
    PixelRect(center - sf::Vector2f{4.f * life, 12.f * life}, {8.f * life, 16.f * life}, PresentationConfig::Light);
  }

  void WorldRenderer::GuardIcon(const Guard &guard, sf::Vector2f camera) const {
    using namespace PresentationConfig;
    const auto indicator = IndicatorFor(guard);
    if (indicator == GuardIndicator::None) return;
    const bool body = indicator == GuardIndicator::Unconscious || indicator == GuardIndicator::Dead;
    const float height = body ? CombatConfig::ProneHeight : ActorHeight;
    const auto center = ProjectGround(guard.position, camera, ctx_.window.GetLogicalSize(), zoom_) - sf::Vector2f{0.f, height * zoom_ + IconOffset};
    const auto at = [&](float x, float y) { return center + sf::Vector2f{x, y}; };
    if (body) { BodyStatusGlyph(center, indicator); return; }
    EyeGlyph(center, GuardDetectionFor(guard), GuardEyeScale, false);
    if (indicator == GuardIndicator::Searching) Label("?", at(21.f, -12.f), Amber, 16);
    if (indicator == GuardIndicator::Confirmed || indicator == GuardIndicator::Combat) {
      PixelRect(at(17.f, -12.f), {9.f, 24.f}, Outline);
      PixelRect(at(20.f, -9.f), {3.f, 11.f}, indicator == GuardIndicator::Confirmed ? Cyan : Red);
      PixelRect(at(20.f, 6.f), {3.f, 3.f}, Red);
    }
  }

  void WorldRenderer::BodyStatus(const Agent &agent, sf::Vector2f camera) const {
    const auto indicator = BodyIndicatorFor(agent);
    if (indicator == GuardIndicator::None) return;
    const auto center = ProjectGround(agent.position, camera, ctx_.window.GetLogicalSize(), zoom_) -
      sf::Vector2f{0.f, CombatConfig::ProneHeight * zoom_ + PresentationConfig::IconOffset};
    ctx_.renderer.ResetView();
    BodyStatusGlyph(center, indicator);
    ctx_.renderer.SetView(WorldView(ctx_.window.GetLogicalSize(), zoom_));
  }

  void WorldRenderer::BodyStatusGlyph(sf::Vector2f center, GuardIndicator indicator) const {
    using namespace PresentationConfig;
    const auto &glyph = indicator == GuardIndicator::Dead ? DeadGlyph : UnconsciousGlyph;
    const sf::Vector2f size{BodyIconWidth * BodyIconPixelSize, BodyIconHeight * BodyIconPixelSize};
    const auto origin = SnapToPixels(center - size * 0.5f);
    PixelRect(origin - sf::Vector2f{2.f, 2.f}, size + sf::Vector2f{4.f, 4.f}, Outline);
    sf::VertexArray pixels(sf::PrimitiveType::Triangles);
    for (std::size_t row = 0; row < glyph.size(); ++row) for (std::size_t column = 0; column < glyph[row].size(); ++column) {
      const char value = glyph[row][column];
      if (value == '.') continue;
      const auto color = value == 'S' ? Skin : value == 'L' ? Light : value == 'R' ? Red : value == 'X' ? Outline : Violet;
      const auto point = origin + sf::Vector2f{static_cast<float>(column), static_cast<float>(row)} * BodyIconPixelSize;
      const sf::Vector2f right{BodyIconPixelSize, 0.f}, down{0.f, BodyIconPixelSize};
      for (const auto vertex : {point, point + right, point + right + down, point, point + right + down, point + down}) pixels.append({vertex, color});
    }
    ctx_.renderer.Draw(pixels);
  }

  void WorldRenderer::CameraIcon(const Interactable &object, sf::Vector2f camera) const {
    if (!object.active || object.camera.state != CameraState::Operating || WorldGeometry::Properties(object).decorative) return;
    Agent observed;
    observed.detectionProgress = object.camera.indicatorProgress;
    observed.detectionConfirmed = object.camera.confirmedRemaining > 0.f;
    const auto center = ProjectGround(object.position, camera, ctx_.window.GetLogicalSize(), zoom_) - sf::Vector2f{0.f, 56.f * zoom_ + PresentationConfig::IconOffset};
    EyeGlyph(center, DetectionFor(observed), PresentationConfig::GuardEyeScale, false);
    if (object.camera.perception == CameraPerception::Searching) Label("?", center + sf::Vector2f{21.f, -12.f}, PresentationConfig::Amber, 16);
  }

  void WorldRenderer::Vision(const Guard &guard, const WorldState &world, sf::Vector2f camera) const {
    if (guard.trainingDummy || guard.bodyHidden || guard.storedIn != 0 || guard.state == GuardState::Dead || guard.state == GuardState::Incapacitated || guard.cqcState != CqcState::None) return;
    auto color = guard.state == GuardState::Alert ? PresentationConfig::Red : PresentationConfig::Amber;
    color.a = guard.state == GuardState::Alert ? 34 : 20;
    FieldOfView(guard.position, guard.facing, GameplayConfig::VisionDistance * (world.alarm ? GameplayConfig::AlarmVisionMultiplier : 1.f), color, world, camera);
  }

  void WorldRenderer::FieldOfView(sf::Vector2f position, sf::Vector2f facing, float range, sf::Color color, const WorldState &world, sf::Vector2f camera, float cosHalfAngle) const {
    const float angle = std::atan2(facing.y, facing.x);
    const float halfAngle = std::acos(std::clamp(cosHalfAngle, -1.f, 1.f));
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
    if (noise.remaining <= 0.f || noise.duration <= 0.f) return;
    const float fraction = std::clamp(noise.remaining / std::max(0.001f, noise.duration), 0.f, 1.f);
    sf::VertexArray ring(sf::PrimitiveType::Lines);
    for (int wave = 0; wave < PresentationConfig::NoiseWaveCount; ++wave) {
      const float progress = 1.f - fraction - static_cast<float>(wave) * PresentationConfig::NoiseWaveLag;
      if (progress <= 0.f || progress >= 1.f) continue;
      auto color = PresentationConfig::Amber;
      color.a = static_cast<std::uint8_t>((1.f - progress) * 110.f);
      const float radius = noise.radius * progress;
      for (int step = 0; step < PresentationConfig::NoiseCircleSegments; ++step) {
        for (int endpoint = 0; endpoint <= 1; ++endpoint) {
          const float angle = 2.f * std::numbers::pi_v<float> * (step + endpoint) / PresentationConfig::NoiseCircleSegments;
          ring.append({GroundToScreen(noise.position + sf::Vector2f{std::cos(angle), std::sin(angle)} * radius, camera), color});
        }
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
      const auto visible = [&](sf::Vector2f point) { return !IsTactical(world) || (observer && Visibility::IsVisible(world, *observer, point)); };
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
      DecoyProjectile arc; arc.from = trajectory.from; arc.to = trajectory.to; arc.duration = 1.f; arc.height = DistractionConfig::DecoyArcHeight;
      arc.elapsed = static_cast<float>(segment) / segments;
      const auto first = GroundToScreen(from, camera) - sf::Vector2f{0.f, DistractionGeometry::DecoyHeight(arc)};
      arc.elapsed = (static_cast<float>(segment) + 0.65f) / segments;
      const auto last = GroundToScreen(to, camera) - sf::Vector2f{0.f, DistractionGeometry::DecoyHeight(arc)};
      if (visible(from) && visible(to)) PixelLine(first, last, color);
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

  void WorldRenderer::DecoyArt(const DecoyProjectile &disc, sf::Vector2f camera) const {
    const auto ground = GroundToScreen(DistractionGeometry::DecoyPosition(disc), camera);
    const auto center = ground - sf::Vector2f{0.f, DistractionGeometry::DecoyHeight(disc)};
    const float radius = PresentationConfig::DecoyDiscRadius;
    Polygon({ground + sf::Vector2f{-radius, 0.f}, ground + sf::Vector2f{0.f, -radius * 0.45f}, ground + sf::Vector2f{radius, 0.f}, ground + sf::Vector2f{0.f, radius * 0.45f}}, {0, 0, 0, 90});
    Polygon({center + sf::Vector2f{-radius, 0.f}, center + sf::Vector2f{0.f, -radius * 0.5f}, center + sf::Vector2f{radius, 0.f}, center + sf::Vector2f{0.f, radius * 0.5f}}, PresentationConfig::Amber);
    PixelLine(center - sf::Vector2f{radius * 0.6f, 0.f}, center + sf::Vector2f{radius * 0.6f, 0.f}, PresentationConfig::Light);
  }

  void WorldRenderer::AimLaser(const WorldState &world, sf::Vector2f camera, const Agent *observer) const {
    if (!observer || !observer->aiming || !observer->alive || observer->incapacitated || observer->hidden || observer->extracted || observer->reloadRemaining > 0.f) return;
    const auto ray = LaserFor(world, *observer);
    const auto muzzle = SourceMuzzle(world, ray, camera, observer);
    if (!muzzle) return;
    const auto from = *muzzle;
    const auto to = GroundToScreen(ray.to, camera) - sf::Vector2f{0.f, ray.toHeight};
    constexpr unsigned int segments = 32;
    for (unsigned int index = 0; index < segments; ++index) {
      const float first = static_cast<float>(index) / segments, last = static_cast<float>(index + 1) / segments;
      if (!Visibility::IsGeometricallyVisible(world, *observer, ray.from + (ray.to - ray.from) * last)) break;
      PixelLine(from + (to - from) * first, from + (to - from) * last, {221, 67, 77, 85}, 1.f);
    }
    if (!Visibility::IsGeometricallyVisible(world, *observer, ray.to)) return;
    const float size = PresentationConfig::AimReticleSize;
    PixelLine(to - sf::Vector2f{size, 0.f}, to + sf::Vector2f{size, 0.f}, PresentationConfig::Red);
    PixelLine(to - sf::Vector2f{0.f, size}, to + sf::Vector2f{0.f, size}, PresentationConfig::Red);
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
    Label(IsTraining(world) ? "PLANO / ENTRENAMIENTO" : world.phase == GamePhase::Base ? "PLANO / BASE CIO" : "PLANO / OPERACIÓN", MinimapPosition + sf::Vector2f{120.f, 4.f}, Light, MinimapTextSize);
    const auto player = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
    const bool fog = world.phase == GamePhase::Mission;
    const auto visible = [&](sf::Vector2f point) { return !fog || (player != world.agents.end() && Visibility::IsVisible(world, *player, point)); };
    PixelRect(origin, MinimapGroundSize, {22, 33, 42});
    if (fog) ctx_.renderer.Draw(FogMask(world, visibility, map));
    for (const auto &obstacle : world.obstacles) {
      if (fog && (player == world.agents.end() || !FootprintVisible(world, *player, obstacle.bounds))) continue;
      PixelRect(map(obstacle.bounds.position), obstacle.bounds.size.componentWiseMul(scale),
                obstacle.cover == CoverKind::Low ? CrateTop : obstacle.cover == CoverKind::Hide ? sf::Color{38, 76, 62} : WallTop);
    }
    const auto markerColor = [](ObjectKind kind) {
      switch (kind) {
      case ObjectKind::Terminal: case ObjectKind::Operations: case ObjectKind::Computer: return Cyan;
      case ObjectKind::Secondary: case ObjectKind::Armory: return Amber;
      case ObjectKind::CoopSwitch: case ObjectKind::Intelligence: return Violet;
      case ObjectKind::Extraction: case ObjectKind::Deployment: return Green;
      case ObjectKind::Camera: case ObjectKind::AlarmPanel: case ObjectKind::CameraPanel: case ObjectKind::Explosive: return Red;
      case ObjectKind::TrainingReset: return Green;
      default: return Light;
      }
    };
    for (const auto &object : world.objects) {
      if (WorldGeometry::Properties(object).decorative) continue;
      if (object.kind == ObjectKind::Explosive && object.trap.state == TrapState::Armed) continue;
      if (fog && (player == world.agents.end() || !Visibility::ObjectVisible(world, *player, object))) continue;
      const auto point = map(object.position);
      PixelRect(point - sf::Vector2f{4.f, 4.f}, {8.f, 8.f}, Outline);
      PixelRect(point - sf::Vector2f{2.f, 2.f}, {4.f, 4.f}, object.completed || !object.active ? Shade(markerColor(object.kind), 0.45f) : markerColor(object.kind));
      if (object.kind == ObjectKind::PuzzleNode) Label(std::string(1, static_cast<char>('A' + std::clamp(static_cast<int>(object.puzzle.nodeIndex), 1, 4) - 1)), point + sf::Vector2f{0.f, -15.f}, object.open ? Green : Amber, MinimapTextSize);
    }
    for (const auto &guard : world.guards) {
      if (guard.bodyHidden || guard.storedIn != 0 || (fog && (player == world.agents.end() || !Visibility::GuardVisible(world, *player, guard)))) continue;
      PixelRect(map(guard.position) - sf::Vector2f{2.f, 2.f}, {4.f, 4.f}, guard.trainingDummy ? DummyTarget : guard.state == GuardState::Incapacitated ? Amber : guard.state == GuardState::Dead ? WallTop : Red);
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
    const std::array<std::string, 4> names = IsTraining(world) ? std::array<std::string, 4>{"MUÑECOS", "DISPOSITIVOS", "ESCONDITES", "REINICIAR"} : base ? std::array<std::string, 4>{"OPERACIONES", "ARMERÍA", "INTELIGENCIA", "DESPLIEGUE"}
                                               : std::array<std::string, 4>{"PRINCIPAL", "SECUNDARIO", "REPETIDOR", "EXTRACCIÓN"};
    for (std::size_t index = 0; index < names.size(); ++index) {
      const auto position = MinimapPosition + sf::Vector2f{12.f + static_cast<float>(index % 2) * 118.f, 151.f + static_cast<float>(index / 2) * 19.f};
      PixelRect(position + sf::Vector2f{0.f, 4.f}, {6.f, 6.f}, colors[index]);
      Label(names[index], position + sf::Vector2f{60.f, 0.f}, colors[index], MinimapTextSize);
    }
  }

  void WorldRenderer::DetectionEye(const WorldState &world, const Agent *observer) const {
    if (!IsTactical(world) || !observer || observer->extracted) return;
    const auto visual = SecurityEyeFor(world);
    const auto center = PresentationConfig::DetectionHudCenter;
    Polygon({center + sf::Vector2f{-34.f, -26.f}, center + sf::Vector2f{34.f, -26.f}, center + sf::Vector2f{38.f, -22.f},
        center + sf::Vector2f{38.f, 22.f}, center + sf::Vector2f{34.f, 26.f}, center + sf::Vector2f{-34.f, 26.f},
        center + sf::Vector2f{-38.f, 22.f}, center + sf::Vector2f{-38.f, -22.f}}, PresentationConfig::Outline);
    EyeGlyph(center, visual, 1.f, true);
  }

  void WorldRenderer::EyeGlyph(sf::Vector2f center, DetectionVisual visual, float scale, bool brackets) const {
    if (!brackets) PixelRect(center - sf::Vector2f{32.f, 24.f} * scale, sf::Vector2f{64.f, 48.f} * scale, PresentationConfig::Outline);
    const auto at = [center, scale](sf::Vector2f offset) { return center + offset * scale; };
    const std::array<sf::Vector2f, PresentationConfig::DetectionEyeSegments> contour{{{-24.f, 0.f}, {-20.f, -6.f}, {-14.f, -10.f}, {-8.f, -14.f},
        {0.f, -16.f}, {8.f, -14.f}, {14.f, -10.f}, {20.f, -6.f}, {24.f, 0.f}, {20.f, 6.f}, {14.f, 10.f}, {8.f, 14.f},
        {0.f, 16.f}, {-8.f, 14.f}, {-14.f, 10.f}, {-20.f, 6.f}}};
    for (std::size_t segment = 0; segment < contour.size(); ++segment) {
      const auto color = segment < visual.segments ? visual.color : sf::Color{69, 86, 98, 100};
      PixelLine(at(contour[segment]), at(contour[(segment + 1) % contour.size()]), color, 2.f);
    }
    const auto iris = visual.segments > 0 ? visual.color : sf::Color{69, 86, 98, 100};
    PixelRect(at({-6.f, -8.f}), sf::Vector2f{12.f, 16.f} * scale, iris);
    PixelRect(at({-8.f, -4.f}), sf::Vector2f{16.f, 8.f} * scale, iris);
    PixelRect(at({-2.f, -6.f}), sf::Vector2f{4.f, 12.f} * scale, visual.confirmed ? PresentationConfig::Light : PresentationConfig::Outline);
    if (brackets && visual.confirmed) for (const float side : {-1.f, 1.f}) {
      const auto corner = at({side * 32.f, -20.f});
      PixelLine(corner, corner - sf::Vector2f{side * 8.f, 0.f}, PresentationConfig::Red, 2.f);
      PixelLine(corner, corner + sf::Vector2f{0.f, 10.f}, PresentationConfig::Red, 2.f);
      PixelLine(corner + sf::Vector2f{0.f, 40.f}, corner + sf::Vector2f{-side * 8.f, 40.f}, PresentationConfig::Red, 2.f);
      PixelLine(corner + sf::Vector2f{0.f, 40.f}, corner + sf::Vector2f{0.f, 30.f}, PresentationConfig::Red, 2.f);
    }
  }

  void WorldRenderer::RescueIndicators(const WorldState &world, const Agent *observer) const {
    if (!observer || world.phase != GamePhase::Mission) return;
    if (observer->incapacitated) {
      Label(std::format("RESCATE NECESARIO  /  {:.0f} s", std::ceil(observer->incapacityRemaining)), {800.f, 190.f}, PresentationConfig::Red, 26);
    }
    std::vector<RescueSignal> signals = world.rescueSignals;
    if (!world.visibility.authoritative && observer->privateOrder != PrivateOrder::Traitor) for (const auto &ally : world.agents) {
      if (ally.id == observer->id || !ally.connected || !ally.incapacitated || ally.privateOrder == PrivateOrder::Traitor) continue;
      const auto delta = ally.position - observer->position;
      signals.push_back({ally.id, delta.lengthSquared() > 0.001f ? delta / delta.length() : sf::Vector2f{1.f,0.f}, ally.incapacityRemaining});
    }
    for (const auto &signal : signals) {
      sf::Vector2f direction{(signal.direction.x - signal.direction.y) * PresentationConfig::GroundProjection.x,
        (signal.direction.x + signal.direction.y) * PresentationConfig::GroundProjection.y};
      if (direction.lengthSquared() < 0.001f) continue;
      direction /= direction.length();
      const sf::Vector2f side{-direction.y, direction.x}, center{800.f, 450.f};
      const float edge = std::min(650.f / std::max(.001f,std::abs(direction.x)), 235.f / std::max(.001f,std::abs(direction.y)));
      const auto tip = center + direction * edge;
      Polygon({tip, tip - direction * 28.f + side * 13.f, tip - direction * 20.f, tip - direction * 28.f - side * 13.f}, PresentationConfig::Amber);
      Label(std::format("{}  /  {:.0f} s", signal.agent, std::ceil(signal.remaining)), tip + side * 42.f, PresentationConfig::Amber, 18);
    }
  }

  void WorldRenderer::Render(const WorldState &world, PlayerId localPlayer, bool hudVisible) const {
    ctx_.renderer.ResetView();
    PixelRect({}, ctx_.window.GetLogicalSize(), PresentationConfig::Void);
    ctx_.renderer.SetView(WorldView(ctx_.window.GetLogicalSize(), zoom_));
    const auto camera = CameraGround(world, localPlayer);
    const auto player = std::find_if(world.agents.begin(), world.agents.end(), [localPlayer](const Agent &agent) { return agent.id == localPlayer; });
    const Agent *observer = player == world.agents.end() ? nullptr : &*player;
    const bool fog = world.phase == GamePhase::Mission;
    const auto visible = [&](sf::Vector2f point, Stance stance = Stance::Standing) { return !fog || (observer && Visibility::IsVisible(world, *observer, point, stance)); };
    const auto objectVisible = [&](const Interactable &object) { return (object.kind != ObjectKind::Explosive || object.trap.state != TrapState::Armed) && (!fog || (observer && Visibility::ObjectVisible(world, *observer, object))); };
    const auto guardVisible = [&](const Guard &guard) { return !guard.bodyHidden && guard.storedIn == 0 && (!fog || (observer && Visibility::GuardVisible(world, *observer, guard))); };
    const auto obstacleVisible = [&](const Obstacle &obstacle) { return !fog || (observer && FootprintVisible(world, *observer, obstacle.bounds)); };
    const auto visibility = observer ? Visibility::BuildGrid(world, *observer) : VisibilityGrid{};
    Floor(world, camera);
    for (const auto &obstacle : world.obstacles) if (obstacle.cover == CoverKind::Hide && obstacleVisible(obstacle)) ObstacleArt(obstacle, camera);
    for (const auto &guard : world.guards) if (guardVisible(guard)) Vision(guard, world, camera);
    for (const auto &object : world.objects) {
      if (object.kind == ObjectKind::Camera && object.active && object.camera.state == CameraState::Operating && !WorldGeometry::Properties(object).decorative && objectVisible(object)) {
        FieldOfView(object.position, object.facing, object.camera.range, {221, 67, 77, 26}, world, camera, object.camera.cosHalfAngle);
      }
      if (objectVisible(object)) ExplosionArt(object, world, camera);
    }
    Fog(world, camera, visibility);
    Footsteps(world, camera, observer);
    DecoyPreview(world, camera, observer);
    AimLaser(world, camera, observer);
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
    for (std::size_t index = 0; index < world.decoys.size(); ++index) {
      const auto point = DistractionGeometry::DecoyPosition(world.decoys[index]);
      if (visible(point)) add(4, index, {point, {0.f, 0.f}});
    }
    for (std::size_t index = 0; index < world.agents.size(); ++index) {
      const auto &agent = world.agents[index];
      const bool entering = AnimateHideoutEntry(world, agent, agent.id == localPlayer);
      if ((agent.hidden && !entering) || agent.extracted) continue;
      const bool partner = observer && !agent.hidden && ((observer->cqcHoldingAgent == agent.id && agent.cqcHeldBy == observer->id) ||
          (observer->cqcHeldBy == agent.id && agent.cqcHoldingAgent == observer->id)) &&
          (agent.position - observer->position).length() <= GameplayConfig::CqcDistance * GameplayConfig::CqcMaximumStretch;
      const float phase = entering ? std::clamp(1.f - agent.actionPoseTime / GameplayConfig::StanceTransitionDuration, 0.f, 1.f) : 1.f;
      const auto point = entering ? agent.hideoutExit + (agent.position - agent.hideoutExit) * phase : agent.position;
      if (agent.id == localPlayer || partner || visible(agent.position, agent.stance)) add(3, index, {point, {0.f, 0.f}});
    }
    const auto deferCqc = [&](const Agent &agent) {
      if (!agent.alive || agent.incapacitated || agent.hidden || agent.extracted) return false;
      const auto pose = CqcPoseFor(agent);
      if (!pose.gripping && agent.cqcAttackTime <= 0.f) return false;
      const float reach = pose.gripping ? GameplayConfig::CqcDistance * GameplayConfig::CqcMaximumStretch : pose.kick ? CqcConfig::KickReach : CqcConfig::PunchReach;
      const auto eligible = [&](sf::Vector2f position) {
        const auto offset = position - agent.position;
        return offset.length() > 0.f && offset.length() <= reach && agent.facing.dot(offset) >= CqcConfig::FacingCosine * offset.length() * agent.facing.length() &&
            CqcOverlayClear(world, agent, position);
      };
      for (const auto &guard : world.guards) if (guardVisible(guard) && (!pose.gripping || (guard.id == agent.cqcHoldingGuard && guard.heldBy == agent.id)) && eligible(guard.position)) return true;
      if (agent.cqcHoldingAgent != 0) for (const auto &other : world.agents) if (other.id == agent.cqcHoldingAgent && other.cqcHeldBy == agent.id && !other.hidden &&
          visible(other.position, other.stance) && eligible(other.position)) return true;
      return false;
    };
    std::vector<std::size_t> cqcOverlays;
    for (const auto index : GroundDrawOrder(footprints)) {
      const auto &item = items[index];
      switch (item.kind) {
      case 0: ObstacleArt(world.obstacles[item.index], camera); break;
      case 1: {
        auto object = world.objects[item.index];
        if (observer && observer->hidden && observer->hideoutId == object.id && observer->actionPose == ActionPose::HideEnter && observer->actionPoseTime > 0.f &&
            WorldGeometry::HideoutType(object) == HideoutKind::Closet) {
          object.open = true;
          const float phase = std::clamp(1.f - observer->actionPoseTime / GameplayConfig::StanceTransitionDuration, 0.f, 1.f);
          object.hideoutAperture = std::sin(std::numbers::pi_v<float> * phase);
        }
        ObjectArt(object, camera, (world.phase == GamePhase::Base && !IsTraining(world)) ||
            (object.position - camera).lengthSquared() < GameplayConfig::InteractionDistance * GameplayConfig::InteractionDistance * 4.f);
        break;
      }
      case 2: GuardArt(world.guards[item.index], world, camera); break;
      case 3: {
        const bool deferred = deferCqc(world.agents[item.index]);
        AgentArt(world.agents[item.index], world, camera, world.agents[item.index].id == localPlayer, deferred);
        if (deferred) cqcOverlays.push_back(item.index);
        break;
      }
      case 4: DecoyArt(world.decoys[item.index], camera); break;
      }
    }
    for (const auto index : cqcOverlays) {
      const auto &agent = world.agents[index];
      CqcArt(agent, world, camera, GroundToScreen(agent.position, camera), PresentationConfig::AgentUniforms[(agent.id - 1) % PresentationConfig::AgentUniforms.size()]);
    }
    Combat(world, camera, observer);
    for (const auto &noise : world.noises) if (observer && Visibility::NoiseAudible(world, *observer, noise)) {
      auto perceived = noise;
      if (noise.actor != observer->id && !visible(noise.position)) {
        const float cell = AudioConfig::PositionQuantization;
        perceived.position = {std::floor(noise.position.x / cell) * cell, std::floor(noise.position.y / cell) * cell};
      }
      if (noise.actor == observer->id) perceived.position += noise.visualDirection * PresentationConfig::FootstepVisualOffset;
      Noise(perceived, camera);
    }
    ctx_.renderer.ResetView();
    for (const auto &object : world.objects) if (object.kind == ObjectKind::Camera && objectVisible(object)) CameraIcon(object, camera);
    for (const auto &guard : world.guards) if (guardVisible(guard)) GuardIcon(guard, camera);
    for (const auto &guard : world.guards) if (guardVisible(guard)) DummyStatus(guard, world, camera, observer);
    Minimap(world, localPlayer, visibility);
    if (hudVisible) DetectionEye(world, observer);
    RescueIndicators(world, observer);
  }
}
