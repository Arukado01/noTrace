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
    const std::array<float, 5> zooms{{1.f, 1.25f, 1.6f, 2.5f, 3.5f}};
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
    Require(NoTrace::PresentationConfig::DefaultZoom > 1.f && NoTrace::PresentationConfig::MinimumZoom >= 1.f && NoTrace::PresentationConfig::MaximumZoom > 3.f,
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
        if (point.x == -1.f && point.y == -1.f) continue;
        Require(std::isfinite(point.x) && std::isfinite(point.y) && point.x >= 0.f && point.y >= 0.f && point.x < rect.size.x && point.y < rect.size.y,
            "Una boca del arma queda fuera de los píxeles de su fotograma");
        const auto pixel = rect.position + sf::Vector2i(point);
        Require(image.getPixel(sf::Vector2u(pixel)).a >= 220, "El socket no toca un píxel opaco del cañón del PNG");
        bool boundary = false;
        for (int dy = -4; dy <= 4; ++dy) for (int dx = -4; dx <= 4; ++dx) {
          const auto neighbor = pixel + sf::Vector2i{dx, dy};
          if (neighbor.x < rect.position.x || neighbor.y < rect.position.y || neighbor.x >= rect.position.x + rect.size.x || neighbor.y >= rect.position.y + rect.size.y ||
              image.getPixel(sf::Vector2u(neighbor)).a < 50) boundary = true;
        }
        Require(boundary, "El socket está dentro del cuerpo del soldado y no en el extremo del arma");
      }
      Require(sockets.eof() && socketIndex == rectangles.size(), "Faltan bocas o indicadores de arma oculta en la tabla de fotogramas");
    };
    inspectAtlas("SoldiersAtlas.png", "SoldiersAtlas.frames", NoTrace::PresentationConfig::ActorAtlasRows, NoTrace::PresentationConfig::ActorAtlasColumns);
    inspectAtlas("ProneAtlas.png", "ProneAtlas.frames", NoTrace::PresentationConfig::ProneAtlasRows, NoTrace::PresentationConfig::ProneAtlasColumns);
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
    const auto invisibleHead = WorldRenderer::ProjectGround(guard.position, fogWorld.agents[0].position, size, 1.6f) - sf::Vector2f{0.f, 40.f * 1.6f};
    Require(WorldRenderer::ResolveAim(invisibleHead, fogWorld, 1, size, 1.6f).slope == 0.f, "La puntería revela o selecciona un enemigo detrás de la pared");
    fogWorld.visibility = visibility;
    Require(!NoTrace::Visibility::GuardVisible(fogWorld, fogWorld.agents[0], guard), "El cliente ignora la máscara de visibilidad autoritativa");
    NoTrace::ShotTrace shot;
    shot.toHeight = 38.f;
    Require(WorldRenderer::ShotImpactHeight(shot, fogWorld) == 38.f, "La presentación reemplaza la altura de impacto resuelta por el anfitrión");
    std::cout << "PASS presentación: " << checks << " comprobaciones de ejes, profundidad, zoom, cámara, cursor y cuantización sin ventana\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL presentación: " << error.what() << '\n';
    return 1;
  }
}
