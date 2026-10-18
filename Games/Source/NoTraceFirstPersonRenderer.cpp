#include "NoTraceFirstPersonRenderer.h"
#include "NoTraceFirstPersonGeometry.h"
#include "NoTraceFirstPersonArt.h"
#include "Config/NoTraceWeaponConfig.h"
#include "Core/EngineContext.h"
#include <SFML/Graphics/RectangleShape.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Text.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace NoTrace {
  namespace {
    const Agent *Player(const WorldState &world, PlayerId player) {
      const auto found = std::ranges::find(world.agents, player, &Agent::id);
      return found == world.agents.end() ? nullptr : &*found;
    }
    std::uint8_t Channel(float value) { return static_cast<std::uint8_t>(std::clamp(value, 0.f, 255.f)); }
    sf::Color Shade(sf::Color color, float light) {
      return {Channel(color.r * light), Channel(color.g * light), Channel(color.b * light), color.a};
    }
    void Blend(std::vector<std::uint8_t> &pixels, std::size_t offset, sf::Color color) {
      if (color.a == 0) return;
      const float alpha = color.a / 255.f;
      pixels[offset] = Channel(color.r * alpha + pixels[offset] * (1.f - alpha));
      pixels[offset + 1] = Channel(color.g * alpha + pixels[offset + 1] * (1.f - alpha));
      pixels[offset + 2] = Channel(color.b * alpha + pixels[offset + 2] * (1.f - alpha));
      pixels[offset + 3] = 255;
    }
  }

  struct FirstPersonRenderer::State {
    FirstPersonArt art;
    sf::Image materials;
    sf::Texture texture;
    const sf::Font *font = nullptr;
    std::vector<std::uint8_t> pixels;
    std::vector<float> depth;
    FirstPersonRenderStats stats;
    std::uint64_t eventSequence = 0;
    std::uint64_t soundSequence = 0;
    std::uint64_t lastTick = 0;
    float lastHealth = 0.f;
    float recoil = 0.f;
    float dryFire = 0.f;
    float damage = 0.f;

    explicit State(EngineContext &context) : art(context), font(context.resources.FetchFont("Relidux.otf")) {
      const auto *source = context.resources.FetchTexture("NoTrace/Environment/MilitaryMaterials.png");
      if (!source) throw std::runtime_error("Faltan los materiales militares de primera persona");
      materials = source->copyToImage();
    }
  };

  FirstPersonRenderer::FirstPersonRenderer(EngineContext &context) : context_(context), state_(std::make_unique<State>(context)) {}
  FirstPersonRenderer::~FirstPersonRenderer() = default;

  void FirstPersonRenderer::Reset(const WorldState &world, PlayerId player) {
    state_->eventSequence = 0; state_->soundSequence = 0;
    for (const auto &event : world.events) state_->eventSequence = std::max(state_->eventSequence, event.sequence);
    for (const auto &sound : world.sounds) state_->soundSequence = std::max(state_->soundSequence, sound.sequence);
    const auto *agent = Player(world, player);
    state_->lastTick = world.tick;
    state_->lastHealth = agent ? agent->health : 0.f;
    state_->recoil = 0.f; state_->dryFire = 0.f; state_->damage = 0.f;
  }

  void FirstPersonRenderer::Update(const WorldState &world, PlayerId player, float deltaTime) {
    if (world.tick < state_->lastTick) Reset(world, player);
    state_->lastTick = world.tick;
    const float elapsed = std::isfinite(deltaTime) ? std::clamp(deltaTime, 0.f, 1.f) : 0.f;
    state_->recoil = std::max(0.f, state_->recoil - elapsed);
    state_->dryFire = std::max(0.f, state_->dryFire - elapsed);
    state_->damage = std::max(0.f, state_->damage - elapsed);
    for (const auto &event : world.events) {
      if (event.sequence <= state_->eventSequence) continue;
      if (event.actor == player && (event.kind == EventKind::WeaponLethal || event.kind == EventKind::WeaponNonLethal)) state_->recoil = FirstPersonConfig::RecoilDuration;
    }
    for (const auto &event : world.events) state_->eventSequence = std::max(state_->eventSequence, event.sequence);
    const auto *agent = Player(world, player);
    for (const auto &sound : world.sounds) {
      if (sound.sequence > state_->soundSequence && sound.kind == SoundKind::DryFire && agent &&
          (sound.actor == player || ((sound.position - agent->position).lengthSquared() < .01f && std::abs(sound.elevation - agent->elevation) < .1f))) state_->dryFire = FirstPersonConfig::DryFireDuration;
    }
    for (const auto &sound : world.sounds) state_->soundSequence = std::max(state_->soundSequence, sound.sequence);
    if (agent) {
      if (agent->health < state_->lastHealth) state_->damage = FirstPersonConfig::DamageDuration;
      state_->lastHealth = agent->health;
    }
  }

  CombatGeometry::AimSolution FirstPersonRenderer::Aim(const WorldState &world, PlayerId player, const FirstPersonCamera &camera) const {
    const auto *agent = Player(world, player);
    return agent ? FirstPersonGeometry::ResolveAim(world, *agent, camera) : CombatGeometry::AimSolution{};
  }

  void FirstPersonRenderer::Render(const WorldState &world, PlayerId player, const FirstPersonCamera &camera, bool hudVisible) const {
    using namespace FirstPersonGeometry;
    const auto started = std::chrono::steady_clock::now();
    const auto *observer = Player(world, player);
    if (!observer) return;
    const auto logical = context_.window.GetLogicalSize();
    const unsigned int width = FirstPersonConfig::RenderWidth;
    const unsigned int height = std::clamp(static_cast<unsigned int>(std::lround(width * logical.y / std::max(1.f, logical.x))), FirstPersonConfig::MinimumRenderHeight, FirstPersonConfig::MaximumRenderHeight);
    if (state_->stats.width != width || state_->stats.height != height) {
      if (!state_->texture.resize({width, height})) throw std::runtime_error("No se pudo crear el framebuffer de primera persona");
      state_->texture.setSmooth(false);
      state_->pixels.resize(static_cast<std::size_t>(width) * height * 4);
      state_->depth.resize(static_cast<std::size_t>(width) * height);
    }
    state_->stats = {width, height};
    std::fill(state_->depth.begin(), state_->depth.end(), FirstPersonConfig::RenderDistance);
    const auto normalized = Normalize(camera);
    const auto origin = Eye(*observer);
    const auto forward = Forward(normalized), right = Right(normalized);
    const float focal = FocalLength(normalized, static_cast<float>(width));
    const float pitch = std::tan(normalized.pitch);
    const float horizon = height * .5f + pitch * focal;
    const auto scene = BuildScene(world);
    state_->stats.prisms = scene.prisms.size();
    std::vector<ColumnIntersection> candidates;
    candidates.reserve(scene.prisms.size());
    for (unsigned int x = 0; x < width; ++x) {
      const auto ground = forward + right * ((static_cast<float>(x) + .5f - width * .5f) / focal);
      candidates.clear();
      for (const auto &prism : scene.prisms) if (prism.visible) {
        const auto intersection = IntersectColumn(prism, origin, ground);
        if (intersection) candidates.push_back(*intersection);
      }
      std::sort(candidates.begin(), candidates.end(), [](const auto &first, const auto &last) { return first.near < last.near; });
      state_->stats.columnCandidates += candidates.size();
      for (unsigned int y = 0; y < height; ++y) {
        const std::size_t index = static_cast<std::size_t>(y) * width + x;
        const float sky = std::clamp((static_cast<float>(y) - horizon + height * .7f) / (height * 1.4f), 0.f, 1.f);
        sf::Color color{Channel(11.f + 17.f * sky), Channel(19.f + 22.f * sky), Channel(28.f + 20.f * sky)};
        const sf::Vector3f direction{ground.x, ground.y, (horizon - static_cast<float>(y) - .5f) / focal};
        std::optional<RayHit> nearest;
        for (const auto &column : candidates) {
          if (column.near > (nearest ? nearest->distance : FirstPersonConfig::RenderDistance)) break;
          const auto hit = IntersectRow(column, origin, direction, nearest ? nearest->distance : FirstPersonConfig::RenderDistance);
          if (hit && (!nearest || hit->distance < nearest->distance)) nearest = hit;
        }
        if (nearest) {
          state_->depth[index] = nearest->distance;
          const bool horizontal = std::abs(nearest->normal.z) > .5f;
          const float u = horizontal ? nearest->point.x : std::abs(nearest->normal.x) > .5f ? nearest->point.y : nearest->point.x;
          const float v = horizontal ? nearest->point.y : nearest->point.z;
          const auto coordinate = [](float value) { const float wrapped = value / FirstPersonConfig::MaterialTileSize - std::floor(value / FirstPersonConfig::MaterialTileSize); return static_cast<unsigned int>(wrapped * 127.99f); };
          const unsigned int tile = std::min(15u, nearest->prism->material);
          const sf::Vector2u uv{tile % 4u * 128u + coordinate(u), tile / 4u * 128u + coordinate(v)};
          color = state_->materials.getPixel(uv);
          const float side = horizontal ? (nearest->normal.z > 0.f ? .95f : .6f) : std::abs(nearest->normal.x) > .5f ? .78f : .9f;
          const float light = side * (.38f + .62f / (1.f + nearest->distance * .005f));
          color = Shade(color, light);
        }
        const auto offset = index * 4;
        state_->pixels[offset] = color.r; state_->pixels[offset + 1] = color.g; state_->pixels[offset + 2] = color.b; state_->pixels[offset + 3] = 255;
      }
    }

    const auto billboard = [&](sf::Vector2f position, float elevation, unsigned int variant, unsigned int animation, bool hurt = false) {
      const auto delta = position - observer->position;
      const float depth = delta.dot(forward);
      if (depth <= FirstPersonConfig::NearPlane || depth >= FirstPersonConfig::RenderDistance) return;
      const auto frame = state_->art.ActorFrame(variant, animation);
      const float scale = focal / depth * (CombatConfig::StandingHeight / FirstPersonArtConfig::StandingPixels);
      const float screenX = width * .5f + delta.dot(right) * focal / depth;
      const float feetY = horizon - (elevation - origin.z) * focal / depth;
      const float left = screenX - frame.anchor.x * scale, top = feetY - frame.anchor.y * scale;
      const int fromX = std::max(0, static_cast<int>(std::floor(left))), toX = std::min(static_cast<int>(width), static_cast<int>(std::ceil(left + frame.rect.size.x * scale)));
      const int fromY = std::max(0, static_cast<int>(std::floor(top))), toY = std::min(static_cast<int>(height), static_cast<int>(std::ceil(top + frame.rect.size.y * scale)));
      if (fromX >= toX || fromY >= toY) return;
      ++state_->stats.billboards;
      const auto &image = state_->art.ActorsPixels();
      for (int y = fromY; y < toY; ++y) for (int x = fromX; x < toX; ++x) {
        const auto index = static_cast<std::size_t>(y) * width + x;
        if (depth >= state_->depth[index]) continue;
        const sf::Vector2i texel{std::clamp(static_cast<int>((x + .5f - left) / scale), 0, frame.rect.size.x - 1), std::clamp(static_cast<int>((y + .5f - top) / scale), 0, frame.rect.size.y - 1)};
        auto color = image.getPixel(sf::Vector2u(frame.rect.position + texel));
        if (color.a < 32) continue;
        if (hurt) { color.r = Channel(color.r * .72f + 66.f); color.g = Channel(color.g * .78f); color.b = Channel(color.b * .78f); }
        Blend(state_->pixels, index * 4, Shade(color, .65f + .35f / (1.f + depth * .006f)));
        state_->depth[index] = depth; ++state_->stats.actorPixels;
      }
    };
    for (const auto &guard : world.guards) {
      if (guard.bodyHidden || guard.storedIn != 0 || !Visibility::GuardVisible(world, *observer, guard)) continue;
      const bool down = guard.state == GuardState::Incapacitated || guard.state == GuardState::Dead;
      const bool prone = !guard.trainingDummy && guard.heldBy == 0 && (guard.cqcState == CqcState::KnockedDown || guard.cqcState == CqcState::Restrained);
      const bool impacted = std::ranges::any_of(world.shots, [&](const ShotTrace &shot) { return shot.hit && shot.remaining > 0.f && shot.targetKind == ShotTarget::Guard && shot.targetId == guard.id; });
      const bool walking = !guard.navigation.empty() || (guard.patrolPauseRemaining <= 0.f && guard.stepTime > .001f);
      const unsigned int animation = guard.state == GuardState::Dead ? 8u : down ? 7u : prone ? 6u : impacted || guard.staggerRemaining > 0.f || guard.trainingFeedback > 0.f ? 4u : guard.attackTime > 0.f ? 3u : walking && !guard.trainingDummy ? 1u + static_cast<unsigned int>(guard.stepTime * 8.f) % 2u : 0u;
      billboard(guard.position, guard.elevation, guard.trainingDummy ? 3u : guard.id % 2u, animation, prone && impacted);
    }
    for (const auto &agent : world.agents) {
      if (agent.id == player || !AgentVisible(world, *observer, agent)) continue;
      const bool impacted = std::ranges::any_of(world.shots, [&](const ShotTrace &shot) { return shot.hit && shot.remaining > 0.f && shot.targetKind == ShotTarget::Agent && shot.targetId == agent.id; });
      const unsigned int animation = !agent.alive ? 8u : agent.incapacitated ? 7u : agent.stance == Stance::Prone ? 6u : agent.stance == Stance::Crouched ? 5u : impacted || agent.staggerRemaining > 0.f ? 4u :
          agent.actionPose == ActionPose::Shoot ? 3u : agent.moving ? 1u + static_cast<unsigned int>(agent.animationTime * 8.f) % 2u : 0u;
      billboard(agent.position, agent.elevation, 2u, animation, impacted && agent.stance != Stance::Standing);
    }

    for (const auto &shot : world.shots) if (shot.remaining > 0.f && (shot.hit || shot.blocked)) {
      const auto delta = shot.to - observer->position;
      const float depth = delta.dot(forward);
      if (depth <= FirstPersonConfig::NearPlane || depth >= FirstPersonConfig::RenderDistance) continue;
      const float x = width * .5f + delta.dot(right) * focal / depth;
      const float y = horizon - (shot.toHeight - origin.z) * focal / depth;
      const int radius = std::clamp(static_cast<int>(FirstPersonConfig::ImpactSize * focal / depth), 1, 5);
      for (int dy = -radius; dy <= radius; ++dy) for (int dx = -radius; dx <= radius; ++dx) {
        if (dx * dx + dy * dy > radius * radius) continue;
        const int px = static_cast<int>(x) + dx, py = static_cast<int>(y) + dy;
        if (px < 0 || py < 0 || px >= static_cast<int>(width) || py >= static_cast<int>(height)) continue;
        const auto index = static_cast<std::size_t>(py) * width + px;
        if (depth > state_->depth[index] + 1.5f) continue;
        Blend(state_->pixels, index * 4, shot.hit ? sf::Color{173, 99, 74, 210} : sf::Color{230, 204, 128, 210});
      }
    }

    const bool suppressed = observer->selectedWeapon == WeaponKind::Pistol ? observer->pistolSuppressorEquipped && observer->pistolSuppressor > 0.f : observer->nonLethalSuppressorEquipped && observer->nonLethalSuppressor > 0.f;
    const unsigned int gesture = observer->reloadRemaining > 0.f ? (observer->reloadRemaining > WeaponConfig::ReloadDuration * .5f ? 2u : 3u) : state_->recoil > 0.f ? 1u : 0u;
    const auto weapon = state_->art.ViewmodelFrame(observer->selectedWeapon, suppressed, gesture);
    const auto &weaponImage = state_->art.ViewmodelPixels();
    const float weaponScale = width * .64f / weapon.rect.size.x;
    const float recoil = state_->recoil / FirstPersonConfig::RecoilDuration;
    const float weaponLeft = width * .5f - weapon.anchor.x * weaponScale;
    const float weaponTop = static_cast<float>(height) - weapon.anchor.y * weaponScale + recoil * 3.f;
    for (int y = std::max(0, static_cast<int>(weaponTop)); y < static_cast<int>(height); ++y) for (int x = std::max(0, static_cast<int>(weaponLeft)); x < static_cast<int>(width); ++x) {
      const sf::Vector2i source{static_cast<int>((x - weaponLeft) / weaponScale), static_cast<int>((y - weaponTop) / weaponScale)};
      if (source.x < 0 || source.y < 0 || source.x >= weapon.rect.size.x || source.y >= weapon.rect.size.y) continue;
      Blend(state_->pixels, (static_cast<std::size_t>(y) * width + x) * 4, weaponImage.getPixel(sf::Vector2u(weapon.rect.position + source)));
    }
    if (state_->damage > 0.f) for (unsigned int y = 0; y < height; ++y) for (unsigned int x = 0; x < width; ++x) {
      if (x > 5u && y > 4u && x + 6u < width && y + 5u < height) continue;
      Blend(state_->pixels, (static_cast<std::size_t>(y) * width + x) * 4, {176, 32, 24, Channel(160.f * state_->damage / FirstPersonConfig::DamageDuration)});
    }
    state_->texture.update(state_->pixels.data());
    context_.renderer.ResetView();
    sf::Sprite screen(state_->texture);
    screen.setScale({logical.x / width, logical.y / height});
    context_.renderer.Draw(screen);
    if (hudVisible) {
      const auto rect = [&](sf::Vector2f position, sf::Vector2f size, sf::Color color) {
        sf::RectangleShape shape(size); shape.setPosition(position); shape.setFillColor(color); context_.renderer.Draw(shape);
      };
      const auto center = logical * .5f;
      const sf::Color reticle = state_->dryFire > 0.f ? sf::Color{235, 117, 78} : sf::Color{206, 222, 191};
      for (const auto offset : {sf::Vector2f{-11.f, -1.f}, sf::Vector2f{6.f, -1.f}}) rect(center + offset, {5.f, 2.f}, reticle);
      for (const auto offset : {sf::Vector2f{-1.f, -11.f}, sf::Vector2f{-1.f, 6.f}}) rect(center + offset, {2.f, 5.f}, reticle);
      rect({24.f, logical.y - 64.f}, {360.f, 44.f}, {7, 13, 18, 218});
      if (state_->font) {
        const int magazine = observer->selectedWeapon == WeaponKind::Pistol ? observer->lethalMagazine : observer->nonLethalMagazine;
        const int capacity = observer->selectedWeapon == WeaponKind::Pistol ? WeaponConfig::PistolCapacity : WeaponConfig::NonLethalCapacity;
        const std::string weaponName = observer->selectedWeapon == WeaponKind::Pistol ? "PISTOLA" : "NO LETAL";
        const std::string status = weaponName + "  " + std::to_string(magazine) + "/" + std::to_string(capacity) + "  +" + std::to_string(ReserveAmmo(*observer, observer->selectedWeapon)) + "   HP " + std::to_string(static_cast<int>(std::ceil(observer->health)));
        sf::Text text(*state_->font, status, 18); text.setPosition({36.f, logical.y - 54.f}); text.setFillColor({211, 223, 197}); context_.renderer.Draw(text);
      }
    }
    state_->stats.milliseconds = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
  }

  const FirstPersonRenderStats &FirstPersonRenderer::Stats() const { return state_->stats; }
  float FirstPersonRenderer::DepthAt(sf::Vector2f normalized) const {
    if (state_->depth.empty()) return FirstPersonConfig::RenderDistance;
    const auto x = std::min(state_->stats.width - 1u, static_cast<unsigned int>(std::clamp(normalized.x, 0.f, 1.f) * state_->stats.width));
    const auto y = std::min(state_->stats.height - 1u, static_cast<unsigned int>(std::clamp(normalized.y, 0.f, 1.f) * state_->stats.height));
    return state_->depth[static_cast<std::size_t>(y) * state_->stats.width + x];
  }
  sf::Image FirstPersonRenderer::FrameImage() const {
    return state_->pixels.empty() ? sf::Image{} : sf::Image{{state_->stats.width, state_->stats.height}, state_->pixels.data()};
  }
}
