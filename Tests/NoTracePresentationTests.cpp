#include "NoTraceWorldRenderer.h"
#include <SFML/Graphics/Image.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <set>
#include <limits>
#include <stdexcept>

namespace {
  int checks = 0;
  void Require(bool condition, const char *message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
  }
  bool Near(sf::Vector2f first, sf::Vector2f second, float tolerance) {
    return std::abs(first.x - second.x) <= tolerance && std::abs(first.y - second.y) <= tolerance;
  }
  void GroundSupportsAndDirectionBoundaries(const std::filesystem::path &project) {
    using namespace NoTrace;
    sf::Image image;
    Require(image.loadFromFile(project / "Content/Textures/NoTrace/Characters/OperativeAtlas.png"), "No se puede inspeccionar el apoyo real del atlas operativo");
    std::ifstream table(project / SpriteConfig::FrameTable);
    std::vector<sf::IntRect> frames;
    sf::IntRect frame;
    while (table >> frame.position.x >> frame.position.y >> frame.size.x >> frame.size.y) frames.push_back(frame);
    Require(frames.size() == SpriteConfig::FrameCount, "La inspección de apoyos no recorre los 896 fotogramas");
    const sf::Vector2f feet{802.f, 486.f}, camera{420.f, 380.f};
    for (std::size_t first = 0; first < frames.size(); first += 4) {
      const auto support = WorldRenderer::MeasureGroundSupport(image, {frames[first], frames[first + 1], frames[first + 2], frames[first + 3]});
      Require(support.anchor.x > 0.f && support.anchor.x < SpriteConfig::CellSize && support.anchor.y == SpriteConfig::FeetY && support.width > 0.f,
        "Un apoyo medido abandona el plano del suelo o el área útil del personaje");
      for (std::size_t offset = 0; offset < 4; ++offset) {
        const WorldRenderer::SpriteFrame selected{first + offset, false, false, true};
        const auto placement = WorldRenderer::PlaceSprite(feet, {1.f, 0.f}, Stance::Standing, false, selected, frames[first + offset].size);
        const auto shadow = placement.Map(support.anchor);
        Require(shadow.y == feet.y && std::abs(shadow.x - feet.x) < SpriteConfig::CellSize * placement.scale.x * 0.5f,
          "La sombra se separa verticalmente del suelo o pierde la proporción del atlas");
        for (const sf::Vector2f logical : {sf::Vector2f{1280.f, 720.f}, sf::Vector2f{1600.f, 900.f}, sf::Vector2f{1920.f, 1080.f}})
          for (const float zoom : {PresentationConfig::MinimumZoom, PresentationConfig::DefaultZoom, PresentationConfig::MaximumZoom}) {
            const auto projected = WorldRenderer::ProjectGround(camera, camera, logical);
            const auto transformed = WorldRenderer::PlaceSprite(projected, {1.f, 0.f}, Stance::Standing, false, selected, frames[first + offset].size).Map(support.anchor);
            Require(Near(projected + (transformed - projected) * zoom,
              projected + sf::Vector2f{(support.anchor.x - SpriteConfig::FeetX) * placement.scale.x * zoom, 0.f}, 0.001f),
              "El zoom o la resolución aplican una segunda transformación al apoyo de suelo");
          }
      }
    }
    const auto diagonalIdle = WorldRenderer::MeasureGroundSupport(image, {frames[16], frames[17], frames[18], frames[19]});
    Require(std::abs(diagonalIdle.anchor.x - 43.75f) < 0.01f && diagonalIdle.anchor.x != SpriteConfig::FeetX,
      "La sombra vuelve a usar el centro fijo de la celda pese al apoyo diagonal real");
    const auto facingAt = [](float degrees) {
      const float angle = degrees * std::numbers::pi_v<float> / 180.f;
      const float horizontal = std::sin(angle) / PresentationConfig::GroundProjection.x;
      const float vertical = std::cos(angle) / PresentationConfig::GroundProjection.y;
      return CombatGeometry::Unit({horizontal + vertical, vertical - horizontal});
    };
    for (std::size_t sector = 0; sector < SpriteConfig::Directions; ++sector) {
      const float center = static_cast<float>(sector) * 45.f;
      const auto next = (sector + 1) % SpriteConfig::Directions;
      Require(WorldRenderer::StableSpriteDirection(facingAt(center + 24.f), sector) == sector &&
        WorldRenderer::StableSpriteDirection(facingAt(center + 26.f), sector) == next,
        "La mirada oscila cerca del límite angular o no gira tras superarlo");
      Require(WorldRenderer::StableSpriteDirection(facingAt(center + 21.f), next) == next &&
        WorldRenderer::StableSpriteDirection(facingAt(center + 19.f), next) == sector,
        "La histéresis no conserva el margen al volver de la dirección vecina");
      Require(WorldRenderer::StableSpriteDirection(facingAt(center), std::nullopt) == sector,
        "La primera orientación visual no coincide con la proyección axonométrica");
    }
    Require(WorldRenderer::StableSpriteDirection({}, 7) == 7 && WorldRenderer::StableSpriteDirection({std::numeric_limits<float>::quiet_NaN(), 0.f}, 2) == 2,
      "Un cursor coincidente o inválido borra la última orientación visual válida");
  }
  void OperativeAnimationBindings() {
    using namespace NoTrace;
    std::set<std::size_t> orientations;
    for (std::size_t direction = 0; direction < SpriteConfig::Directions; ++direction) {
      const float angle = direction * std::numbers::pi_v<float> * 0.25f;
      const sf::Vector2f facing{std::sin(angle) / PresentationConfig::GroundProjection.x + std::cos(angle) / PresentationConfig::GroundProjection.y,
          -std::sin(angle) / PresentationConfig::GroundProjection.x + std::cos(angle) / PresentationConfig::GroundProjection.y};
      const auto idle = WorldRenderer::ClipFrame(SpriteConfig::Clip::Idle, facing, 0.f);
      orientations.insert(idle.index);
      for (std::size_t clip = 0; clip < SpriteConfig::Animations.size(); ++clip) {
        const auto &animation = SpriteConfig::Animations[clip];
        std::set<std::size_t> frames;
        Require(animation.count > 0 && animation.first + animation.count <= SpriteConfig::Columns && animation.fps > 0.f,
          "Un ciclo de animación cruza filas o carece de duración");
        for (std::size_t frame = 0; frame < animation.count; ++frame) {
          const auto selected = WorldRenderer::ClipFrame(static_cast<SpriteConfig::Clip>(clip), facing, 0.f,
            (static_cast<float>(frame) + 0.25f) / animation.count);
          Require(selected.operative && !selected.mirror && selected.index < SpriteConfig::FrameCount &&
            selected.index / SpriteConfig::Columns % SpriteConfig::Directions == direction,
            "Un ciclo cambia de dirección, usa espejo o sale del atlas integrado");
          frames.insert(selected.index);
          const auto placement = WorldRenderer::PlaceSprite({800.f, 482.f}, facing, Stance::Standing, false, selected,
            {static_cast<int>(SpriteConfig::CellSize), static_cast<int>(SpriteConfig::CellSize)});
          Require(Near(placement.Map({SpriteConfig::FeetX, SpriteConfig::FeetY}), {800.f, 482.f}, 0.001f) && placement.scale.x == placement.scale.y && placement.scale.x > 0.f,
            "Una pose desplaza su ancla de suelo, refleja el cuerpo o deforma su proporción");
        }
        Require(frames.size() == animation.count, "La tabla repite un único fotograma para fases distintas");
      }
      Agent actor; actor.facing = facing; actor.animationTime = 1234.f;
      actor.moving = true; actor.running = true; actor.reloadRemaining = WeaponConfig::ReloadDuration;
      const auto reload = WorldRenderer::SelectActorSprite(actor);
      Require(reload.index == WorldRenderer::ClipFrame(SpriteConfig::Clip::Reload, facing, 0.f, 0.f).index,
        "El movimiento reemplaza una recarga todavía activa");
      actor.cqcHoldingGuard = 9;
      Require(WorldRenderer::SelectActorSprite(actor).index / SpriteConfig::FamilyFrames ==
        SpriteConfig::Animations[static_cast<std::size_t>(SpriteConfig::Clip::Grab)].family, "La recarga reemplaza al agarre autoritativo");
      actor.actionPose = ActionPose::Choke; actor.actionPoseTime = GameplayConfig::AttackAnimationDuration;
      const auto choke = WorldRenderer::SelectActorSprite(actor);
      actor.actionPoseTime *= 0.2f;
      Require(choke.index != WorldRenderer::SelectActorSprite(actor).index,
        "La pulsación de asfixia depende del reloj global y no recorre su propio ciclo");
      actor = Agent{}; actor.facing = facing; actor.incapacitated = true; actor.actionPose = ActionPose::Fall;
      actor.actionPoseTime = GameplayConfig::AttackAnimationDuration;
      const auto fall = WorldRenderer::SelectActorSprite(actor);
      actor.actionPoseTime = 0.f;
      Require(fall.index != WorldRenderer::SelectActorSprite(actor).index && fall.index == WorldRenderer::ClipFrame(SpriteConfig::Clip::Fall, facing, 0.f, 0.f).index,
        "El desmayo omite los fotogramas de caída o no llega al cuerpo inmóvil");
      for (const bool rising : {false, true}) {
        actor = Agent{}; actor.facing = facing;
        actor.stance = rising ? Stance::Standing : Stance::Crouched;
        actor.transitionFrom = rising ? Stance::Crouched : Stance::Standing;
        actor.transitionTime = GameplayConfig::StanceTransitionDuration * 0.9f;
        const auto first = WorldRenderer::SelectActorSprite(actor);
        actor.transitionTime = GameplayConfig::StanceTransitionDuration * 0.1f;
        Require(first.index != WorldRenderer::SelectActorSprite(actor).index, "Agacharse o incorporarse cambia de postura sin recorrer poses de transición");
      }
      actor = Agent{}; actor.facing = facing; actor.stance = Stance::Crouched;
      actor.maneuver.kind = ManeuverKind::Roll; actor.maneuver.phase = ManeuverPhase::Travel; actor.maneuver.direction = CombatGeometry::Unit(facing);
      std::set<std::size_t> rollFrames;
      for (const float fraction : {0.05f, 0.45f, 0.85f}) {
        actor.maneuver.time = ManeuverConfig::RollStart + ManeuverConfig::RollTravel * fraction;
        rollFrames.insert(WorldRenderer::SelectActorSprite(actor).index);
      }
      Require(rollFrames.size() >= 2, "La rodada mantiene una pose fija durante todo el giro corporal");
      std::set<std::size_t> weaponPostures;
      for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
        actor = Agent{}; actor.facing = facing; actor.stance = stance; actor.reloadRemaining = WeaponConfig::ReloadDuration;
        const auto first = WorldRenderer::SelectActorSprite(actor);
        actor.reloadRemaining *= 0.4f;
        Require(first.index != WorldRenderer::SelectActorSprite(actor).index, "La recarga conserva una imagen estática en una postura");
        weaponPostures.insert(first.index / SpriteConfig::FamilyFrames);
        actor.reloadRemaining = 0.f; actor.aiming = true;
        Require(WorldRenderer::SelectActorSprite(actor).index / SpriteConfig::FamilyFrames == first.index / SpriteConfig::FamilyFrames,
          "Apuntar sustituye la postura de arma por reposo o por un cuerpo vertical");
        actor.cover.active = true;
        Require(WorldRenderer::SelectActorSprite(actor).index == WorldRenderer::ClipFrame(SpriteConfig::Clip::Aim, facing, actor.animationTime).index % SpriteConfig::FamilyFrames + first.index / SpriteConfig::FamilyFrames * SpriteConfig::FamilyFrames,
          "Apuntar en cobertura oculta el arma y separa el láser de su cañón");
        actor.cover.active = false;
        for (const auto pose : {ActionPose::Shoot, ActionPose::ThrowDecoy}) {
          actor.actionPose = pose; actor.actionPoseTime = GameplayConfig::AttackAnimationDuration;
          Require(WorldRenderer::SelectActorSprite(actor).index / SpriteConfig::FamilyFrames == first.index / SpriteConfig::FamilyFrames,
            "Disparar o lanzar un disco abandona la postura física del agente");
        }
      }
      Require(weaponPostures.size() == 3, "Recargar agachado o tumbado muestra un personaje de pie");
      actor = Agent{}; actor.facing = facing; actor.puzzle.target = 8;
      actor.actionPose = ActionPose::Interact; actor.actionPoseTime = GameplayConfig::StanceTransitionDuration;
      actor.animationTime = 0.f;
      const auto interaction = WorldRenderer::SelectActorSprite(actor);
      actor.animationTime = 0.4f;
      Require(interaction.index != WorldRenderer::SelectActorSprite(actor).index,
        "La interacción mantenida del puzle reinicia su reloj y congela la animación");
      Guard guard; WorldState world; world.elapsed = 1234.f; guard.facing = facing;
      for (const bool melee : {false, true}) {
        guard.meleeAttack = melee; guard.attackTime = GameplayConfig::AttackAnimationDuration;
        const auto first = WorldRenderer::SelectGuardSprite(guard, world);
        guard.attackTime *= 0.4f;
        Require(first.index != WorldRenderer::SelectGuardSprite(guard, world).index,
          "El ataque del guardia depende del tiempo total de misión en lugar de su fase de impacto");
      }
    }
    Require(orientations.size() == SpriteConfig::Directions, "El nuevo personaje repite orientaciones en lugar de disponer de ocho direcciones propias");
  }
  void AimingLaserUsesPhysicalSurfaces() {
    using namespace NoTrace;
    WorldState world; world.phase = GamePhase::Mission; world.size = {700.f, 600.f};
    Agent agent; agent.id = 1; agent.position = {100.f, 300.f}; agent.facing = {1.f, 0.f};
    world.obstacles.push_back({{{250.f, 250.f}, {20.f, 100.f}}, CoverKind::High, true, "Muro del láser"});
    Require(Near(WorldRenderer::LaserFor(world, agent).to, {250.f, 300.f}, 0.01f), "El láser atraviesa un muro alto");
    world.obstacles[0].cover = CoverKind::Low; agent.aimSlope = (SpatialConfig::LowCoverHeight + 1.f - CombatConfig::StandingWeaponHeight) / (250.f - agent.position.x);
    const auto standing = WorldRenderer::LaserFor(world, agent);
    agent.stance = Stance::Prone;
    const auto prone = WorldRenderer::LaserFor(world, agent);
    Require(standing.to.x > 270.f && Near(prone.to, {250.f, 300.f}, 0.01f), "El láser no distingue la altura del arma de pie y tumbada frente a cobertura baja");
    world.obstacles.clear(); agent.stance = Stance::Standing; agent.aimSlope = 0.f;
    Interactable door{9, ObjectKind::Door, {300.f, 300.f}, "Puerta del láser"}; world.objects.push_back(door);
    Require(WorldRenderer::LaserFor(world, agent).to.x < door.position.x, "El láser atraviesa una puerta cerrada");
    world.objects[0].open = true;
    const auto open = WorldRenderer::LaserFor(world, agent);
    Require(open.to.x > door.position.x + 20.f, "Abrir una puerta conserva el bloqueo del láser");
    Guard hidden; hidden.id = 77; hidden.position = {180.f, 300.f}; hidden.bodyHidden = true; world.guards.push_back(hidden);
    const auto privateRay = WorldRenderer::LaserFor(world, agent);
    Require(privateRay.to == open.to && privateRay.targetId == 0 && privateRay.targetKind == ShotTarget::None,
      "El láser selecciona o revela una entidad privada en su trayectoria");
    agent.aimSlope = -0.2f;
    const auto floor = WorldRenderer::LaserFor(world, agent);
    Require(floor.toHeight == 0.f && std::abs((floor.to - agent.position).length() - WorldRenderer::WeaponHeight(agent.stance) / 0.2f) < 0.01f,
      "El láser continúa debajo del suelo al apuntar hacia abajo");
  }
}

