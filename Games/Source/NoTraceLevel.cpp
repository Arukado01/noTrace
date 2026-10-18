#include "NoTraceSimulation.h"
#include "NoTraceLevel.h"
#include "NoTraceSpatialGeometry.h"
#include "Config/NoTraceLevelConfig.h"
#include "Config/NoTraceTraversalConfig.h"
#include "NoTraceWorldGeometry.h"
#include "NoTraceDistractionGeometry.h"
#include "Config/NoTraceAudioConfig.h"
#include "Config/NoTraceNavigationConfig.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <bit>
#include <iomanip>
#include <sstream>

namespace NoTrace {
  namespace {
    float Length(sf::Vector2f value) { return std::hypot(value.x, value.y); }
    sf::Vector2f Unit(sf::Vector2f value) { const float length = Length(value); return length > 0.0001f ? value / length : sf::Vector2f{}; }
    float Distance(sf::Vector2f first, sf::Vector2f second) { return Length(first - second); }
  }
  namespace Level {
    void BuildMilitary(WorldState &world) {
      using namespace LevelConfig;
      world.size = {Width, Height}; world.walkSurfaces.clear(); world.verticalLinks.clear(); world.gripSurfaces.clear(); world.elevators.clear(); world.obstacles.clear();
      const auto floor = [&](std::uint32_t id, unsigned int layer, sf::FloatRect bounds, float elevation, WalkSurfaceKind kind = WalkSurfaceKind::Floor, SurfaceMaterial material = SurfaceMaterial::Concrete, sf::Vector2f slope = {}) {
        world.walkSurfaces.push_back({id, layer, kind, bounds, elevation, slope, SlabThickness, material});
      };
      floor(1, 0, {{0.f, 0.f}, {1600.f, 650.f}}, 0.f);
      floor(5, 0, {{0.f, 650.f}, {1360.f, 550.f}}, 0.f);
      floor(6, 0, {{1440.f, 650.f}, {160.f, 550.f}}, 0.f, WalkSurfaceKind::Floor, SurfaceMaterial::Soil);
      floor(7, 0, {{1360.f, 730.f}, {80.f, 470.f}}, 0.f);
      floor(10, 1, {{180.f, 80.f}, {500.f, 220.f}}, 96.f, WalkSurfaceKind::Floor, SurfaceMaterial::Industrial);
      floor(11, 1, {{800.f, 80.f}, {500.f, 430.f}}, 96.f, WalkSurfaceKind::Floor, SurfaceMaterial::Steel);
      floor(12, 1, {{680.f, 220.f}, {120.f, 122.f}}, 96.f, WalkSurfaceKind::Bridge, SurfaceMaterial::Grate);
      floor(13, 1, {{800.f, 670.f}, {500.f, 180.f}}, 96.f, WalkSurfaceKind::Catwalk, SurfaceMaterial::Grate);
      floor(14, 1, {{1120.f, 510.f}, {80.f, 160.f}}, 96.f, WalkSurfaceKind::Catwalk, SurfaceMaterial::Grate);
      floor(15, 1, {{1300.f, 332.f}, {100.f, 80.f}}, 96.f, WalkSurfaceKind::Catwalk, SurfaceMaterial::Steel);
      floor(16, 1, {{1300.f, 650.f}, {60.f, 80.f}}, 96.f, WalkSurfaceKind::Catwalk, SurfaceMaterial::Steel);
      floor(20, 2, {{800.f, 80.f}, {600.f, 60.f}}, 192.f, WalkSurfaceKind::Floor, SurfaceMaterial::Steel);
      floor(21, 2, {{180.f, 80.f}, {500.f, 160.f}}, 192.f, WalkSurfaceKind::Floor, SurfaceMaterial::Steel);
      floor(22, 2, {{680.f, 160.f}, {120.f, 80.f}}, 192.f, WalkSurfaceKind::Bridge, SurfaceMaterial::Grate);
      floor(23, 2, {{800.f, 140.f}, {240.f, 160.f}}, 192.f, WalkSurfaceKind::Floor, SurfaceMaterial::Industrial);
      floor(24, 2, {{1300.f, 650.f}, {60.f, 80.f}}, 192.f, WalkSurfaceKind::Catwalk, SurfaceMaterial::Steel);
      floor(25, 2, {{1280.f, 140.f}, {40.f, 590.f}}, 192.f, WalkSurfaceKind::Catwalk, SurfaceMaterial::Grate);
      floor(2, 0, {{200.f, 300.f}, {80.f, 192.f}}, 96.f, WalkSurfaceKind::Stair, SurfaceMaterial::Concrete, {0.f, -0.5f});
      floor(3, 0, {{1300.f, 420.f}, {192.f, 80.f}}, 96.f, WalkSurfaceKind::Stair, SurfaceMaterial::Steel, {-0.5f, 0.f});
      floor(4, 1, {{1320.f, 140.f}, {80.f, 192.f}}, 192.f, WalkSurfaceKind::Stair, SurfaceMaterial::Steel, {0.f, -0.5f});
      floor(90, 0, {{1360.f, 650.f}, {80.f, 80.f}}, 0.f, WalkSurfaceKind::ElevatorCabin, SurfaceMaterial::Steel);
      const auto connection = [&](std::uint32_t id, VerticalLinkKind kind, std::uint32_t from, std::uint32_t to, sf::Vector3f start, sf::Vector3f end, float width = 64.f, std::uint32_t elevator = 0) {
        world.verticalLinks.push_back({id, kind, from, to, 0, 0, start, end, width, true, elevator});
      };
      connection(1, VerticalLinkKind::Stair, 1, 2, {240.f, 504.f, 0.f}, {240.f, 492.f, 0.f});
      connection(2, VerticalLinkKind::Stair, 2, 10, {240.f, 300.f, 96.f}, {240.f, 288.f, 96.f});
      connection(3, VerticalLinkKind::Stair, 1, 3, {1504.f, 460.f, 0.f}, {1492.f, 460.f, 0.f});
      connection(4, VerticalLinkKind::Stair, 3, 11, {1300.f, 460.f, 96.f}, {1288.f, 460.f, 96.f});
      connection(5, VerticalLinkKind::Stair, 15, 4, {1360.f, 344.f, 96.f}, {1360.f, 332.f, 96.f});
      connection(6, VerticalLinkKind::Stair, 4, 20, {1360.f, 140.f, 192.f}, {1360.f, 128.f, 192.f});
      connection(7, VerticalLinkKind::ElevatorDock, 5, 90, {1348.f, 690.f, 0.f}, {1400.f, 690.f, 0.f}, 64.f, 1);
      connection(8, VerticalLinkKind::ElevatorDock, 16, 90, {1348.f, 690.f, 96.f}, {1400.f, 690.f, 96.f}, 64.f, 1);
      connection(9, VerticalLinkKind::ElevatorDock, 24, 90, {1348.f, 690.f, 192.f}, {1400.f, 690.f, 192.f}, 64.f, 1);
      world.elevators.push_back({1, 40, 90, {0.f, 96.f, 192.f}, {5, 16, 24}});
      world.gripSurfaces = {{41, GripKind::Wall, {340.f, 300.f, 64.f}, {600.f, 300.f, 64.f}, {0.f, 1.f}},
        {42, GripKind::Ledge, {340.f, 300.f, 142.f}, {600.f, 300.f, 142.f}, {0.f, 1.f}},
        {43, GripKind::Pipe, {600.f, 330.f, 142.f}, {820.f, 330.f, 142.f}, {}}};
      const float back = GameplayConfig::AgentRadius + TraversalConfig::BodyGap;
      world.verticalLinks.push_back({20, VerticalLinkKind::Attach, 1, 0, 0, 41, {340.f, 326.f, 0.f}, {340.f, 300.f + back, 18.f}, 24.f, true});
      world.verticalLinks.push_back({21, VerticalLinkKind::GripTransfer, 0, 0, 41, 42, {500.f, 300.f + back, 18.f}, {500.f, 300.f + back, 96.f}, 24.f, true});
      world.verticalLinks.push_back({22, VerticalLinkKind::Climb, 0, 10, 42, 0, {500.f, 300.f + back, 96.f}, {500.f, 270.f, 96.f}, 24.f, true});
      world.verticalLinks.push_back({23, VerticalLinkKind::GripTransfer, 0, 0, 42, 43, {600.f, 300.f + back, 96.f}, {600.f, 330.f, 96.f}, 24.f, true});
      world.verticalLinks.push_back({24, VerticalLinkKind::Climb, 0, 11, 43, 0, {820.f, 330.f, 96.f}, {840.f, 330.f, 96.f}, 24.f, true});
      world.verticalLinks.push_back({25, VerticalLinkKind::Drop, 0, 1, 41, 0, {340.f, 300.f + back, 18.f}, {340.f, 326.f, 0.f}, 24.f, false});
      world.verticalLinks.push_back({26, VerticalLinkKind::Drop, 0, 1, 42, 0, {340.f, 300.f + back, 96.f}, {340.f, 326.f, 0.f}, 24.f, false});
      world.verticalLinks.push_back({27, VerticalLinkKind::Drop, 0, 12, 43, 0, {700.f, 330.f, 96.f}, {700.f, 330.f, 96.f}, 24.f, false});
      const auto wall = [&](sf::FloatRect bounds, float z, std::string name, CoverKind kind = CoverKind::High, float height = 66.f) {
        Obstacle obstacle{bounds, kind, true, std::move(name)}; obstacle.elevation = z; obstacle.height = height; world.obstacles.push_back(std::move(obstacle));
      };
      wall({{180.f, 80.f}, {500.f, 16.f}}, 96.f, "Hormigón / archivo");
      wall({{180.f, 80.f}, {16.f, 220.f}}, 96.f, "Hormigón / archivo");
      wall({{664.f, 80.f}, {16.f, 140.f}}, 96.f, "Hormigón / archivo");
      wall({{180.f, 284.f}, {20.f, 16.f}}, 96.f, "Descansillo oeste");
      wall({{280.f, 284.f}, {400.f, 16.f}}, 96.f, "Saliente del archivo", CoverKind::Low, 46.f);
      wall({{330.f, 284.f}, {280.f, 16.f}}, 0.f, "Muro de agarre", CoverKind::High, 96.f);
      wall({{800.f, 80.f}, {500.f, 16.f}}, 96.f, "Sala de control");
      wall({{1284.f, 80.f}, {16.f, 220.f}}, 96.f, "Sala de control");
      wall({{1284.f, 380.f}, {16.f, 40.f}}, 96.f, "Sala de control");
      wall({{1284.f, 500.f}, {16.f, 10.f}}, 96.f, "Sala de control");
      wall({{800.f, 494.f}, {320.f, 16.f}}, 96.f, "Sala de control");
      wall({{1200.f, 494.f}, {100.f, 16.f}}, 96.f, "Sala de control");
      wall({{200.f, 670.f}, {480.f, 16.f}}, 0.f, "Almacén militar");
      wall({{200.f, 670.f}, {16.f, 370.f}}, 0.f, "Almacén militar");
      wall({{668.f, 670.f}, {16.f, 118.f}}, 0.f, "Almacén militar");
      wall({{668.f, 852.f}, {16.f, 188.f}}, 0.f, "Almacén militar");
      wall({{200.f, 1024.f}, {480.f, 16.f}}, 0.f, "Almacén militar");
      wall({{800.f, 880.f}, {400.f, 16.f}}, 0.f, "Enfermería y armamento");
      wall({{1184.f, 880.f}, {16.f, 230.f}}, 0.f, "Enfermería y armamento");
      wall({{800.f, 1094.f}, {400.f, 16.f}}, 0.f, "Enfermería y armamento");
      wall({{800.f, 80.f}, {600.f, 12.f}}, 192.f, "Mantenimiento superior", CoverKind::Low, 36.f);
      wall({{180.f, 80.f}, {500.f, 12.f}}, 192.f, "Observatorio", CoverKind::Low, 36.f);
      wall({{330.f, 750.f}, {100.f, 38.f}}, 0.f, "Cajas militares", CoverKind::Low, 36.f);
      wall({{510.f, 920.f}, {100.f, 38.f}}, 0.f, "Suministros", CoverKind::Low, 36.f);
      wall({{950.f, 950.f}, {90.f, 40.f}}, 0.f, "Equipamiento médico", CoverKind::Low, 36.f);
      for (const auto position : {sf::Vector2f{284.f, 112.f}, sf::Vector2f{636.f, 240.f}})
        wall({position, {PillarWidth, PillarWidth}}, 0.f, "Pilar exterior del archivo", CoverKind::High, PillarHeight);
      for (auto &object : world.objects) {
        object.elevation = 0.f; object.supportId = object.position.y >= 650.f ? 5u : 1u;
        if (object.id == 11) { object.elevation = 96.f; object.supportId = 10; }
        if (object.id == 12) { object.elevation = 96.f; object.supportId = 11; }
        if (object.id == 13) { object.elevation = 96.f; object.supportId = 10; }
        if (object.id == 16) { object.elevation = 96.f; object.supportId = 11; }
        if (object.id == 22) object.puzzle.intelligence = "INTELIGENCIA: el archivo ocupa la planta 1 oeste; el repetidor, control este. La escalera de servicio, el ascensor de carga y el tubo ofrecen rutas alternativas.";
      }
      const auto furniture = [&](std::uint32_t id, ObjectKind kind, sf::Vector2f position, float z, std::uint32_t support, std::string name) -> Interactable & {
        Interactable object{id, kind, position, std::move(name)}; object.elevation = z; object.supportId = support; world.objects.push_back(std::move(object)); return world.objects.back();
      };
      auto &server = furniture(30, ObjectKind::Armory, {1180.f, 170.f}, 96.f, 11, "Rack de servidores"); server.propStyle = PropStyle::Server; server.decorative = true;
      auto &computer = furniture(31, ObjectKind::Computer, {900.f, 200.f}, 192.f, 23, "Consola de observación");
      computer.propStyle = PropStyle::Console; computer.puzzle.kind = PuzzleKind::Computer; computer.puzzle.difficulty = 3; computer.puzzle.intelligence = "INTELIGENCIA: el puente superior une las dos alas. El ascensor conserva sus ocupantes durante todo el recorrido.";
      auto &medical = furniture(32, ObjectKind::Armory, {1100.f, 1010.f}, 0.f, 5, "Suministros médicos"); medical.propStyle = PropStyle::Medical; medical.decorative = true;
      auto &ammunition = furniture(33, ObjectKind::Armory, {380.f, 850.f}, 0.f, 5, "Almacén de armamento"); ammunition.propStyle = PropStyle::Ammunition; ammunition.decorative = true;
      auto &table = furniture(34, ObjectKind::Hideout, {450.f, 950.f}, 0.f, 5, "Mesa del taller"); table.hideoutKind = HideoutKind::UnderTable; table.propStyle = PropStyle::Desk;
      auto &closet = furniture(35, ObjectKind::Hideout, {620.f, 140.f}, 96.f, 10, "Taquilla del archivo"); closet.hideoutKind = HideoutKind::Closet; closet.propStyle = PropStyle::Locker;
      auto &lift = furniture(40, ObjectKind::Elevator, {1400.f, 690.f}, 0.f, 90, "Ascensor de carga");
      lift.propStyle = PropStyle::Elevator; lift.footprint = {80.f, 80.f}; lift.solid = SpatialOverride::Disabled; lift.blocksVision = SpatialOverride::Disabled; lift.elevatorId = 1;
      for (unsigned int index = 0; index < 3; ++index) {
        auto &panel = furniture(43 + index, ObjectKind::Elevator, {1340.f, 690.f}, static_cast<float>(index) * 96.f, index == 0 ? 5u : index == 1 ? 16u : 24u, "Mando del ascensor");
        panel.propStyle = PropStyle::Buttons; panel.footprint = {12.f, 18.f}; panel.solid = SpatialOverride::Disabled; panel.blocksVision = SpatialOverride::Disabled; panel.elevatorId = 1;
      }
      auto &waste = furniture(36, ObjectKind::Hideout, {1020.f, 790.f}, 0.f, 5, "Recogida de residuos"); waste.hideoutKind = HideoutKind::TrashBin; waste.propStyle = PropStyle::Waste;
      auto &logistics = furniture(37, ObjectKind::Armory, {1150.f, 750.f}, 0.f, 5, "Logística de carga"); logistics.propStyle = PropStyle::Logistics; logistics.decorative = true;
      auto &munitions = furniture(38, ObjectKind::Armory, {280.f, 900.f}, 0.f, 5, "Reserva de munición"); munitions.propStyle = PropStyle::Ammunition; munitions.decorative = true;
      auto &desk = furniture(39, ObjectKind::Operations, {450.f, 190.f}, 96.f, 10, "Puesto de archivo"); desk.propStyle = PropStyle::Desk; desk.decorative = true;
      auto &communications = furniture(41, ObjectKind::Armory, {990.f, 420.f}, 96.f, 11, "Servidores de comunicaciones"); communications.propStyle = PropStyle::Server; communications.decorative = true;
      auto &care = furniture(42, ObjectKind::Armory, {980.f, 1050.f}, 0.f, 5, "Puesto sanitario"); care.propStyle = PropStyle::Medical; care.decorative = true;
      const auto decoration = [&](std::uint32_t id, PropStyle style, sf::Vector2f position, sf::Vector2f size, float z, std::uint32_t support, std::string name) {
        auto &object = furniture(id, ObjectKind::Armory, position, z, support, std::move(name));
        object.propStyle = style; object.footprint = size; object.decorative = true; object.interactive = false;
        object.blocksVision = style == PropStyle::Glass || style == PropStyle::Chair || style == PropStyle::Bench ? SpatialOverride::Disabled : SpatialOverride::Enabled;
      };
      decoration(64, PropStyle::Chair, {420.f, 115.f}, {20.f, 20.f}, 96.f, 10, "Silla del archivo");
      decoration(65, PropStyle::Bench, {360.f, 1000.f}, {72.f, 20.f}, 0.f, 5, "Banco de descanso");
      decoration(66, PropStyle::Barrels, {560.f, 820.f}, {42.f, 36.f}, 0.f, 5, "Bidones de suministros");
      decoration(67, PropStyle::Glass, {1050.f, 340.f}, {70.f, 10.f}, 96.f, 11, "Vidrio de seguridad");
      decoration(68, PropStyle::Ventilation, {1150.f, 850.f}, {42.f, 36.f}, 0.f, 5, "Ventilación técnica");
      decoration(69, PropStyle::SafetyLamp, {1270.f, 320.f}, {18.f, 18.f}, 96.f, 11, "Baliza de seguridad");
      for (auto &agent : world.agents) { agent.elevation = 0.f; agent.supportId = agent.position.y >= 650.f ? 5u : 1u; agent.traversal = {}; }
      for (auto &guard : world.guards) {
        if (guard.id == 1) { guard.patrol = {{330.f, 140.f}, {520.f, 140.f}, {520.f, 250.f}, {330.f, 250.f}}; guard.elevation = 96.f; guard.supportId = 10; }
        else if (guard.id == 2) { guard.patrol = {{900.f, 130.f}, {1240.f, 130.f}, {1240.f, 460.f}, {900.f, 460.f}}; guard.elevation = 96.f; guard.supportId = 11; }
        else if (guard.id == 3) { guard.patrol = {{315.f, 550.f}, {610.f, 550.f}, {610.f, 610.f}, {315.f, 610.f}}; guard.elevation = 0.f; guard.supportId = 1; }
        else { guard.patrol = {{850.f, 170.f}, {1000.f, 170.f}, {1000.f, 260.f}, {850.f, 260.f}}; guard.elevation = 192.f; guard.supportId = 23; }
        guard.patrolElevations.assign(guard.patrol.size(), guard.elevation); guard.waypoint %= guard.patrol.size(); guard.position = guard.patrol[guard.waypoint];
        guard.waypoint = (guard.waypoint + 1) % guard.patrol.size(); guard.lastKnownElevation = guard.elevation; guard.facing = Unit(guard.patrol[guard.waypoint] - guard.position);
      }
      world.securityEntries = {{{1582.f, 110.f}, {1500.f, 110.f}}, {{1582.f, 1110.f}, {1500.f, 1110.f}}, {{315.f, 18.f}, {315.f, 95.f}}};
    }
  }

