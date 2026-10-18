#include "NoTraceFirstPersonGeometry.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
  using namespace NoTrace;
  using namespace NoTrace::FirstPersonGeometry;
  unsigned int checks = 0;
  void Require(bool valid, const char *message) { ++checks; if (!valid) throw std::runtime_error(message); }
  bool Close(float first, float last, float tolerance = .001f) { return std::abs(first - last) <= tolerance; }
  WorldState Room() {
    WorldState world; world.size = {1000.f, 800.f}; world.phase = GamePhase::Mission;
    Agent player; player.id = 1; player.position = {200.f, 300.f}; player.facing = {1.f, 0.f};
    world.agents.push_back(player);
    return world;
  }
  Obstacle Wall(sf::FloatRect bounds, float elevation = 0.f, float height = 66.f) {
    Obstacle wall; wall.bounds = bounds; wall.elevation = elevation; wall.height = height;
    return wall;
  }
  void ProjectionAndInvalidInput() {
    FirstPersonCamera camera;
    const sf::Vector2f size{320.f, 180.f};
    const auto center = CameraRay(camera, size * .5f, size);
    Require(Close(center.x, 1.f) && Close(center.y, 0.f) && Close(center.z, 0.f), "La mira central no coincide con el eje de cámara");
    const auto edge = CameraRay(camera, {size.x, size.y * .5f}, size);
    Require(Close(std::atan2(edge.y, edge.x), 42.5f * std::numbers::pi_v<float> / 180.f), "El borde de la imagen no respeta el FOV horizontal");
    camera.yaw = std::numbers::pi_v<float> * .5f;
    Require((Forward(camera) - sf::Vector2f{0.f, 1.f}).length() < .001f && (Right(camera) - sf::Vector2f{-1.f, 0.f}).length() < .001f, "Los ejes FPS no conservan WASD relativo a cámara");
    camera.pitch = .4f;
    Require(Close(CameraRay(camera, size * .5f, size).z, std::tan(.4f)), "La inclinación de la mira y el horizonte difieren");
    camera = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};
    camera = Normalize(camera);
    Require(camera.yaw == 0.f && camera.pitch == 0.f && camera.fieldOfView == FirstPersonConfig::DefaultFov, "La configuración no descarta números no finitos");
    camera.pitch = 99.f; camera.fieldOfView = 999.f;
    Require(Normalize(camera).pitch == FirstPersonConfig::MaximumPitch && Normalize(camera).fieldOfView == FirstPersonConfig::MaximumFov, "FOV o pitch superan los límites centrales");
    camera.fieldOfView = 1.f;
    Require(Normalize(camera).fieldOfView == FirstPersonConfig::MinimumFov, "El FOV mínimo no se respeta");
    const auto world = Room();
    for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
      auto agent = world.agents.front(); agent.stance = stance; agent.elevation = 192.f;
      Require(Close(Eye(agent).z, 192.f + CombatGeometry::HeadCenterHeight(stance)), "Los ojos no conservan la altura de la postura y planta reales");
    }
  }
  void DepthAndDoors() {
    auto world = Room();
    const auto legacy = BuildScene(world);
    for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
      auto agent = world.agents[0]; agent.stance = stance;
      const auto floor = Raycast(legacy, Eye(agent), {1.f, 0.f, -1.f});
      Require(floor && Close(floor->point.z, 0.f) && Close(floor->distance, CombatGeometry::HeadCenterHeight(stance)), "LegacyPlane no conserva un suelo físico bajo los pies de todas las posturas");
    }
    world.obstacles.push_back(Wall({{300.f, 100.f}, {12.f, 400.f}}));
    const auto scene = BuildScene(world);
    for (float screenX : {0.f, 32.f, 80.f, 160.f, 240.f, 288.f, 320.f}) {
      const auto ray = CameraRay({}, {screenX, 90.f}, {320.f, 180.f});
      const auto hit = Raycast(scene, Eye(world.agents[0]), ray);
      Require(hit && Close(hit->distance, 100.f), "La pared plana se curva por distancia radial y produce ojo de pez");
      Require(hit && Close(hit->point.x, 300.f), "El rayo frontal no respeta la pared real");
    }
    world.obstacles.clear();
    Interactable door; door.id = 10; door.kind = ObjectKind::Door; door.position = {300.f, 300.f};
    world.objects.push_back(door);
    const auto closed = BuildScene(world);
    Require(Raycast(closed, Eye(world.agents[0]), {1.f, 0.f, 0.f}).has_value(), "La puerta cerrada no ocluye");
    world.objects[0].open = true;
    const auto opened = BuildScene(world);
    Require(!Raycast(opened, Eye(world.agents[0]), {1.f, 0.f, 0.f}), "La puerta abierta mantiene un muro invisible en FPS");
    world.obstacles.push_back(Wall({{450.f, 250.f}, {20.f, 100.f}}));
    const auto behind = BuildScene(world);
    const auto far = Raycast(behind, Eye(world.agents[0]), {1.f, 0.f, 0.f});
    Require(far && Close(far->distance, 250.f), "La apertura no deja ver la geometría física posterior");
    world.objects.clear(); world.obstacles.clear();
    auto glass = Wall({{300.f, 240.f}, {10.f, 120.f}}); glass.blocksVision = false;
    world.obstacles.push_back(glass);
    auto transparent = BuildScene(world);
    Require(!Raycast(transparent, Eye(world.agents[0]), {1.f, 0.f, 0.f}), "El cristal no opaco de un obstáculo se convierte en pared visual");
    Require(Raycast(transparent, Eye(world.agents[0]), {1.f, 0.f, 0.f}, 340.f, true).has_value(), "El cristal sólido pierde su obstrucción balística");
    world.obstacles.clear();
    Interactable window; window.id = 12; window.kind = ObjectKind::Armory; window.propStyle = PropStyle::Glass; window.position = {300.f, 300.f}; window.footprint = {10.f, 120.f}; window.blocksVision = SpatialOverride::Disabled;
    world.objects.push_back(window); transparent = BuildScene(world);
    Require(!Raycast(transparent, Eye(world.agents[0]), {1.f, 0.f, 0.f}), "El objeto Glass opaco oculta al actor visible detrás");
    Require(Raycast(transparent, Eye(world.agents[0]), {1.f, 0.f, 0.f}, 340.f, true).has_value(), "El objeto Glass sólido permite al arma ignorar su volumen");
  }
  void FloorsStairsAndCabin() {
    auto world = Room();
    world.walkSurfaces.push_back({1, 0, WalkSurfaceKind::Floor, {{0.f, 0.f}, world.size}, 0.f, {}, 6.f});
    world.walkSurfaces.push_back({2, 1, WalkSurfaceKind::Floor, {{250.f, 200.f}, {250.f, 250.f}}, 96.f, {}, 6.f});
    auto scene = BuildScene(world);
    const auto underside = Raycast(scene, Eye(world.agents[0]), {1.f, 0.f, .8f});
    Require(underside && Close(underside->point.z, 90.f) && underside->normal.z < 0.f, "El suelo superior deja ver su interior desde la planta inferior");
    const auto top = Raycast(scene, {300.f, 300.f, 140.f}, {1.f, 0.f, -1.f});
    Require(top && Close(top->point.z, 96.f) && top->normal.z > 0.f, "La planta superior no ocluye al mirar hacia abajo");
    const auto floor = Raycast(scene, Eye(world.agents[0]), {1.f, 0.f, -.5f});
    Require(floor && Close(floor->point.z, 0.f), "El suelo inferior no recibe la vista hacia abajo");
    world.walkSurfaces.clear();
    world.walkSurfaces.push_back({3, 0, WalkSurfaceKind::Stair, {{100.f, 0.f}, {100.f, 100.f}}, 20.f, {.5f, 0.f}, 6.f});
    scene = BuildScene(world);
    const auto slope = Raycast(scene, {0.f, 50.f, 80.f}, {1.f, 0.f, -.1f});
    Require(slope && Close(slope->point.x, 183.33333f, .002f) && Close(slope->point.z, 61.66667f, .002f), "La escalera usa una altura plana en lugar de su pendiente física");
    world.walkSurfaces.clear();
    world.walkSurfaces.push_back({90, 0, WalkSurfaceKind::ElevatorCabin, {{250.f, 200.f}, {80.f, 80.f}}, 48.f, {}, 6.f, SurfaceMaterial::Steel});
    scene = BuildScene(world);
    const auto cabin = Raycast(scene, {200.f, 240.f, 45.f}, {1.f, 0.f, 0.f});
    Require(cabin && Close(cabin->distance, 50.f), "La cabina móvil no aparece a su elevación real");
    world.walkSurfaces[0].elevation = 96.f;
    scene = BuildScene(world);
    Require(!Raycast(scene, {200.f, 240.f, 45.f}, {1.f, 0.f, 0.f}), "La cabina deja geometría residual al cambiar de planta");
  }
  void TableOpenSpaceAndPrivacy() {
    auto world = Room();
    Interactable table; table.id = 11; table.kind = ObjectKind::Hideout; table.hideoutKind = HideoutKind::UnderTable; table.position = {300.f, 300.f};
    world.objects.push_back(table);
    auto scene = BuildScene(world);
    Require(!Raycast(scene, {200.f, 300.f, 8.f}, {1.f, 0.f, 0.f}), "La mesa normal se ha convertido en un bloque macizo visual");
    Require(Raycast(scene, {200.f, 300.f, 25.f}, {1.f, 0.f, 0.f}).has_value(), "El tablero no ocluye a su altura real");
    Require(Raycast(scene, {200.f, 278.f, 8.f}, {1.f, 0.f, 0.f}).has_value(), "Las patas de la mesa no aparecen en FPS");
    world.objects[0].overturned = true;
    scene = BuildScene(world);
    Require(!Raycast(scene, {200.f, 300.f, 25.f}, {1.f, 0.f, 0.f}), "El tablero permanece elevado después del vuelco instantáneo");
    Require(Raycast(scene, {200.f, 300.f, 4.f}, {1.f, 0.f, 0.f}).has_value(), "La mesa volcada no conserva su altura de7unidades");
    world.objects.clear();
    Interactable trap; trap.kind = ObjectKind::Explosive; trap.position = {300.f, 300.f}; trap.trap.state = TrapState::Armed;
    world.objects.push_back(trap); scene = BuildScene(world);
    Require(!Raycast(scene, {200.f, 300.f, 3.f}, {1.f, 0.f, 0.f}), "FPS revela una trampa armada oculta por las reglas existentes");
    Agent ally; ally.id = 2; ally.position = {350.f, 300.f};
    Require(AgentVisible(world, world.agents[0], ally), "El compañero visible autorizado desaparece");
    ally.position = {50.f, 300.f};
    Require(!AgentVisible(world, world.agents[0], ally), "El mundo completo del anfitrión revela un compañero fuera de su cono");
    ally.position = {350.f, 300.f}; world.obstacles.push_back(Wall({{270.f, 260.f}, {10.f, 80.f}}));
    Require(!AgentVisible(world, world.agents[0], ally), "El mundo completo revela un compañero tras una pared opaca");
  }
  void MuzzleConvergenceAndObstruction() {
    auto world = Room();
    auto &agent = world.agents.front();
    for (const auto stance : {Stance::Standing, Stance::Crouched, Stance::Prone}) {
      agent.stance = stance; agent.elevation = 96.f;
      const auto aim = ResolveAim(world, agent, {});
      const float distance = (aim.ground - agent.position).length();
      Require(Close(distance, GameplayConfig::WeaponRange), "La mira sin contacto utiliza el alcance de render en lugar del arma");
      const float target = agent.elevation + CombatGeometry::WeaponHeight(stance) + distance * aim.slope;
      Require(Close(target, Eye(agent).z), "La bala no converge desde la boca física al punto bajo la mira");
    }
    agent.stance = Stance::Standing; agent.elevation = 0.f;
    Guard guard; guard.id = 3; guard.position = {380.f, 300.f}; guard.facing = {-1.f, 0.f};
    world.guards.push_back(guard);
    const auto target = ResolveAim(world, agent, {});
    Require((target.ground - agent.position).length() < 180.f && (target.ground - agent.position).length() > 165.f, "La mira no converge al volumen real del enemigo visible");
    world.obstacles.push_back(Wall({{250.f, 270.f}, {10.f, 60.f}}, 0.f, 30.f));
    const auto scene = BuildScene(world);
    Require(!Raycast(scene, Eye(agent), {1.f, 0.f, 0.f}), "La cobertura baja oculta indebidamente los ojos de pie");
    const auto low = ResolveAim(world, agent, {});
    const auto obstruction = Raycast(scene, {agent.position.x, agent.position.y, CombatGeometry::WeaponHeight(agent.stance)}, {1.f, 0.f, low.slope}, GameplayConfig::WeaponRange, true);
    Require(obstruction && Close(obstruction->distance, 50.f), "Ver por encima de cobertura permite atravesarla con la boca del arma");
    world.obstacles[0].height = 66.f;
    const auto wallAim = ResolveAim(world, agent, {});
    Require(Close((wallAim.ground - agent.position).length(), 50.f), "La mira selecciona al enemigo a través de una pared");
    world.obstacles.clear();
    for (float tilt : {-FirstPersonConfig::MaximumPitch, 0.f, FirstPersonConfig::MaximumPitch}) {
      const auto aim = ResolveAim(world, agent, {0.f, tilt, 85.f});
      Require(std::isfinite(aim.slope) && std::abs(aim.slope) <= CombatConfig::MaximumAimSlope, "FPS genera una pendiente de disparo que la autoridad rechaza");
    }
  }
}

int main() {
  try {
    ProjectionAndInvalidInput(); DepthAndDoors(); FloorsStairsAndCabin(); TableOpenSpaceAndPrivacy(); MuzzleConvergenceAndObstruction();
    std::cout << "PASS primera persona: " << checks << " comprobaciones de proyección, profundidad, plantas, puertas y boca física\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << "FAIL primera persona: " << error.what() << '\n'; return 1; }
}