int main() {
  try {
    using NoTrace::WorldRenderer;
    const sf::Vector2f size{1600.f, 900.f};
    const sf::Vector2f camera{330.f, 475.f};
    const auto center = WorldRenderer::ProjectGround(camera, camera, size);
    Require(Near(center, {800.f, 482.f}, 0.001f), "Centro lógico distinto del anclaje visual del agente");
    Require(Near(WorldRenderer::UnprojectGround(center, camera, size), camera, 0.001f), "El cursor sobre el agente no vuelve a su suelo");
    Require(Near(WorldRenderer::ProjectGround(camera + sf::Vector2f{40.f, 0.f}, camera, size) - center, {54.f, 28.f}, 0.001f), "Eje X axonométrico invertido o mal escalado");
    Require(Near(WorldRenderer::ProjectGround(camera + sf::Vector2f{0.f, 40.f}, camera, size) - center, {-54.f, 28.f}, 0.001f), "Eje Y axonométrico invertido o mal escalado");
    Require(Near(WorldRenderer::UnprojectGround(center + sf::Vector2f{108.f, 0.f}, camera, size) - camera, {40.f, -40.f}, 0.001f), "Movimiento horizontal de pantalla no corresponde al suelo");
    Require(Near(WorldRenderer::UnprojectGround(center + sf::Vector2f{0.f, 54.f}, camera, size) - camera, {40.f, 40.f}, 0.001f), "Movimiento vertical de pantalla no corresponde al suelo");
    const std::array<sf::Vector2f, 4> sizes{{{1600.f, 900.f}, {1280.f, 720.f}, {1024.f, 768.f}, {2560.f, 1440.f}}};
    const std::array<sf::Vector2f, 4> cameras{{{0.f, 0.f}, {330.f, 475.f}, {1000.f, 760.f}, {-250.f, 1200.f}}};
    for (const auto logicalSize : sizes) for (const auto groundCamera : cameras) {
      for (float x : {-100.f, 0.f, 87.25f, 500.f, 1000.f}) for (float y : {-80.f, 0.f, 191.4f, 760.f}) {
        const sf::Vector2f ground{x, y};
        const auto projected = WorldRenderer::ProjectGround(ground, groundCamera, logicalSize);
        const auto restored = WorldRenderer::UnprojectGround(projected, groundCamera, logicalSize);
        Require(Near(ground, restored, 1.12f), "La cuantización del arte introduce más de un píxel de error por eje lógico");
        Require(std::fmod(projected.x, NoTrace::PresentationConfig::PixelSize) == 0.f &&
                    std::fmod(projected.y, NoTrace::PresentationConfig::PixelSize) == 0.f,
                "La proyección produce posiciones fuera de la cuadrícula de píxeles");
        const sf::Vector2f translated{131.f, -47.f};
        Require(Near(projected, WorldRenderer::ProjectGround(ground + translated, groundCamera + translated, logicalSize), 0.001f),
                "La cámara altera la posición relativa de objetos y actores");
      }
      for (const auto cursor : std::array<sf::Vector2f, 5>{{{0.f, 0.f}, {127.f, 323.f}, {800.f, 482.f}, {1599.f, 899.f}, {-60.f, 1100.f}}}) {
        const auto ground = WorldRenderer::UnprojectGround(cursor, groundCamera, logicalSize);
        const auto represented = WorldRenderer::ProjectGround(ground, groundCamera, logicalSize);
        Require(Near(cursor, represented, 1.001f), "El destino del cursor no coincide con su representación lógica");
      }
    }
    const std::array<float, 5> zooms{{NoTrace::PresentationConfig::MinimumZoom, NoTrace::PresentationConfig::DefaultZoom, 2.f, 2.5f, NoTrace::PresentationConfig::MaximumZoom}};
    for (const auto logicalSize : sizes) for (const float zoom : zooms) {
      const auto focus = WorldRenderer::ProjectGround(camera, camera, logicalSize, zoom);
      Require(Near(focus, WorldRenderer::ProjectGround(camera, camera, logicalSize), 0.001f), "El zoom desplaza al agente respecto al foco lógico");
      const auto view = WorldRenderer::WorldView(logicalSize, zoom);
      Require(Near(view.getSize(), logicalSize / zoom, 0.001f), "El zoom usa una escala distinta para las dimensiones del mundo");
      for (const auto ground : std::array<sf::Vector2f, 5>{{{0.f, 0.f}, {67.f, 231.f}, {900.f, 700.f}, camera, {-300.f, 1200.f}}}) {
        const auto base = WorldRenderer::ProjectGround(ground, camera, logicalSize);
        const auto projected = WorldRenderer::ProjectGround(ground, camera, logicalSize, zoom);
        const auto viaView = (base - view.getCenter() + view.getSize() * 0.5f).componentWiseDiv(view.getSize()).componentWiseMul(logicalSize);
        Require(Near(viaView, projected, 0.001f), "La vista real y la proyección del cursor discrepan al aplicar zoom");
        Require(Near(ground, WorldRenderer::UnprojectGround(projected, camera, logicalSize, zoom), 1.12f), "El zoom rompe el retorno al suelo de un punto proyectado");
        Require(Near((projected - focus) / zoom, base - focus, 0.001f), "El zoom deforma la proporción del mundo axonométrico");
        const auto worldCursor = WorldRenderer::UnprojectGround(base, camera, logicalSize, zoom);
        Require(Near(base, WorldRenderer::ProjectGround(worldCursor, camera, logicalSize, zoom), zoom + 0.001f), "El cursor apunta a otra posición después del zoom");
      }
    }
    const float initialZoom = NoTrace::PresentationConfig::DefaultZoom;
    Require(std::abs(WorldRenderer::ZoomAfterSteps(WorldRenderer::ZoomAfterSteps(initialZoom, 3.f), -3.f) - initialZoom) < 0.00001f,
            "Acercar y alejar el mismo número de pasos no restaura el zoom");
    Require(WorldRenderer::ZoomAfterSteps(initialZoom, 1000.f) == NoTrace::PresentationConfig::MaximumZoom, "Acercar supera el máximo configurable");
    Require(WorldRenderer::ZoomAfterSteps(initialZoom, -1000.f) == NoTrace::PresentationConfig::MinimumZoom, "Alejar supera el mínimo configurable");
    Require(WorldRenderer::ClampZoom(std::numeric_limits<float>::quiet_NaN()) == initialZoom, "El zoom admite NaN");
    Require(WorldRenderer::ClampZoom(std::numeric_limits<float>::infinity()) == initialZoom, "El zoom admite infinito");
    Require(WorldRenderer::ZoomAfterSteps(initialZoom, std::numeric_limits<float>::quiet_NaN()) == initialZoom, "Un evento de rueda inválido altera la vista");
    Require(WorldRenderer::ZoomAfterSteps(NoTrace::PresentationConfig::MinimumZoom, -1.f) == NoTrace::PresentationConfig::MinimumZoom &&
        WorldRenderer::ZoomAfterSteps(NoTrace::PresentationConfig::MaximumZoom, 1.f) == NoTrace::PresentationConfig::MaximumZoom,
      "La rueda provoca un salto al insistir en alguno de los límites del zoom");
    const sf::FloatRect crate({100.f, 100.f}, {100.f, 100.f});
    const auto checkSide = [&](sf::Vector2f feet, bool inFront) {
      const std::vector<sf::FloatRect> footprints{crate, {feet, {0.f, 0.f}}};
      const auto order = WorldRenderer::GroundDrawOrder(footprints);
      Require(order.size() == 2 && order[0] == (inFront ? 0u : 1u), "El actor se dibuja en el lado equivocado de la caja");
      Require(footprints[0] == crate && footprints[1].position == feet, "La clasificación visual altera las huellas de colisión");
    };
    for (const auto feet : std::array<sf::Vector2f, 5>{{{150.f, 95.f}, {95.f, 150.f}, {95.f, 95.f}, {195.f, 95.f}, {95.f, 195.f}}}) checkSide(feet, false);
    for (const auto feet : std::array<sf::Vector2f, 5>{{{150.f, 205.f}, {205.f, 150.f}, {205.f, 205.f}, {205.f, 105.f}, {105.f, 205.f}}}) checkSide(feet, true);
    const std::vector<sf::FloatRect> scene{crate, {{95.f, 140.f}, {0.f, 0.f}}, {{205.f, 105.f}, {0.f, 0.f}}, {{250.f, 150.f}, {80.f, 80.f}}, {{240.f, 180.f}, {0.f, 0.f}}};
    const auto ordered = WorldRenderer::GroundDrawOrder(scene);
    const auto rank = [&](std::size_t index) { return std::find(ordered.begin(), ordered.end(), index) - ordered.begin(); };
    Require(rank(1) < rank(0) && rank(0) < rank(2), "Los enemigos a ambos lados de una caja tienen la profundidad invertida");
    Require(rank(0) < rank(4) && rank(4) < rank(3), "Un agente entre dos cajas no conserva la profundidad relativa de ambas");
    Require(WorldRenderer::GroundDrawOrder(scene) == ordered, "El desempate de profundidad cambia entre fotogramas idénticos");
    const auto sameFootprint = WorldRenderer::GroundDrawOrder({{{100.f, 100.f}, {0.f, 0.f}}, {{100.f, 100.f}, {0.f, 0.f}}});
    Require(sameFootprint == std::vector<std::size_t>{0, 1}, "Dos actores en el mismo punto no mantienen un desempate estable");
    const std::vector<sf::FloatRect> wideCrate{{{100.f, 100.f}, {200.f, 100.f}}, {{305.f, 110.f}, {0.f, 0.f}}};
    Require(WorldRenderer::GroundDrawOrder(wideCrate) == std::vector<std::size_t>{0, 1}, "La esquina máxima de una caja ancha sigue ocultando a un actor que ya está delante");
    NoTrace::Interactable station;
    station.kind = NoTrace::ObjectKind::Armory;
    station.position = {500.f, 300.f};
    const auto stationFootprint = WorldRenderer::ObjectFootprint(station);
    Require(stationFootprint == sf::FloatRect({465.f, 287.f}, {70.f, 26.f}), "El ancla de la armería no coincide con la huella de su dibujo");
    Require(WorldRenderer::GroundDrawOrder({stationFootprint, {{536.f, 290.f}, {0.f, 0.f}}}) == std::vector<std::size_t>{0, 1}, "Los objetos interactivos no utilizan la misma profundidad que las cajas");
    Require(WorldRenderer::GroundDrawOrder({}).empty(), "Un mundo vacío produce objetos de dibujo inexistentes");
    NoTrace::WorldState aimingWorld;
    NoTrace::Agent observer;
    observer.id = 1; observer.position = {100.f, 100.f};
    NoTrace::Agent target;
    target.id = 2; target.position = {250.f, 190.f}; target.facing = {1.f, 0.f};
    aimingWorld.agents = {observer, target};
    for (const auto logicalSize : sizes) for (const float zoom : zooms) {
      for (const auto stance : {NoTrace::Stance::Standing, NoTrace::Stance::Crouched, NoTrace::Stance::Prone}) {
        aimingWorld.agents[1].stance = stance;
        const auto volumes = NoTrace::CombatGeometry::ActorVolumes(target.position, target.facing, stance);
        std::array<NoTrace::WorldRenderer::AimSolution, 3> solutions;
        const std::array<std::size_t, 3> parts{{0, 1, 4}};
        const std::array<NoTrace::HitZone, 3> zones{{NoTrace::HitZone::Head, NoTrace::HitZone::Torso, NoTrace::HitZone::Limbs}};
        for (std::size_t part = 0; part < parts.size(); ++part) {
          const auto &volume = volumes[parts[part]];
          float height = part == 0 && stance == NoTrace::Stance::Prone ? volume.top - 0.5f : (volume.bottom + volume.top) * 0.5f;
          if (part == 1 && stance == NoTrace::Stance::Crouched) height = volume.bottom + 2.f;
          auto point = volume.center;
          if (part == 2) {
            // El empeine expuesto evita confundir una pierna ocluida con el brazo o torso delantero.
            const sf::Vector2f side{-volume.forward.y, volume.forward.x};
            point += volume.forward * (volume.halfSize.x - 1.f) * (stance == NoTrace::Stance::Prone ? -1.f : 1.f) + side * (volume.halfSize.y - 1.f);
            height = volume.bottom + 1.f;
          }
          const auto cursor = WorldRenderer::ProjectGround(point, observer.position, logicalSize, zoom) - sf::Vector2f{0.f, height * zoom};
          const auto aim = WorldRenderer::ResolveAim(cursor, aimingWorld, observer.id, logicalSize, zoom);
          solutions[part] = aim;
          const auto hit = NoTrace::CombatGeometry::IntersectActor(observer.position, NoTrace::CombatGeometry::Unit(aim.ground - observer.position),
              WorldRenderer::WeaponHeight(observer.stance), aim.slope, NoTrace::GameplayConfig::WeaponRange, target.position, target.facing, stance);
          const auto aimContext = "El cursor no coincide con volumen: postura=" + std::to_string(static_cast<int>(stance)) + " zoom=" + std::to_string(zoom) +
              " zonaEsperada=" + std::to_string(static_cast<int>(zones[part])) + " zonaReal=" + std::to_string(hit ? static_cast<int>(hit->zone) : -1) +
              " cursor=" + std::to_string(cursor.x) + "," + std::to_string(cursor.y) + " suelo=" + std::to_string(aim.ground.x) + "," + std::to_string(aim.ground.y) +
              " pendiente=" + std::to_string(aim.slope);
          Require(hit && hit->zone == zones[part], aimContext.c_str());
          const float targetHeight = WorldRenderer::WeaponHeight(observer.stance) + aim.slope * (aim.ground - observer.position).length();
          const auto projected = WorldRenderer::ProjectGround(aim.ground, observer.position, logicalSize, zoom) - sf::Vector2f{0.f, targetHeight * zoom};
          Require(Near(cursor, projected, zoom + 0.01f), "El destino volumétrico no vuelve al cursor tras zoom o resolución");
        }
        Require(std::abs(solutions[0].slope - solutions[1].slope) > 0.001f, "Cabeza y torso siguen compartiendo una selección automática de toda la silueta");
        Require(std::abs(solutions[1].slope - solutions[2].slope) > 0.001f, "Torso y extremidades no conservan alturas de impacto diferentes");
      }
      const sf::Vector2f emptyCursor{50.f, 80.f};
      for (const auto stance : {NoTrace::Stance::Standing, NoTrace::Stance::Crouched, NoTrace::Stance::Prone}) {
        aimingWorld.agents[0].stance = stance;
        const auto aim = WorldRenderer::ResolveAim(emptyCursor, aimingWorld, 1, logicalSize, zoom);
        const auto projected = WorldRenderer::ProjectGround(aim.ground, observer.position, logicalSize, zoom) - sf::Vector2f{0.f, WorldRenderer::WeaponHeight(stance) * zoom};
        Require(aim.slope == 0.f && Near(emptyCursor, projected, zoom + 0.001f), "La puntería libre no respeta la altura del arma según postura");
      }
      aimingWorld.agents[0].stance = NoTrace::Stance::Standing;
    }
    Require(NoTrace::PresentationConfig::MinimumZoom > 1.4f && NoTrace::PresentationConfig::MaximumZoom > 3.5f &&
        NoTrace::PresentationConfig::DefaultZoom >= NoTrace::PresentationConfig::MinimumZoom && NoTrace::PresentationConfig::DefaultZoom <= NoTrace::PresentationConfig::MaximumZoom,
            "El intervalo de zoom conserva el alejamiento excesivo o no permite apreciar los detalles");
    const auto project = std::filesystem::path(__FILE__).parent_path().parent_path();
    const auto inspectAtlas = [&](const char *imageFile, const char *tableFile, std::size_t rows, std::size_t columns) {
      sf::Image image;
      Require(image.loadFromFile(project / "Content/Textures/NoTrace/Characters" / imageFile), "No se puede leer el atlas original de personajes");
      std::ifstream table(project / "Content/Textures/NoTrace/Characters" / tableFile);
      std::vector<std::uint64_t> hashes;
      std::vector<sf::IntRect> rectangles;
      int x, y, width, height;
      while (table >> x >> y >> width >> height) {
        Require(x >= 0 && y >= 0 && width > 0 && height > 0 && x + width <= static_cast<int>(image.getSize().x) && y + height <= static_cast<int>(image.getSize().y), "La tabla de sprites contiene un recorte fuera del atlas");
        std::uint64_t hash = 1469598103934665603ull;
        std::size_t opaque = 0;
        for (int py = y; py < y + height; ++py) for (int px = x; px < x + width; ++px) {
          const auto pixel = image.getPixel({static_cast<unsigned int>(px), static_cast<unsigned int>(py)});
          opaque += pixel.a > 100;
          hash = (hash ^ pixel.toInteger()) * 1099511628211ull;
        }
        Require(opaque > 100 && opaque < static_cast<std::size_t>(width) * height, "Un fotograma está vacío o incluye un fondo opaco");
        hashes.push_back(hash);
        rectangles.emplace_back(sf::Vector2i{x, y}, sf::Vector2i{width, height});
      }
      Require(hashes.size() == rows * columns, "Faltan poses o direcciones diseñadas en el atlas");
      for (std::size_t row = 0; row < rows; ++row) {
        const std::size_t first = row * columns + 1;
        Require(hashes[first] != hashes[first + 1], "El movimiento reutiliza una ilustración estática en lugar de fotogramas distintos");
      }
      auto muzzleFile = project / "Content/Textures/NoTrace/Characters" / tableFile;
      muzzleFile.replace_extension(".muzzles");
      std::ifstream sockets(muzzleFile);
      sf::Vector2f point;
      std::size_t socketIndex = 0;
      while (sockets >> point.x >> point.y) {
        Require(socketIndex < rectangles.size(), "Sobran bocas de arma respecto a los fotogramas del PNG");
        const auto &rect = rectangles[socketIndex++];
        const auto family = (socketIndex - 1) / NoTrace::SpriteConfig::FamilyFrames;
        const bool weaponFrame = std::string(imageFile) == "OperativeAtlas.png" &&
          (family == 3 || family == 5 || family == 6) && (socketIndex - 1) % columns < 8;
        if (point.x == -1.f && point.y == -1.f) {
          Require(!weaponFrame, "Una pose de apuntado o disparo carece de boca de arma en su postura y dirección");
          continue;
        }
        Require(std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0.f && point.y >= 0.f && point.x < rect.size.x && point.y < rect.size.y,
            "Una boca del arma queda fuera de los píxeles de su fotograma");
        const auto pixel = rect.position + sf::Vector2i(point);
        const auto alpha = image.getPixel(sf::Vector2u(pixel)).a;
        const auto socketContext = std::string("El socket no toca un píxel opaco del cañón: ") + imageFile + " frame=" + std::to_string(socketIndex - 1) + " alfa=" + std::to_string(alpha);
        Require(alpha >= 220, socketContext.c_str());
        bool boundary = false;
        for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
          const auto neighbor = pixel + sf::Vector2i{dx, dy};
          if (neighbor.x < rect.position.x || neighbor.y < rect.position.y || neighbor.x >= rect.position.x + rect.size.x || neighbor.y >= rect.position.y + rect.size.y ||
              image.getPixel(sf::Vector2u(neighbor)).a < 50) boundary = true;
        }
        // El cañón frontal se proyecta sobre el torso; las otras vistas terminan junto al borde de la silueta.
        const bool frontalWeapon = std::string(imageFile) == "OperativeAtlas.png" && (socketIndex - 1) / columns % NoTrace::SpriteConfig::Directions == 0;
        Require(boundary || frontalWeapon, "El socket está dentro del cuerpo del soldado y no en el extremo del arma");
      }
      Require(sockets.eof() && socketIndex == rectangles.size(), "Faltan bocas o indicadores de arma oculta en la tabla de fotogramas");
    };
    inspectAtlas("SoldiersAtlas.png", "SoldiersAtlas.frames", NoTrace::PresentationConfig::ActorAtlasRows, NoTrace::PresentationConfig::ActorAtlasColumns);
    inspectAtlas("ProneAtlas.png", "ProneAtlas.frames", NoTrace::PresentationConfig::ProneAtlasRows, NoTrace::PresentationConfig::ProneAtlasColumns);
    inspectAtlas("OperativeAtlas.png", "OperativeAtlas.frames", NoTrace::SpriteConfig::FrameCount / NoTrace::SpriteConfig::Columns, NoTrace::SpriteConfig::Columns);
    GroundSupportsAndDirectionBoundaries(project);
    OperativeAnimationBindings();
    AimingLaserUsesPhysicalSurfaces();
    std::set<std::pair<std::size_t, bool>> directions;
    for (int direction = 0; direction < 8; ++direction) {
      const float angle = direction * 0.78539816339f;
      const sf::Vector2f facing{std::sin(angle) / NoTrace::PresentationConfig::GroundProjection.x - std::cos(angle) / NoTrace::PresentationConfig::GroundProjection.y,
          -std::sin(angle) / NoTrace::PresentationConfig::GroundProjection.x - std::cos(angle) / NoTrace::PresentationConfig::GroundProjection.y};
      const auto selected = WorldRenderer::SelectSprite(facing, NoTrace::Stance::Standing, 0.f, false, false, false, false);
      directions.emplace(selected.index, selected.mirror);
      for (const auto stance : {NoTrace::Stance::Standing, NoTrace::Stance::Crouched, NoTrace::Stance::Prone}) {
        const auto first = WorldRenderer::SelectSprite(facing, stance, 0.f, true, false, false, false);
        const auto second = WorldRenderer::SelectSprite(facing, stance, stance == NoTrace::Stance::Prone ? 0.3f : 0.2f, true, false, false, false);
        Require(first.index != second.index, "Una postura no anima sus propios fotogramas de movimiento");
        Require(first.prone == (stance == NoTrace::Stance::Prone), "La postura tumbada sigue usando un cuerpo vertical rotado");
        const auto bounds = WorldRenderer::SpriteBounds(facing, stance, false);
        Require(std::isfinite(bounds.position.x) && bounds.size.x > 0.f && bounds.size.y > 0.f, "La silueta del sprite pierde su ancla física según dirección");
      }
    }
    Require(directions.size() == 8, "La orientación de pie no conserva ocho direcciones distinguibles");
    const sf::Vector2f knownFacing = NoTrace::CombatGeometry::Unit({125.f, -20.f});
    const auto knownPlacement = WorldRenderer::PlaceSprite({800.f, 482.f}, knownFacing, NoTrace::Stance::Standing, false, {32, false, false}, {54, 71});
    Require(Near(knownPlacement.Map({52.5f, 17.5f}), {825.644f, 445.083f}, 0.01f), "La boca revisada del fotograma E vuelve al offset procedural separado del PNG");
    const auto mirroredPlacement = WorldRenderer::PlaceSprite({800.f, 482.f}, knownFacing, NoTrace::Stance::Standing, false, {32, false, true}, {54, 71});
    Require(Near(mirroredPlacement.Map({52.5f, 17.5f}), {777.993f, 445.083f}, 0.01f), "El espejo del sprite no refleja también la boca del arma");
    NoTrace::Guard guard;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::None, "Un guardia patrullando revela una alarma inexistente");
    guard.state = NoTrace::GuardState::Investigate;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Suspicion, "Investigar ruido muestra persecución confirmada");
    guard.state = NoTrace::GuardState::Alert; guard.detectedTime = 0.5f;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Confirmed, "La detección confirmada no tiene indicador propio");
    guard.detectedTime = 0.f;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Combat, "La persecución no se distingue de sospecha");
    guard.state = NoTrace::GuardState::Incapacitated;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Unconscious, "La inconsciencia no sigue el estado real");
    guard.bodyHidden = true;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::None, "Un cuerpo oculto revela su icono");
    guard.bodyHidden = false; guard.state = NoTrace::GuardState::Dead;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Dead, "El cadáver no tiene un símbolo distinto de la inconsciencia");
    guard.storedIn = 105;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::None, "Un cadáver almacenado revela su icono fuera del contenedor");
    guard.storedIn = 0;
    NoTrace::Agent bodyAgent;
    Require(WorldRenderer::BodyIndicatorFor(bodyAgent) == WorldRenderer::GuardIndicator::None, "Un agente operativo muestra un icono de cuerpo");
    bodyAgent.incapacitated = true; bodyAgent.lifeState = NoTrace::LifeState::Incapacitated;
    Require(WorldRenderer::BodyIndicatorFor(bodyAgent) == WorldRenderer::GuardIndicator::Unconscious, "El agente inconsciente no comparte el icono reconocible de inconsciencia");
    bodyAgent.alive = false; bodyAgent.lifeState = NoTrace::LifeState::Dead;
    Require(WorldRenderer::BodyIndicatorFor(bodyAgent) == WorldRenderer::GuardIndicator::Dead, "El agente muerto conserva el símbolo de inconsciencia");
    bodyAgent.hidden = true;
    Require(WorldRenderer::BodyIndicatorFor(bodyAgent) == WorldRenderer::GuardIndicator::None, "El estado vital revela un agente oculto");
    bodyAgent.hidden = false; bodyAgent.extracted = true;
    Require(WorldRenderer::BodyIndicatorFor(bodyAgent) == WorldRenderer::GuardIndicator::None, "El icono vital revela un agente ya extraído");
    guard.bodyHidden = false; guard.state = NoTrace::GuardState::Search;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Searching, "La búsqueda pierde su indicador al romper el contacto visual");
    for (const auto state : {NoTrace::GuardState::ApproachBody, NoTrace::GuardState::InspectBody, NoTrace::GuardState::Vigilance}) {
      guard.state = state;
      Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Searching, "La aproximación, asistencia o vigilancia pierde su indicador de investigación");
    }
    guard.state = NoTrace::GuardState::InspectBody; guard.bodyInspectionTime = 0.6f;
    const auto assistance = WorldRenderer::SelectGuardSprite(guard, NoTrace::WorldState{});
    Require(assistance.index == WorldRenderer::ClipFrame(NoTrace::SpriteConfig::Clip::Interact, guard.facing, guard.bodyInspectionTime).index,
      "La asistencia no utiliza el ciclo de interacción del guardia detenido");
    guard.state = NoTrace::GuardState::Patrol; guard.suspicionRemaining = 0.4f; guard.indicatorProgress = 0.5f;
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::Suspicion && WorldRenderer::GuardDetectionFor(guard).segments > 0,
      "La sospecha reciente desaparece antes de terminar su tiempo de presentación");
    guard.suspicionRemaining = 0.f; guard.indicatorProgress = 0.f;
    NoTrace::WorldState fogWorld;
    fogWorld.phase = NoTrace::GamePhase::Mission; fogWorld.size = {700.f, 600.f};
    fogWorld.agents.push_back(observer); fogWorld.agents[0].position = {200.f, 300.f};
    fogWorld.obstacles = {{{{290.f, 100.f}, {20.f, 400.f}}, NoTrace::CoverKind::High, true, "Muro"}};
    guard.bodyHidden = false; guard.state = NoTrace::GuardState::Alert; guard.position = {450.f, 300.f};
    fogWorld.guards.push_back(guard);
    const auto visibility = NoTrace::Visibility::BuildGrid(fogWorld, fogWorld.agents[0]);
    Require(WorldRenderer::VisibleCell(visibility, fogWorld.agents[0].position), "La niebla oculta al propio agente");
    Require(!WorldRenderer::VisibleCell(visibility, guard.position), "La niebla revela suelo detrás de una pared");
    Require(!NoTrace::Visibility::GuardVisible(fogWorld, fogWorld.agents[0], guard), "Un guardia invisible conserva representación e icono");
    for (const auto state : {NoTrace::GuardState::Incapacitated, NoTrace::GuardState::Dead}) {
      guard.state = state;
      Require(!NoTrace::Visibility::GuardVisible(fogWorld, fogWorld.agents[0], guard), "El icono de un cuerpo atraviesa la pared que lo oculta");
    }
    guard.state = NoTrace::GuardState::Alert;
    NoTrace::Interactable hiddenTerminal;
    hiddenTerminal.kind = NoTrace::ObjectKind::Terminal; hiddenTerminal.position = {460.f, 320.f};
    Require(hiddenTerminal.id == 0 && fogWorld.agents[0].hideoutId == 0 && !NoTrace::Visibility::ObjectVisible(fogWorld, fogWorld.agents[0], hiddenTerminal),
            "El identificador cero de un objeto se confunde con el contenedor ocupado y revela información detrás del muro");
    NoTrace::WorldState zeroDoorWorld;
    zeroDoorWorld.phase = NoTrace::GamePhase::Mission; zeroDoorWorld.size = fogWorld.size;
    NoTrace::Interactable zeroDoor;
    zeroDoor.kind = NoTrace::ObjectKind::Door; zeroDoor.position = {300.f, 300.f}; zeroDoorWorld.objects.push_back(zeroDoor);
    Require(!NoTrace::Visibility::HasLineOfSight(zeroDoorWorld, {200.f, 300.f}, {450.f, 300.f}), "El valor por defecto de objeto ignorado elimina una puerta con id cero de la oclusión");
    zeroDoorWorld.objects[0].open = true;
    Require(NoTrace::Visibility::HasLineOfSight(zeroDoorWorld, {200.f, 300.f}, {450.f, 300.f}), "La puerta con id cero sigue bloqueando visión tras abrirse");
    Require(WorldRenderer::FootprintVisible(fogWorld, fogWorld.agents[0], fogWorld.obstacles[0].bounds), "La pared visible desaparece junto con el suelo que oculta");
    const auto wallFootprint = fogWorld.obstacles[0].bounds;
    auto clientTerrain = fogWorld; clientTerrain.visibility = NoTrace::Visibility::BuildGrid(fogWorld, fogWorld.agents[0]); clientTerrain.visibility.authoritative = true;
    Require(WorldRenderer::FootprintVisible(clientTerrain, clientTerrain.agents[0], wallFootprint),
        "El cliente elimina una pared visible porque el centro de su celda cae dentro de la superficie sólida");
    Require(!WorldRenderer::FootprintVisible(clientTerrain, clientTerrain.agents[0], {{450.f, 260.f}, {20.f, 70.f}}),
        "La prueba exacta de superficies revela otra huella completamente oculta tras la pared");
    const auto invisibleHead = WorldRenderer::ProjectGround(guard.position, fogWorld.agents[0].position, size, initialZoom) - sf::Vector2f{0.f, 40.f * initialZoom};
    Require(WorldRenderer::ResolveAim(invisibleHead, fogWorld, 1, size, initialZoom).slope == 0.f, "La puntería revela o selecciona un enemigo detrás de la pared");
    fogWorld.visibility = visibility;
    Require(!NoTrace::Visibility::GuardVisible(fogWorld, fogWorld.agents[0], guard), "El cliente ignora la máscara de visibilidad autoritativa");
    NoTrace::ShotTrace shot;
    shot.toHeight = 38.f;
    Require(WorldRenderer::ShotImpactHeight(shot, fogWorld) == 38.f, "La presentación reemplaza la altura de impacto resuelta por el anfitrión");
    NoTrace::WorldState softFog;
    softFog.phase = NoTrace::GamePhase::Mission; softFog.size = {1200.f, 1000.f};
    NoTrace::Agent viewer;
    viewer.id = 1; viewer.position = {500.f, 500.f}; viewer.facing = {1.f, 0.f};
    softFog.agents.push_back(viewer);
    const auto openFog = NoTrace::Visibility::BuildGrid(softFog, viewer);
    Require(openFog.visualRadii.size() == NoTrace::VisibilityConfig::VisualRayCount && openFog.visualOrigin == viewer.position,
        "La frontera continua no usa rayos propios del observador");
    const float openLimit = WorldRenderer::FogBoundaryRadius(openFog, 0.f);
    Require(openLimit > NoTrace::VisibilityConfig::StandingRange - 1.f && openLimit <= NoTrace::VisibilityConfig::StandingRange, "La niebla reduce o amplía el alcance real en una habitación abierta");
    float previousOpacity = -1.f;
    std::set<int> opacityLevels;
    for (int distance = static_cast<int>(NoTrace::VisibilityConfig::StandingRange - NoTrace::PresentationConfig::FogFeatherWidth - 5.f); distance <= static_cast<int>(NoTrace::VisibilityConfig::StandingRange + 5.f); ++distance) {
      const float opacity = WorldRenderer::FogOpacity(openFog, viewer.position + sf::Vector2f{static_cast<float>(distance), 0.f});
      Require(std::isfinite(opacity) && opacity >= previousOpacity && opacity >= 0.f && opacity <= 1.f, "La transición radial de niebla no es gradual y monótona");
      previousOpacity = opacity; opacityLevels.insert(static_cast<int>(std::round(opacity * 255.f)));
    }
    Require(opacityLevels.size() > 25, "La niebla continúa usando dos opacidades o saltos asociados a tiles");
    const float viewEdge = 107.f * std::numbers::pi_v<float> / 180.f;
    const auto sideTransition = viewer.position + sf::Vector2f{std::cos(viewEdge), std::sin(viewEdge)} * (NoTrace::VisibilityConfig::StandingRange * 0.65f);
    Require(WorldRenderer::FogOpacity(openFog, sideTransition) > 0.1f && WorldRenderer::FogOpacity(openFog, sideTransition) < 1.f,
        "La niebla suaviza la distancia pero conserva un corte duro en los lados del campo de visión");
    auto alteredCells = openFog;
    std::fill(alteredCells.cells.begin(), alteredCells.cells.end(), 0);
    Require(WorldRenderer::FogOpacity(alteredCells, {680.f, 500.f}) == WorldRenderer::FogOpacity(openFog, {680.f, 500.f}),
        "El dibujo continuo vuelve a recortarse por la cuadrícula de simulación");
    for (int index = 1; index < 70; ++index) {
      const float angle = index * 0.01f;
      const float radius = WorldRenderer::FogBoundaryRadius(openFog, angle);
      const sf::Vector2f point = viewer.position + sf::Vector2f{std::cos(angle), std::sin(angle)} * (radius - 10.f);
      Require(WorldRenderer::FogOpacity(openFog, point) > 0.f && WorldRenderer::FogOpacity(openFog, point) < 1.f,
          "La transición continua presenta un escalón cuadrado al cambiar de ángulo");
    }
    softFog.obstacles = {{{{590.f, 150.f}, {20.f, 700.f}}, NoTrace::CoverKind::High, true, "Pared continua"}};
    const auto wallFog = NoTrace::Visibility::BuildGrid(softFog, viewer);
    Require(WorldRenderer::FogBoundaryRadius(wallFog, 0.f) <= 90.f && WorldRenderer::FogOpacity(wallFog, {650.f, 500.f}) == 1.f,
        "Suavizar el borde revela suelo al otro lado de una pared");
    softFog.obstacles = {{{{590.f, 150.f}, {20.f, 318.f}}, NoTrace::CoverKind::High, true, "Marco norte"},
        {{{590.f, 532.f}, {20.f, 318.f}}, NoTrace::CoverKind::High, true, "Marco sur"}};
    NoTrace::Interactable corridorDoor;
    corridorDoor.id = 5; corridorDoor.kind = NoTrace::ObjectKind::Door; corridorDoor.position = {600.f, 500.f}; softFog.objects = {corridorDoor};
    const auto closedFog = NoTrace::Visibility::BuildGrid(softFog, viewer);
    softFog.objects[0].open = true;
    const auto openedFog = NoTrace::Visibility::BuildGrid(softFog, viewer);
    Require(WorldRenderer::FogOpacity(closedFog, {650.f, 500.f}) == 1.f && WorldRenderer::FogOpacity(openedFog, {650.f, 500.f}) == 0.f,
        "La frontera continua no distingue puerta cerrada y abierta");
    softFog.objects.clear(); softFog.obstacles.clear();
    viewer.position += sf::Vector2f{13.f, 7.f}; softFog.agents[0] = viewer;
    const auto movedFog = NoTrace::Visibility::BuildGrid(softFog, viewer);
    Require(movedFog.visualOrigin == viewer.position && std::abs(WorldRenderer::FogBoundaryRadius(movedFog, 0.f) - openLimit) < 0.001f,
        "El movimiento desplaza el contorno por casillas en lugar de seguir al agente");
    viewer.detectionProgress = 0.f;
    const auto quietEye = WorldRenderer::DetectionFor(viewer);
    viewer.detectionProgress = 0.5f;
    const auto partialEye = WorldRenderer::DetectionFor(viewer);
    viewer.detectionProgress = 0.9f;
    const auto warningEye = WorldRenderer::DetectionFor(viewer);
    viewer.detectionConfirmed = true;
    const auto confirmedEye = WorldRenderer::DetectionFor(viewer);
    viewer.detectionConfirmed = false; viewer.confirmedIndicatorRemaining = 0.4f;
    Require(WorldRenderer::DetectionFor(viewer).confirmed, "El indicador rojo ignora el tiempo mínimo de confirmación");
    viewer.confirmedIndicatorRemaining = 0.f;
    Require(quietEye.segments == 0 && partialEye.segments == NoTrace::PresentationConfig::DetectionEyeSegments / 2 && warningEye.segments > partialEye.segments,
        "El ojo no sigue el progreso gradual autoritativo");
    Require(partialEye.color.r == NoTrace::PresentationConfig::Amber.r && warningEye.color.r == NoTrace::PresentationConfig::Red.r && confirmedEye.confirmed && confirmedEye.segments == NoTrace::PresentationConfig::DetectionEyeSegments,
        "El ojo no distingue sospecha, advertencia y confirmación");
    guard.state = NoTrace::GuardState::Suspicion; guard.suspicion = 0.5f; guard.detectedTime = 0.f;
    const auto guardHalf = WorldRenderer::GuardDetectionFor(guard);
    Require(guardHalf.segments == partialEye.segments && guardHalf.color == partialEye.color && !guardHalf.confirmed,
        "El ojo sobre el guardia usa un progreso o colores distintos del indicador del jugador");
    guard.state = NoTrace::GuardState::Alert;
    Require(WorldRenderer::GuardDetectionFor(guard).confirmed && WorldRenderer::GuardDetectionFor(guard).segments == NoTrace::PresentationConfig::DetectionEyeSegments,
        "La persecución confirmada conserva un ojo de sospecha incompleto");
    NoTrace::Agent fighter;
    fighter.cqcNextKick = true; fighter.cqcAttackTime = NoTrace::CqcConfig::PunchCooldown;
    const auto windup = WorldRenderer::CqcPoseFor(fighter);
    fighter.cqcAttackTime -= NoTrace::CqcConfig::PunchWindup + 0.001f;
    const auto impact = WorldRenderer::CqcPoseFor(fighter);
    fighter.cqcAttackTime = 0.05f;
    const auto recovering = WorldRenderer::CqcPoseFor(fighter);
    Require(windup.extension < 0.f && impact.extension > 0.9f && recovering.extension >= 0.f && recovering.extension < impact.extension && !impact.kick,
        "El puñetazo no distingue preparación, extensión de impacto y recuperación");
    fighter.cqcNextKick = false; fighter.cqcAttackTime = NoTrace::CqcConfig::KickCooldown - NoTrace::CqcConfig::KickWindup - 0.001f;
    Require(WorldRenderer::CqcPoseFor(fighter).kick && WorldRenderer::CqcPoseFor(fighter).extension > 0.9f, "La patada reutiliza la pose de brazo o no alcanza su fase de impacto");
    fighter.cqcAttackTime = 0.f; fighter.cqcHoldingAgent = 2; fighter.actionPose = NoTrace::ActionPose::Choke; fighter.actionPoseTime = NoTrace::GameplayConfig::AttackAnimationDuration * 0.5f;
    Require(WorldRenderer::CqcPoseFor(fighter).gripping && WorldRenderer::CqcPoseFor(fighter).extension > 0.95f,
        "El agarre amistoso no comparte la animación progresiva de asfixia");
    NoTrace::WorldState striking;
    NoTrace::Agent striker; striker.position = {300.f, 300.f}; striker.facing = {1.f, 0.f};
    Require(WorldRenderer::CqcOverlayClear(striking, striker, {330.f, 300.f}), "Una extremidad CQC en espacio abierto no puede solaparse con el objetivo visible");
    striking.obstacles = {{{{314.f, 260.f}, {8.f, 80.f}}, NoTrace::CoverKind::High, true, "Muro entre combatientes"}};
    Require(!WorldRenderer::CqcOverlayClear(striking, striker, {330.f, 300.f}), "El pase final de CQC permite dibujar brazos a través de una pared");
    striking.obstacles = {{{{300.f, 335.f}, {80.f, 12.f}}, NoTrace::CoverKind::High, true, "Muro en primer plano"}};
    Require(NoTrace::WorldGeometry::ClearMovementSegment(striking, striker.position, {330.f, 300.f}, NoTrace::GameplayConfig::AgentRadius) &&
        !WorldRenderer::CqcOverlayClear(striking, striker, {330.f, 300.f}), "El pase final muestra la extremidad por encima de un muro que ocluye a ambos actores");
    NoTrace::WorldState training;
    training.phase = NoTrace::GamePhase::Base; training.mode = NoTrace::SimulationMode::Training;
    viewer.position = {300.f, 300.f}; viewer.facing = {1.f, 0.f}; viewer.detectionProgress = 0.f; viewer.detectionConfirmed = false;
    training.agents = {viewer}; guard = {}; guard.id = 88; guard.trainingDummy = true; guard.position = {345.f, 300.f};
    Require(NoTrace::IsTraining(training) && NoTrace::IsTactical(training) && WorldRenderer::DummyHealthVisible(guard, training, &training.agents[0]),
        "La sala de entrenamiento no activa percepción táctica o no muestra vida de la diana al acercarse");
    Require(WorldRenderer::IndicatorFor(guard) == WorldRenderer::GuardIndicator::None && NoTrace::PresentationConfig::DummyHeight == NoTrace::CombatConfig::StandingHeight,
        "La diana reutiliza un icono enemigo o una altura visual que impide acertar a su volumen real");
    guard.position = {450.f, 300.f};
    Require(!WorldRenderer::DummyHealthVisible(guard, training, &training.agents[0]), "La diana lejana conserva una barra flotante sin interacción ni impacto");
    guard.trainingFeedback = 1.f;
    Require(WorldRenderer::DummyHealthVisible(guard, training, &training.agents[0]), "Impactar una diana visible no activa su indicador de vida");
    guard.trainingDummy = false;
    Require(!WorldRenderer::DummyHealthVisible(guard, training, &training.agents[0]), "La nueva barra de entrenamiento se aplica también a un guardia real");
    guard.trainingDummy = true; training.obstacles = {{{{390.f, 250.f}, {20.f, 100.f}}, NoTrace::CoverKind::High, true, "Pared del entrenamiento"}};
    Require(WorldRenderer::DummyHealthVisible(guard, training, &training.agents[0]), "El mapa de entrenamiento oculta la diana que acaba de recibir un impacto");
    training.phase = NoTrace::GamePhase::Mission; training.obstacles.clear();
    Require(!NoTrace::IsTraining(training) && !WorldRenderer::DummyHealthVisible(guard, training, &training.agents[0]), "Las barras de entrenamiento se conservan al comenzar una operación real");
    viewer.detectionConfirmed = false; viewer.detectionProgress = std::numeric_limits<float>::quiet_NaN();
    Require(WorldRenderer::DetectionFor(viewer).segments == 0, "Una intensidad inválida llena el indicador de vigilancia");
    NoTrace::Agent covered; covered.cover.active=true; covered.cover.normal={-1.f,0.f}; covered.facing=covered.cover.normal;
    const auto coverFrame=WorldRenderer::SelectActorSprite(covered);
    covered.actionPose=NoTrace::ActionPose::KnockWall;
    for (float remaining : {.24f,.18f,.12f,.06f}) {
      covered.actionPoseTime=remaining;
      Require(WorldRenderer::SelectActorSprite(covered).index==coverFrame.index,"El golpe de pared sustituye el cuerpo de cobertura por un puñetazo");
    }
    NoTrace::Guard resting; resting.patrol={{400.f,300.f}}; resting.position={300.f,300.f}; resting.patrolPauseRemaining=15.f;
    const auto idleGuard=WorldRenderer::ClipFrame(NoTrace::SpriteConfig::Clip::Idle,resting.facing,resting.id*.25f);
    Require(WorldRenderer::SelectGuardSprite(resting,training).index==idleGuard.index,"La pausa de patrulla reproduce pasos sin desplazamiento");
    std::cout << "PASS presentación: " << checks << " comprobaciones de ejes, profundidad, zoom, cámara, cursor y cuantización sin ventana\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL presentación: " << error.what() << '\n';
    return 1;
  }
}