  void Simulation::BuildMission(std::uint32_t seed) {
    world_.mode = SimulationMode::Operation;
    ResetSecurity();
    for (auto &agent : world_.agents) { agent.cover = {}; agent.decoyGuide = {}; agent.hideoutId = 0; agent.hideoutExit = {}; agent.hidden = false; agent.draggingGuard = -1; agent.actionPose = ActionPose::None; agent.actionPoseTime = 0.f; }
    world_.obstacles.clear(); world_.guards.clear(); world_.objects.clear(); world_.footsteps.clear();
    world_.objects = {
      {10, ObjectKind::Door, {680.f, 820.f}, "Puerta de servicio"},
      {11, ObjectKind::Terminal, {570.f, 240.f}, "Archivo principal"},
      {12, ObjectKind::Secondary, {1100.f, 230.f}, "Archivo secundario"},
      {13, ObjectKind::Camera, {640.f, 230.f}, "Cámara"},
      {14, ObjectKind::Hideout, {95.f, 295.f}, "Escondite oeste"},
      {15, ObjectKind::Hideout, {885.f, 625.f}, "Escondite este"},
      {16, ObjectKind::CoopSwitch, {1150.f, 420.f}, "Repetidor cooperativo"},
      {17, ObjectKind::Extraction, {105.f, 665.f}, "Extracción"},
      {18, ObjectKind::AlarmPanel, {350.f, 110.f}, "Central de seguridad"}
    };
    for (auto &object : world_.objects) {
      if (object.kind == ObjectKind::Door) { object.locked = true; object.puzzle.kind = PuzzleKind::Door; object.puzzle.difficulty = 2u + seed % 2u; }
      if (object.kind == ObjectKind::Camera) { object.camera.sweeping = true; object.camera.baseAngle = 3.14159265f; object.camera.sweepAngle = object.camera.minAngle; const float initialAngle = object.camera.baseAngle + object.camera.sweepAngle; object.facing = {std::cos(initialAngle), std::sin(initialAngle)}; }
    }
    Interactable explosive{21, ObjectKind::Explosive, {300.f, 550.f}, "Explosivo oculto"};
    explosive.puzzle.kind = PuzzleKind::Explosive; explosive.puzzle.difficulty = 3u + seed % 2u;
    explosive.footprint = {16.f, 12.f}; explosive.solid = SpatialOverride::Disabled; explosive.blocksVision = SpatialOverride::Disabled;
    world_.objects.push_back(explosive);
    Interactable computer{22, ObjectKind::Computer, {770.f, 590.f}, "Computador de inteligencia"};
    computer.puzzle.kind = PuzzleKind::Computer; computer.puzzle.difficulty = 3u + static_cast<unsigned int>(world_.contract);
    computer.footprint = {38.f, 28.f};
    world_.objects.push_back(computer);
    Interactable cameraPanel{23, ObjectKind::CameraPanel, {510.f, 110.f}, "Panel de cámaras"};
    cameraPanel.puzzle.kind = PuzzleKind::CameraPanel; cameraPanel.puzzle.difficulty = 3;
    cameraPanel.footprint = {28.f, 24.f}; world_.objects.push_back(cameraPanel);
    for (auto &object : world_.objects) if (object.kind == ObjectKind::Hideout) object.hideoutKind = HideoutKind::TrashBin;
    Interactable closet{19, ObjectKind::Hideout, {600.f, 650.f}, "Armario de mantenimiento"};
    closet.hideoutKind = HideoutKind::Closet; world_.objects.push_back(closet);
    Interactable table{20, ObjectKind::Hideout, {360.f, 650.f}, "Mesa de servicio"};
    table.hideoutKind = HideoutKind::UnderTable; world_.objects.push_back(table);
    for (std::uint32_t index = 0; index < 3 + seed % 2; ++index) {
      Guard guard;
      guard.id = index + 1;
      guard.waypoint = (seed + index + 1) % 4;
      world_.guards.push_back(guard);
    }
    Level::BuildMilitary(world_);
  }

