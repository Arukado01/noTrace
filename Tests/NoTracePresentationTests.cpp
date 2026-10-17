#include "NoTraceWorldRenderer.h"
#include <array>
#include <cmath>
#include <iostream>
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
    const std::array<float, 5> zooms{{0.5f, 0.75f, 1.f, 1.5f, 2.f}};
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
    Require(WorldRenderer::ZoomAfterSteps(1.5f, std::numeric_limits<float>::quiet_NaN()) == 1.5f, "Un evento de rueda inválido altera la vista");
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
    NoTrace::Agent local;
    local.id = 1; local.position = {100.f, 100.f};
    aimingWorld.agents.push_back(local);
    NoTrace::Guard target;
    target.id = 1; target.position = {220.f, 180.f};
    aimingWorld.guards.push_back(target);
    for (const auto logicalSize : sizes) for (const float zoom : zooms) {
      const auto feet = WorldRenderer::ProjectGround(target.position, local.position, logicalSize, zoom);
      for (const float height : {8.f, 22.f, 42.f}) {
        const auto cursor = feet - sf::Vector2f{0.f, height * zoom};
        Require(Near(WorldRenderer::ResolveAimGround(cursor, aimingWorld, 1, logicalSize, zoom), target.position, 0.001f),
                "Apuntar a la silueta dibujada no dirige el disparo al collider del guardia");
      }
      NoTrace::Guard closer = target;
      closer.id = 2; closer.position += sf::Vector2f{10.f, 10.f};
      aimingWorld.guards.push_back(closer);
      const auto sharedTorso = feet - sf::Vector2f{0.f, NoTrace::PresentationConfig::ShotHeight * zoom};
      Require(Near(WorldRenderer::ResolveAimGround(sharedTorso, aimingWorld, 1, logicalSize, zoom), closer.position, 0.001f),
              "El clic en dos siluetas solapadas selecciona al guardia que está detrás");
      aimingWorld.guards.pop_back();
      const auto headCursor = feet - sf::Vector2f{0.f, 42.f * zoom};
      for (const auto state : {NoTrace::GuardState::Dead, NoTrace::GuardState::Incapacitated}) {
        aimingWorld.guards[0].state = state;
        Require(!Near(WorldRenderer::ResolveAimGround(headCursor, aimingWorld, 1, logicalSize, zoom), target.position, 5.f),
                "La puntería selecciona un cuerpo inactivo como guardia de pie");
      }
      aimingWorld.guards[0].state = NoTrace::GuardState::Patrol;
      aimingWorld.guards[0].bodyHidden = true;
      Require(!Near(WorldRenderer::ResolveAimGround(headCursor, aimingWorld, 1, logicalSize, zoom), target.position, 5.f),
              "La puntería selecciona un cuerpo oculto");
      aimingWorld.guards[0].bodyHidden = false;
      const sf::Vector2f emptyCursor{50.f, 80.f};
      const auto elevatedGround = WorldRenderer::ResolveAimGround(emptyCursor, aimingWorld, 1, logicalSize, zoom);
      const auto representedShot = WorldRenderer::ProjectGround(elevatedGround, local.position, logicalSize, zoom) - sf::Vector2f{0.f, NoTrace::PresentationConfig::ShotHeight * zoom};
      Require(Near(emptyCursor, representedShot, zoom + 0.001f), "El disparo en espacio libre no pasa por el cursor a su altura visual");
      NoTrace::Agent teammate;
      teammate.id = 2; teammate.position = {330.f, 240.f}; teammate.stance = NoTrace::Stance::Prone;
      aimingWorld.agents.push_back(teammate);
      const auto teammateFeet = WorldRenderer::ProjectGround(teammate.position, local.position, logicalSize, zoom);
      const auto proneCursor = teammateFeet + sf::Vector2f{20.f, -6.f} * zoom;
      Require(Near(WorldRenderer::ResolveAimGround(proneCursor, aimingWorld, 1, logicalSize, zoom), teammate.position, 0.001f),
              "La puntería ignora la silueta horizontal de un agente tumbado");
      aimingWorld.agents[1].stance = NoTrace::Stance::Crouched;
      const auto crouchedCursor = teammateFeet - sf::Vector2f{0.f, 30.f * zoom};
      Require(Near(WorldRenderer::ResolveAimGround(crouchedCursor, aimingWorld, 1, logicalSize, zoom), teammate.position, 0.001f),
              "La puntería ignora la silueta baja de un agente agachado");
      aimingWorld.agents[1].alive = false;
      Require(!Near(WorldRenderer::ResolveAimGround(teammateFeet - sf::Vector2f{0.f, 42.f * zoom}, aimingWorld, 1, logicalSize, zoom), teammate.position, 5.f),
              "La puntería selecciona un agente eliminado");
      aimingWorld.agents.pop_back();
      const auto localFeet = WorldRenderer::ProjectGround(local.position, local.position, logicalSize, zoom);
      Require(!Near(WorldRenderer::ResolveAimGround(localFeet - sf::Vector2f{0.f, 42.f * zoom}, aimingWorld, 1, logicalSize, zoom), local.position, 5.f),
              "La puntería selecciona el propio agente como objetivo");
      for (const auto stance : {NoTrace::Stance::Standing, NoTrace::Stance::Crouched, NoTrace::Stance::Prone}) {
        aimingWorld.agents[0].stance = stance;
        const auto destination = WorldRenderer::ResolveAimGround(emptyCursor, aimingWorld, 1, logicalSize, zoom);
        const auto traceAtCursor = WorldRenderer::ProjectGround(destination, local.position, logicalSize, zoom) - sf::Vector2f{0.f, WorldRenderer::WeaponHeight(stance) * zoom};
        Require(Near(emptyCursor, traceAtCursor, zoom + 0.001f), "La puntería libre no coincide con la altura del arma según la postura");
        Require(WorldRenderer::WeaponOffset(stance, {1.f, 0.f}).y == -WorldRenderer::WeaponHeight(stance), "El arma y el trazo usan alturas distintas");
      }
      aimingWorld.agents[0].stance = NoTrace::Stance::Standing;
    }
    NoTrace::ShotTrace shot;
    shot.actor = 1; shot.from = local.position; shot.to = target.position; shot.hit = true;
    Require(WorldRenderer::ShotStance(shot, aimingWorld) == NoTrace::Stance::Standing, "El emisor de pie usa una pose incorrecta de disparo");
    Require(WorldRenderer::ShotImpactHeight(shot, aimingWorld) == NoTrace::PresentationConfig::ShotHeight, "El impacto en un guardia de pie usa otra altura");
    aimingWorld.guards[0].state = NoTrace::GuardState::Incapacitated;
    Require(WorldRenderer::ShotImpactHeight(shot, aimingWorld) == NoTrace::PresentationConfig::ProneShotHeight, "El impacto queda flotando sobre un guardia derribado");
    aimingWorld.agents[0].stance = NoTrace::Stance::Prone;
    shot.hit = false; shot.blocked = true;
    Require(WorldRenderer::ShotStance(shot, aimingWorld) == NoTrace::Stance::Prone && WorldRenderer::ShotImpactHeight(shot, aimingWorld) == NoTrace::PresentationConfig::ProneShotHeight,
            "El disparo tumbado contra cobertura queda flotando a la altura de pie");
    shot.actor = 0; shot.guardId = target.id; shot.hit = true; shot.to = local.position;
    aimingWorld.agents[0].stance = NoTrace::Stance::Crouched;
    Require(WorldRenderer::ShotStance(shot, aimingWorld) == NoTrace::Stance::Standing && WorldRenderer::ShotImpactHeight(shot, aimingWorld) == NoTrace::PresentationConfig::CrouchedShotHeight,
            "El disparo enemigo no distingue su altura del impacto sobre un agente agachado");
    std::cout << "PASS presentación: " << checks << " comprobaciones de ejes, profundidad, zoom, cámara, cursor y cuantización sin ventana\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL presentación: " << error.what() << '\n';
    return 1;
  }
}
