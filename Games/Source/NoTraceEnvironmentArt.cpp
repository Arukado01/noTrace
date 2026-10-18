#include "NoTraceEnvironmentArt.h"
#include "NoTracePerformance.h"
#include "NoTraceSpatialGeometry.h"
#include "Config/NoTracePresentationConfig.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/Drawable.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/VertexArray.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace NoTrace {
  namespace {
    class TexturedQuad final : public sf::Drawable {
      const sf::Texture &texture_;
      sf::VertexArray vertices_{sf::PrimitiveType::Triangles};
      void draw(sf::RenderTarget &target, sf::RenderStates state) const override { state.texture = &texture_; target.draw(vertices_, state); }
    public:
      TexturedQuad(const sf::Texture &texture, sf::VertexArray vertices) : texture_(texture), vertices_(std::move(vertices)) {}
      TexturedQuad(const sf::Texture &texture, const std::array<sf::Vector2f, 4> &points, sf::FloatRect uv, const std::array<sf::Color, 4> &colors) : texture_(texture) {
        const auto end = uv.position + uv.size;
        const std::array<sf::Vector2f, 4> coordinates{uv.position, {end.x, uv.position.y}, end, {uv.position.x, end.y}};
        for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) vertices_.append({points[index], colors[index], coordinates[index]});
      }
    };

    std::size_t Material(SurfaceMaterial material) {
      switch (material) {
      case SurfaceMaterial::Steel: return 1;
      case SurfaceMaterial::Grate: return 2;
      case SurfaceMaterial::Industrial: return 3;
      case SurfaceMaterial::Soil: return 4;
      default: return 0;
      }
    }

    std::optional<std::size_t> SpriteFor(const Interactable &object) {
      switch (object.propStyle) {
      case PropStyle::Desk: return object.overturned ? 1u : 0u;
      case PropStyle::Locker: return object.open ? 3u : 2u;
      case PropStyle::Ammunition: return 4;
      case PropStyle::Logistics: return 5;
      case PropStyle::Waste: return 6;
      case PropStyle::Server: return 7;
      case PropStyle::Console: return 8;
      case PropStyle::Lever: return object.open ? 10u : 9u;
      case PropStyle::Buttons: return 11;
      case PropStyle::SecurityDoor: return object.open ? 13u : 12u;
      case PropStyle::Medical: return 14;
      case PropStyle::Elevator: return 15;
      case PropStyle::Chair: return 18;
      case PropStyle::Bench: return 19;
      case PropStyle::Barrels: return 20;
      case PropStyle::Glass: return 21;
      case PropStyle::Ventilation: return 22;
      case PropStyle::SafetyLamp: return 23;
      default: break;
      }
      if (WorldGeometry::HideoutType(object) == HideoutKind::UnderTable) return object.overturned ? 1u : 0u;
      if (WorldGeometry::HideoutType(object) == HideoutKind::Closet) return object.open ? 3u : 2u;
      if (WorldGeometry::HideoutType(object) == HideoutKind::TrashBin) return 6;
      switch (object.kind) {
      case ObjectKind::Operations: case ObjectKind::Intelligence: case ObjectKind::Terminal: case ObjectKind::Computer: return 8;
      case ObjectKind::Camera: return 16;
      case ObjectKind::Explosive: return object.trap.state == TrapState::Armed || object.trap.state == TrapState::Detonated ? std::nullopt : std::optional<std::size_t>{17};
      case ObjectKind::Armory: return 4;
      case ObjectKind::Secondary: return 5;
      case ObjectKind::Door: return object.open ? 13u : 12u;
      case ObjectKind::CoopSwitch: case ObjectKind::CameraPanel: case ObjectKind::AlarmPanel: case ObjectKind::TrainingReset: return 11;
      case ObjectKind::PuzzleNode: return object.open ? 10u : 9u;
      case ObjectKind::Elevator: return object.footprint.x > 32.f ? 15u : 11u;
      default: return std::nullopt;
      }
    }
  }

  EnvironmentArt::EnvironmentArt(EngineContext &context) : context_(context),
      furniture_(context.resources.FetchTexture("NoTrace/Environment/MilitaryFurniture.png")),
      materials_(context.resources.FetchTexture("NoTrace/Environment/MilitaryMaterials.png")) {
    if (!furniture_ || !materials_) throw std::runtime_error("No se encuentran los materiales y objetos militares de NO TRACE");
    furniture_->setSmooth(false); materials_->setSmooth(false);
    std::ifstream input("Content/Textures/NoTrace/Environment/MilitaryEnvironment.manifest.json");
    const auto manifest = nlohmann::json::parse(input);
    const auto &frames = manifest.at("furniture").at("frames");
    if (frames.size() != frames_.size()) throw std::runtime_error("Catálogo de mobiliario incompleto");
    for (std::size_t index = 0; index < frames_.size(); ++index) {
      const auto &entry = frames[index]; const auto &rect = entry.at("rect"), &anchor = entry.at("anchor"), &bounds = entry.at("artBounds");
      auto &frame = frames_[index]; frame.rect = {{rect[0].get<int>(), rect[1].get<int>()}, {rect[2].get<int>(), rect[3].get<int>()}};
      frame.anchor = {anchor[0].get<float>(), anchor[1].get<float>()}; frame.size = {bounds[2].get<float>(), bounds[3].get<float>()};
      frame.bottom = bounds[1].get<float>() + bounds[3].get<float>();
      if (frame.rect.position.x < 0 || frame.rect.position.y < 0 || frame.rect.size.x <= 0 || frame.rect.size.y <= 0 ||
          frame.rect.position.x + frame.rect.size.x > static_cast<int>(furniture_->getSize().x) || frame.rect.position.y + frame.rect.size.y > static_cast<int>(furniture_->getSize().y) ||
          frame.size.x <= 0.f || frame.size.y <= 0.f) throw std::runtime_error("Objeto militar fuera del atlas");
    }
  }

  void EnvironmentArt::Quad(const std::array<sf::Vector2f, 4> &points, std::size_t material, sf::Color tint, sf::Vector2f fraction) const {
    ColoredQuad(points, material, {tint, tint, tint, tint}, fraction);
  }

  void EnvironmentArt::ColoredQuad(const std::array<sf::Vector2f, 4> &points, std::size_t material, const std::array<sf::Color, 4> &colors, sf::Vector2f fraction) const {
    const sf::Vector2f uv{static_cast<float>(material % 4) * 128.f, static_cast<float>(material / 4) * 128.f};
    context_.renderer.Draw(TexturedQuad(*materials_, points, {uv, fraction * 128.f}, colors));
  }

  void EnvironmentArt::Surface(const WalkSurface &surface, const Project &project, const Shade &shade) const {
    if (!surface.enabled) return;
    const float tile = PresentationConfig::FloorTileSize;
    const float step = shade ? PresentationConfig::FogEffectStep : tile;
    const auto end = surface.bounds.position + surface.bounds.size;
    const auto material = surface.kind == WalkSurfaceKind::Stair ? 11 : Material(surface.material);
    const sf::Vector2f uv{static_cast<float>(material % 4) * 128.f, static_cast<float>(material / 4) * 128.f};
    sf::VertexArray mesh(sf::PrimitiveType::Triangles);
    for (float x = surface.bounds.position.x; x < end.x; x += step) for (float y = surface.bounds.position.y; y < end.y; y += step) {
      const auto right = std::min(x + step, end.x), bottom = std::min(y + step, end.y);
      const std::array<sf::Vector2f, 4> ground{{{x, y}, {right, y}, {right, bottom}, {x, bottom}}};
      std::array<sf::Vector2f, 4> points;
      std::array<sf::Color, 4> colors;
      for (std::size_t index = 0; index < points.size(); ++index) points[index] = project(ground[index], WorldGeometry::SurfaceHeight(surface, ground[index]));
      for (std::size_t index = 0; index < colors.size(); ++index) {
        const auto brightness = static_cast<std::uint8_t>(std::clamp(shade ? shade(ground[index]) : 1.f, 0.f, 1.f) * 255.f);
        colors[index] = {brightness, brightness, brightness};
      }
      const auto origin = uv + sf::Vector2f{std::fmod(x - surface.bounds.position.x, tile), std::fmod(y - surface.bounds.position.y, tile)} * (128.f / tile);
      const auto size = sf::Vector2f{right - x, bottom - y} * (128.f / tile);
      const std::array<sf::Vector2f, 4> coordinates{origin, origin + sf::Vector2f{size.x, 0.f}, origin + size, origin + sf::Vector2f{0.f, size.y}};
      for (const auto index : {0u, 1u, 2u, 0u, 2u, 3u}) mesh.append({points[index], colors[index], coordinates[index]});
    }
    context_.renderer.Draw(TexturedQuad(*materials_, std::move(mesh)));
    if (surface.elevation <= 0.f) return;
    const auto a = surface.bounds.position, b = sf::Vector2f{end.x, a.y}, c = end, d = sf::Vector2f{a.x, end.y};
    for (const auto edge : {std::pair{d, c}, std::pair{c, b}}) {
      const auto first = project(edge.first, WorldGeometry::SurfaceHeight(surface, edge.first));
      const auto second = project(edge.second, WorldGeometry::SurfaceHeight(surface, edge.second));
      const sf::Vector2f down{0.f, surface.thickness};
      Quad({first, second, second + down, first + down}, 6, {100, 110, 120});
    }
  }

  void EnvironmentArt::Wall(const Obstacle &wall, const Project &project) const {
    NO_TRACE_PROFILE_SCOPE(WallMesh);
    const float height = WorldGeometry::ObstacleHeight(wall), tile = PresentationConfig::FloorTileSize;
    const auto end = wall.bounds.position + wall.bounds.size;
    const auto face = [&](sf::Vector2f from, sf::Vector2f to, sf::Color tint) {
      const auto along = to - from; const float length = along.length();
      for (float horizontal = 0.f; horizontal < length; horizontal += tile) for (float vertical = 0.f; vertical < height; vertical += tile) {
        const float next = std::min(horizontal + tile, length), top = std::min(vertical + tile, height);
        const auto left = from + along * (horizontal / length), right = from + along * (next / length);
        Quad({project(left, wall.elevation + top), project(right, wall.elevation + top), project(right, wall.elevation + vertical), project(left, wall.elevation + vertical)},
          !wall.blocksVision ? 7 : wall.cover == CoverKind::Low ? 6 : 5, tint, {(next - horizontal) / tile, (top - vertical) / tile});
      }
    };
    face({wall.bounds.position.x, end.y}, end, {170, 177, 185});
    face(end, {end.x, wall.bounds.position.y}, {125, 140, 153});
    const auto a = project(wall.bounds.position, wall.elevation + height), b = project({end.x, wall.bounds.position.y}, wall.elevation + height);
    const auto c = project(end, wall.elevation + height), d = project({wall.bounds.position.x, end.y}, wall.elevation + height);
    Quad({a, b, c, d}, wall.cover == CoverKind::Low ? 1 : 5, {215, 220, 225});
  }

  EnvironmentArt::ObjectPlacement EnvironmentArt::PlacementFor(const Interactable &object, const Frame &frame) {
    const auto footprint = WorldGeometry::ObjectFootprint(object);
    const float width = (footprint.size.x + footprint.size.y) * PresentationConfig::GroundProjection.x;
    const float depth = (footprint.size.x + footprint.size.y) * PresentationConfig::GroundProjection.y;
    const float height = WorldGeometry::ObjectHeight(object) + depth;
    const sf::Vector2f scale{width / std::max(1.f, frame.size.x), height / std::max(1.f, frame.size.y)};
    const sf::Vector2f anchor{frame.anchor.x, frame.bottom - depth * .5f / std::max(.001f, scale.y)};
    return {scale, anchor};
  }

  sf::Vector2f EnvironmentArt::ObjectPoint(const Interactable &object, sf::Vector2f feet, sf::Vector2f sourcePoint) const {
    const auto index = SpriteFor(object); if (!index) return feet;
    const auto placement = PlacementFor(object, frames_[*index]);
    return feet + (sourcePoint - placement.anchor).componentWiseMul(placement.scale);
  }

  bool EnvironmentArt::Object(const Interactable &object, sf::Vector2f feet) const {
    const auto index = SpriteFor(object); if (!index) return false;
    const auto &frame = frames_[*index];
    const auto placement = PlacementFor(object, frame);
    sf::Sprite sprite(*furniture_, frame.rect); sprite.setOrigin(placement.anchor); sprite.setScale(placement.scale);
    sprite.setPosition(feet); context_.renderer.Draw(sprite);
    return true;
  }

  void EnvironmentArt::Grip(const GripSurface &grip, const Project &project) const {
    const sf::Vector2f first{grip.start.x, grip.start.y}, last{grip.end.x, grip.end.y};
    const auto a = project(first, grip.start.z), b = project(last, grip.end.z);
    const auto direction = b - a;
    if (direction.lengthSquared() < 0.1f) return;
    const sf::Vector2f normal{-direction.y / direction.length() * 3.f, direction.x / direction.length() * 3.f};
    Quad({a - normal, b - normal, b + normal, a + normal}, grip.kind == GripKind::Pipe ? 1 : 6, {200, 205, 210});
    const auto count = std::max(1, static_cast<int>(direction.length() / 32.f));
    for (int index = 0; index <= count; ++index) {
      const auto center = a + direction * (static_cast<float>(index) / static_cast<float>(count));
      const auto along = direction / direction.length() * 2.f;
      Quad({center - normal * 1.5f - along, center - normal * 1.5f + along, center + normal * 1.5f + along, center + normal * 1.5f - along}, 9, {200, 200, 200});
    }
  }
}