  namespace Level {
    bool CanUseElevator(const WorldState &world, const Agent &agent, const Interactable &object) {
      if (object.kind != ObjectKind::Elevator || !agent.connected || !agent.alive || agent.incapacitated || agent.hidden || agent.extracted || agent.traversal.mode != TraversalMode::Grounded ||
          agent.cqcHoldingAgent || agent.cqcHoldingGuard || agent.cqcHeldBy || agent.puzzle.target || WorldGeometry::DistanceToBounds(agent.position, WorldGeometry::ObjectFootprint(object)) >= GameplayConfig::InteractionDistance) return false;
      const auto found = std::ranges::find(world.elevators, object.elevatorId, &ElevatorState::id);
      if (found == world.elevators.end() || found->phase != ElevatorPhase::Docked || found->requested) return false;
      if (agent.supportId == found->cabinSupport) return true;
      return std::ranges::any_of(found->stops, [&](float z) { return std::abs(z - agent.elevation) <= LevelConfig::ElevatorDockTolerance; }) && std::abs(object.elevation - agent.elevation) <= LevelConfig::SupportTolerance;
    }
    std::string ElevatorPrompt(const WorldState &world, const Agent &agent, const Interactable &object) {
      if (!CanUseElevator(world, agent, object)) return {};
      const auto &lift = *std::ranges::find(world.elevators, object.elevatorId, &ElevatorState::id);
      return agent.supportId == lift.cabinSupport ? "E · Viajar a la siguiente planta" : "E · Llamar al ascensor";
    }
  }

  bool Simulation::UseElevator(Agent &agent, Interactable &object) {
    if (!Level::CanUseElevator(world_, agent, object)) return false;
    auto &lift = *std::ranges::find(world_.elevators, object.elevatorId, &ElevatorState::id);
    unsigned int closest = 0;
    for (unsigned int index = 1; index < lift.stops.size(); ++index) if (std::abs(lift.stops[index] - agent.elevation) < std::abs(lift.stops[closest] - agent.elevation)) closest = index;
    lift.targetStop = agent.supportId == lift.cabinSupport ? (closest + 1) % static_cast<unsigned int>(lift.stops.size()) : closest;
    if (std::abs(lift.stops[lift.targetStop] - lift.elevation) <= LevelConfig::ElevatorDockTolerance) return true;
    lift.requested = true;
    EmitSound(SoundKind::Button, object.position, GameplayConfig::WalkingNoiseRadius, agent.id, object.elevation);
    world_.message = "Ascensor de carga solicitado; espera a la apertura de puertas.";
    return true;
  }

  void Simulation::UpdateElevators(float dt) {
    for (auto &lift : world_.elevators) {
      auto cabin = std::ranges::find(world_.walkSurfaces, lift.cabinSupport, &WalkSurface::id);
      if (cabin == world_.walkSurfaces.end() || lift.stops.empty() || lift.targetStop >= lift.stops.size()) continue;
      const auto fullyInside = [&](sf::Vector2f position, float z) {
        if (std::abs(z - lift.elevation) > LevelConfig::SupportTolerance || !WorldGeometry::CircleOverlaps(position, GameplayConfig::AgentRadius, cabin->bounds)) return true;
        const auto end = cabin->bounds.position + cabin->bounds.size;
        return position.x >= cabin->bounds.position.x + GameplayConfig::AgentRadius && position.y >= cabin->bounds.position.y + GameplayConfig::AgentRadius &&
            position.x <= end.x - GameplayConfig::AgentRadius && position.y <= end.y - GameplayConfig::AgentRadius;
      };
      const bool doorClear = std::ranges::all_of(world_.agents, [&](const Agent &agent) { return fullyInside(agent.position, agent.elevation); }) &&
          std::ranges::all_of(world_.guards, [&](const Guard &guard) { return fullyInside(guard.position, guard.elevation); });
      if (lift.phase == ElevatorPhase::Docked && lift.requested) {
        if (!doorClear) continue;
        lift.phase = ElevatorPhase::Closing; lift.phaseTime = 0.f;
        EmitSound(SoundKind::ElevatorClose, cabin->bounds.position + cabin->bounds.size * .5f, GameplayConfig::WalkingNoiseRadius, 0, lift.elevation);
      }
      if (lift.phase == ElevatorPhase::Closing || lift.phase == ElevatorPhase::Opening) {
        lift.phaseTime += dt;
        if (lift.phaseTime >= LevelConfig::ElevatorDoorTime) {
          lift.phaseTime = 0.f;
          if (lift.phase == ElevatorPhase::Closing) {
            lift.phase = ElevatorPhase::Moving;
            EmitSound(SoundKind::ElevatorStart, cabin->bounds.position + cabin->bounds.size * .5f, GameplayConfig::WalkingNoiseRadius, 0, lift.elevation);
          }
          else { lift.phase = ElevatorPhase::Docked; lift.requested = false; }
        }
      }
      if (lift.phase != ElevatorPhase::Moving) continue;
      const float previous = lift.elevation, target = lift.stops[lift.targetStop];
      const float delta = std::clamp(target - previous, -LevelConfig::ElevatorSpeed * dt, LevelConfig::ElevatorSpeed * dt);
      lift.elevation += delta; cabin->elevation = lift.elevation;
      for (auto &agent : world_.agents) if (agent.supportId == lift.cabinSupport) agent.elevation += delta;
      for (auto &guard : world_.guards) if (guard.supportId == lift.cabinSupport) guard.elevation += delta;
      for (auto &object : world_.objects) if (object.supportId == lift.cabinSupport) object.elevation += delta;
      if (std::abs(target - lift.elevation) <= LevelConfig::ElevatorDockTolerance) {
        lift.elevation = cabin->elevation = target; lift.phase = ElevatorPhase::Opening; lift.phaseTime = 0.f;
        EmitSound(SoundKind::ElevatorStop, cabin->bounds.position + cabin->bounds.size * .5f, GameplayConfig::WalkingNoiseRadius, 0, lift.elevation);
        EmitSound(SoundKind::ElevatorOpen, cabin->bounds.position + cabin->bounds.size * .5f, GameplayConfig::WalkingNoiseRadius, 0, lift.elevation);
      }
    }
  }


  namespace Level {
    std::string GeometryHash(const WorldState &world) {
      std::uint64_t hash = 14695981039346656037ull;
      const auto integer = [&](std::uint32_t value) { for (unsigned int byte = 0; byte < 4; ++byte) { hash ^= (value >> (byte * 8)) & 255u; hash *= 1099511628211ull; } };
      const auto number = [&](float value) { integer(std::bit_cast<std::uint32_t>(value == 0.f ? 0.f : value)); };
      const auto point = [&](sf::Vector3f value) { number(value.x); number(value.y); number(value.z); };
      number(world.size.x); number(world.size.y);
      auto surfaces = world.walkSurfaces; std::ranges::sort(surfaces, {}, &WalkSurface::id); integer(static_cast<std::uint32_t>(surfaces.size()));
      for (const auto &surface : surfaces) {
        integer(surface.id); integer(static_cast<std::uint32_t>(surface.kind)); integer(surface.kind == WalkSurfaceKind::ElevatorCabin ? 0u : surface.layer);
        number(surface.bounds.position.x); number(surface.bounds.position.y); number(surface.bounds.size.x); number(surface.bounds.size.y);
        float elevation = surface.elevation;
        if (surface.kind == WalkSurfaceKind::ElevatorCabin) for (const auto &lift : world.elevators) if (lift.cabinSupport == surface.id && !lift.stops.empty()) elevation = lift.stops.front();
        number(elevation); number(surface.slope.x); number(surface.slope.y); number(surface.thickness); integer(static_cast<std::uint32_t>(surface.material));
      }
      auto grips = world.gripSurfaces; std::ranges::sort(grips, {}, &GripSurface::id); integer(static_cast<std::uint32_t>(grips.size()));
      for (const auto &grip : grips) { integer(grip.id); integer(static_cast<std::uint32_t>(grip.kind)); point(grip.start); point(grip.end); number(grip.normal.x); number(grip.normal.y); }
      auto links = world.verticalLinks; std::ranges::sort(links, {}, &VerticalLink::id); integer(static_cast<std::uint32_t>(links.size()));
      for (const auto &link : links) {
        integer(link.id); integer(static_cast<std::uint32_t>(link.kind)); integer(link.fromSupport); integer(link.toSupport); integer(link.fromGrip); integer(link.toGrip);
        point(link.from); point(link.to); number(link.width); integer(link.bidirectional ? 1u : 0u); integer(link.elevator);
      }
      integer(world.walkSurfaces.empty() ? 0u : static_cast<std::uint32_t>(world.obstacles.size()));
      if (!world.walkSurfaces.empty()) for (const auto &obstacle : world.obstacles) { number(obstacle.bounds.position.x); number(obstacle.bounds.position.y); number(obstacle.bounds.size.x); number(obstacle.bounds.size.y); number(obstacle.elevation); number(obstacle.height); }
      std::ostringstream text; text << std::hex << std::setfill('0') << std::setw(16) << hash; return text.str();
    }
  }

}
