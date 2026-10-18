#pragma once

#include "Config/NoTraceTrainingConfig.h"
#include "NoTraceMultilevelTests.h"
#include "NoTraceFurniture.h"
#include "NoTraceInteraction.h"
#include "NoTraceWorldRenderer.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>

namespace EngineTests {
  template<class Capture>
  nlohmann::json RunLevelRenderFixtures(EngineContext &context, const std::filesystem::path &output, Capture capture) {
    using namespace NoTrace;
    unsigned int checks = 0;
    const auto require = [&](bool condition, const char *message) { ++checks; if (!condition) throw std::runtime_error(message); };
    const auto identical = [](const sf::Image &a, const sf::Image &b) {
      return a.getSize() == b.getSize() && std::equal(a.getPixelsPtr(), a.getPixelsPtr() + static_cast<std::size_t>(a.getSize().x) * a.getSize().y * 4, b.getPixelsPtr());
    };
    Simulation simulation; auto &world = MultilevelTests::QuietMission(simulation); auto &agent = world.agents.front();
    WorldRenderer renderer(context);
    nlohmann::json views = nlohmann::json::array();
    nlohmann::json shadowViews = nlohmann::json::array();
    nlohmann::json heightTransitions = nlohmann::json::array();
    nlohmann::json lowerFloorViews = nlohmann::json::array();
    const auto lowerFog = [](float observer, float surface) { return WorldRenderer::LowerFloorFogOpacity(observer, surface); };
    require(lowerFog(0.f, 0.f) == 0.f && lowerFog(96.f, 96.f) == 0.f && lowerFog(192.f, 192.f) == 0.f && lowerFog(0.f, 96.f) == 0.f,
      "El velo oscurece la planta actual, un piso superior o el suelo al observar desde Z0");
    require(lowerFog(24.f, 0.f) > 0.f && lowerFog(48.f, 0.f) > lowerFog(24.f, 0.f) && lowerFog(72.f, 0.f) > lowerFog(48.f, 0.f) &&
      lowerFog(96.f, 0.f) > lowerFog(72.f, 0.f) && lowerFog(192.f, 0.f) > lowerFog(96.f, 0.f) && lowerFog(192.f, 0.f) < 1.f,
      "El velo no crece de forma continua según la distancia real entre alturas");
    require(lowerFog(LevelConfig::MaximumElevation, LevelConfig::MinimumElevation) <= PresentationConfig::LowerFloorFogMaximumOpacity,
      "La niebla inferior supera el límite configurable y vuelve opaca la planta inferior");
    require(std::abs(lowerFog(192.f, 96.f) - lowerFog(96.f, 0.f)) < .0001f,
      "La niebla inferior depende del número de planta en lugar de la separación física");
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
      require(lowerFog(invalid, 0.f) == 0.f && lowerFog(96.f, invalid) == 0.f, "Una altura no finita contamina la intensidad del velo");
    const auto shadowCapture = [&](WorldRenderer &view, const WorldState &scene, const Agent &actor, std::uint32_t expectedSupport, float expectedElevation, const std::string &name) {
      const auto receiver = WorldRenderer::TraversalShadowSupport(scene, actor);
      require(receiver && receiver->id == expectedSupport && std::abs(receiver->elevation - expectedElevation) <= LevelConfig::SupportTolerance,
        "La sombra colgada no selecciona el suelo físico más alto bajo el cuerpo");
      const auto pixels = capture(view, scene, output / (name + ".png"), false);
      auto bare = scene;
      const auto source = std::ranges::find(bare.agents, actor.id, &Agent::id);
      require(source != bare.agents.end(), "La referencia de sombra pierde el observador de la cámara");
      source->extracted = true;
      const auto floor = capture(view, bare, output / (name + "-without-actor.png"), false);
      const auto logical = context.window.GetLogicalSize();
      const auto bodyAnchor = WorldRenderer::ProjectGround(actor.position, actor.position, logical, view.GetZoom());
      const auto shadowAnchor = bodyAnchor + sf::Vector2f{0.f, actor.elevation - receiver->elevation} * view.GetZoom();
      const auto scale = sf::Vector2f(pixels.getSize()).componentWiseDiv(logical);
      unsigned int shadowPixels = 0;
      for (float y = 2.f; y <= 4.f; y += 1.f) for (float x = -4.f; x <= 4.f; x += 1.f) {
        const auto sample = (shadowAnchor + sf::Vector2f{x, y} * view.GetZoom()).componentWiseMul(scale);
        if (sample.x < 0.f || sample.y < 0.f || sample.x >= pixels.getSize().x || sample.y >= pixels.getSize().y) continue;
        const auto point = sf::Vector2u(sample);
        shadowPixels += pixels.getPixel(point) != floor.getPixel(point);
      }
      require(shadowPixels > 0, "La captura no contiene sombra visible sobre el receptor real");
      shadowViews.push_back({{"file", name + ".png"}, {"withoutActor", name + "-without-actor.png"}, {"grip", actor.traversal.surfaceId},
        {"body", {actor.position.x, actor.position.y, actor.elevation}}, {"receiver", receiver->id}, {"receiverElevation", receiver->elevation},
        {"bodyAnchor", {bodyAnchor.x, bodyAnchor.y}}, {"shadowAnchor", {shadowAnchor.x, shadowAnchor.y}},
        {"verticalSeparation", actor.elevation - receiver->elevation}, {"verifiedShadowPixels", shadowPixels}});
    };
    for (const auto support : {1u, 10u, 11u, 21u, 23u}) {
      const auto *surface = WorldGeometry::FindSurface(world, support);
      require(surface != nullptr, "La galería no encuentra una planta física");
      agent.position = surface->bounds.position + surface->bounds.size * .5f;
      agent.elevation = WorldGeometry::SurfaceHeight(*surface, agent.position); agent.supportId = support;
      bool clear = false;
      float nearest = std::numeric_limits<float>::max();
      const auto center = agent.position;
      for (float y = surface->bounds.position.y + 20.f; y < surface->bounds.position.y + surface->bounds.size.y - 20.f; y += 24.f) {
        for (float x = surface->bounds.position.x + 20.f; x < surface->bounds.position.x + surface->bounds.size.x - 20.f; x += 24.f) {
          const sf::Vector2f point{x, y};
          const float elevation = WorldGeometry::SurfaceHeight(*surface, point);
          const float distance = (point - center).lengthSquared();
          if (distance < nearest && !WorldGeometry::PositionBlocked(world, point, GameplayConfig::AgentRadius, elevation, CombatConfig::StandingHeight, support)) {
            agent.position = point; agent.elevation = elevation; clear = true; nearest = distance;
          }
        }
      }
      require(clear, "La galería no encuentra una posición física libre en la planta");
      agent.facing = {1.f, 0.f}; agent.weaponAim = agent.facing;
      const auto name = "level-support-" + std::to_string(support) + ".png";
      capture(renderer, world, output / name, false);
      const auto focus = context.window.GetLogicalSize().componentWiseMul(PresentationConfig::CameraFocusRatio);
      require((renderer.ScreenToGround(focus, world, agent.id) - agent.position).length() < 1.f, "La cámara pierde el ancla al cambiar de altura");
      views.push_back({{"file", name}, {"support", support}, {"elevation", agent.elevation},
        {"groundOpacity", lowerFog(agent.elevation, 0.f)}, {"currentFloorOpacity", lowerFog(agent.elevation, agent.elevation)}});
    }
    agent.position = {900.f, 220.f}; agent.elevation = 192.f; agent.supportId = 23; agent.facing = {1.f, 0.f};
    Guard below; below.id = 901; below.position = {930.f, 220.f}; below.elevation = 0.f; below.supportId = 1;
    world.guards.push_back(below);
    require(!Visibility::GuardVisible(world, agent, below), "Un suelo sólido permite ver al guardia de otra planta");
    const auto occluded = capture(renderer, world, output / "level-solid-floor-occlusion.png", false);
    world.guards.clear();
    require(identical(occluded, capture(renderer, world, output / "level-solid-floor-redacted.png", false)), "Un guardia bajo el suelo deja píxeles visibles");
    agent.position = {380.f, 950.f}; agent.elevation = 0.f; agent.supportId = 5; agent.facing = {1.f, 0.f};
    auto &table = *std::ranges::find(world.objects, 34u, &Interactable::id);
    require(Furniture::CanFlipTable(world, agent, table), "La mesa del taller no admite volcado físico válido");
    const auto normal = capture(renderer, world, output / "level-table-normal.png", false);
    ActionCommand tool; tool.tool = true; simulation.Step(1.f / 60.f, {{1, tool}});
    require(table.overturned && !Interaction::CanEnterHideout(world, agent, table), "El volcado conserva indebidamente el escondite");
    require(WorldGeometry::BlocksMovement(table) && WorldGeometry::ObjectHeight(table) == SpatialConfig::OverturnedTableHeight, "La mesa volcada no usa su volumen físico");
    require(!identical(normal, capture(renderer, world, output / "level-table-overturned.png", false)), "Volcar la mesa no cambia el sprite en ese mismo tick");
    simulation.Step(1.f / 60.f, {{1, tool}});
    require(!table.overturned, "La segunda acción válida no restablece la mesa");
    agent.position = {340.f, 326.f}; agent.elevation = 0.f; agent.supportId = 1;
    ActionCommand attach; attach.traverse = true; simulation.Step(1.f / 60.f, {{1, attach}});
    require(agent.traversal.mode == TraversalMode::Attach, "La ruta de pared no inicia una transición física");
    for (unsigned int frame = 0; frame < 120; ++frame) simulation.Step(1.f / 60.f, {});
    require(agent.traversal.mode == TraversalMode::Hang, "La transición no llega al estado colgado");
    capture(renderer, world, output / "level-wall-hang.png", false);
    const auto missionGrip = [&](float x) {
      for (unsigned int frame = 0; frame < 600 && std::abs(agent.position.x - x) > .05f; ++frame) {
        ActionCommand move; move.moveX = std::clamp((x - agent.position.x) / (TraversalConfig::ShimmySpeed / 60.f), -1.f, 1.f);
        simulation.Step(1.f / 60.f, {{agent.id, move}});
      }
      simulation.Step(1.f / 60.f, {});
      require(std::abs(agent.position.x - x) <= .05f && agent.traversal.mode == TraversalMode::Hang, "La galería no recorre físicamente el agarre militar");
    };
    const auto missionTransfer = [&] {
      simulation.Step(1.f / 60.f, {{agent.id, attach}});
      for (unsigned int frame = 0; frame < 240 && agent.traversal.mode != TraversalMode::Hang; ++frame) simulation.Step(1.f / 60.f, {});
      require(agent.traversal.mode == TraversalMode::Hang, "La transferencia militar no alcanza un agarre estable");
    };
    missionGrip(500.f); missionTransfer(); missionGrip(600.f); missionTransfer(); missionGrip(620.f);
    shadowCapture(renderer, world, agent, 1, 0.f, "level-pipe-shadow-before-bridge");
    missionGrip(700.f);
    require(agent.traversal.surfaceId == 43 && agent.position.y == 330.f && agent.elevation == 96.f,
      "El escenario de sombra no llega al tubo militar mediante controles");
    shadowCapture(renderer, world, agent, 12, 96.f, "level-pipe-shadow-on-bridge");

    Simulation training; training.ResetBase();
    auto &trainingWorld = const_cast<WorldState &>(training.GetState()); auto &trainee = trainingWorld.agents.front();
    trainee.position = {340.f, 1366.f}; trainee.elevation = 0.f; trainee.supportId = 5;
    trainee.facing = {0.f, -1.f}; trainee.weaponAim = trainee.facing;
    const auto trainingGeometry = Level::GeometryHash(trainingWorld);
    WorldRenderer trainingRenderer(context);
    nlohmann::json trainingViews = nlohmann::json::array();
    const auto trainingCapture = [&](const char *name) {
      const auto image = capture(trainingRenderer, trainingWorld, output / name, false);
      trainingViews.push_back({{"file", name}, {"support", trainee.supportId}, {"grip", trainee.traversal.surfaceId}, {"elevation", trainee.elevation}});
      return image;
    };
    require(IsTraining(trainingWorld) && Traversal::CanTraverse(trainingWorld, trainee), "La entrada de entrenamiento no ofrece el agarre físico");
    const auto entry = trainingCapture("training-course-entry.png");
    auto noGrips = trainingWorld; noGrips.gripSurfaces.clear();
    const auto entryWithoutGrips = capture(trainingRenderer, noGrips, output / "training-course-entry-without-grips.png", false);
    std::size_t groundGripPixels = 0;
    // Se excluye el minimapa para comprobar el agarre visible desde el suelo en el propio mundo.
    for (unsigned int y = 0; y < entry.getSize().y; ++y) for (unsigned int x = 0; x < entry.getSize().x * 3 / 4; ++x)
      groundGripPixels += entry.getPixel({x, y}) != entryWithoutGrips.getPixel({x, y});
    require(groundGripPixels > 0, "Los agarres de entrenamiento no dibujan píxeles visibles desde el suelo");
    const auto settle = [&](TraversalMode mode, const char *message) {
      for (unsigned int frame = 0; frame < 240 && trainee.traversal.mode != mode; ++frame) training.Step(1.f / 60.f, {});
      require(trainee.traversal.mode == mode, message);
    };
    const auto shimmyTo = [&](float x) {
      ActionCommand right; right.moveX = 1.f;
      for (unsigned int frame = 0; frame < 600 && trainee.position.x < x - .1f; ++frame) training.Step(1.f / 60.f, {{trainee.id, right}});
      training.Step(1.f / 60.f, {});
      require(std::abs(trainee.position.x - x) < 1.f && trainee.traversal.mode == TraversalMode::Hang, "El desplazamiento lateral no alcanza el enlace del circuito");
    };
    ActionCommand grab; grab.traverse = true;
    training.Step(1.f / 60.f, {{trainee.id, grab}});
    settle(TraversalMode::Hang, "B no completa el agarre de la pared de entrenamiento");
    require(trainee.traversal.surfaceId == 41 && trainee.supportId == 0, "El agente no queda sostenido por la pared de entrenamiento");
    trainingCapture("training-course-wall-hang.png");
    shimmyTo(500.f); training.Step(1.f / 60.f, {{trainee.id, grab}});
    settle(TraversalMode::Hang, "B no completa el cambio al saliente de entrenamiento");
    require(trainee.traversal.surfaceId == 42 && trainee.elevation == TrainingConfig::TraversalCourse::Storey, "El cambio de agarre no alcanza el saliente superior");
    trainingCapture("training-course-ledge.png");
    shimmyTo(600.f); training.Step(1.f / 60.f, {{trainee.id, grab}});
    settle(TraversalMode::Hang, "B no completa el cambio al tubo de entrenamiento");
    require(trainee.traversal.surfaceId == 43 && trainee.position.y == 1370.f, "El saliente no enlaza físicamente con el tubo de entrenamiento");
    trainingCapture("training-course-pipe.png");
    shimmyTo(700.f);
    require(trainee.elevation == 96.f, "La captura del tubo de prácticas pierde la elevación física del cuerpo");
    shadowCapture(trainingRenderer, trainingWorld, trainee, 5, 0.f, "training-pipe-shadow-on-ground");
    shimmyTo(820.f); training.Step(1.f / 60.f, {{trainee.id, grab}});
    settle(TraversalMode::Grounded, "B no completa la salida del tubo de entrenamiento");
    require(trainee.supportId == 11 && trainee.elevation == TrainingConfig::TraversalCourse::Storey, "La salida del tubo no deja al agente en la plataforma real");
    trainingCapture("training-course-platform.png");
    ActionCommand walk; walk.moveY = -1.f;
    for (unsigned int frame = 0; frame < 300 && trainee.position.y > 1250.f; ++frame) training.Step(1.f / 60.f, {{trainee.id, walk}});
    walk = {}; walk.moveX = -1.f;
    for (unsigned int frame = 0; frame < 600 && trainee.position.x > 500.f; ++frame) training.Step(1.f / 60.f, {{trainee.id, walk}});
    training.Step(1.f / 60.f, {});
    require(trainee.supportId == 10 && std::abs(trainee.position.x - 500.f) < 3.f && trainee.elevation == TrainingConfig::TraversalCourse::Storey,
      "La vista general no alcanza la plataforma por el puente físico");
    trainingRenderer.AdjustZoom(-100.f); trainingCapture("training-course-overview.png");
    require(Level::GeometryHash(trainingWorld) == trainingGeometry, "El recorrido visual altera la geometría del circuito");

    Simulation depth; auto &depthWorld = MultilevelTests::QuietMission(depth); auto &depthActor = depthWorld.agents.front();
    const auto depthGeometry = Level::GeometryHash(depthWorld);
    const auto depthFloor = std::ranges::find(depthWorld.walkSurfaces, 1u, &WalkSurface::id);
    require(depthFloor != depthWorld.walkSurfaces.end(), "La muestra del velo no encuentra su suelo físico");
    const auto sourceGroundMaterial = depthFloor->material;
    // El acero contrasta con el tinte azul; se conserva la misma geometría y ROI en las tres alturas.
    depthFloor->material = SurfaceMaterial::Steel;
    depthWorld.phase = GamePhase::Base; depthWorld.mode = SimulationMode::Operation;
    depthActor.position = {820.f, 220.f}; depthActor.facing = {1.f, 0.f}; depthActor.weaponAim = depthActor.facing;
    WorldRenderer depthRenderer(context); depthRenderer.AdjustZoom(-100.f);
    const auto patch = [&](const sf::Image &pixels, sf::Vector2f point, float surfaceElevation) {
      const auto logical = context.window.GetLogicalSize();
      const auto screen = WorldRenderer::ProjectGround(point, depthActor.position, logical, depthRenderer.GetZoom()) +
        sf::Vector2f{0.f, (depthActor.elevation - surfaceElevation) * depthRenderer.GetZoom()};
      const auto center = screen.componentWiseMul(sf::Vector2f(pixels.getSize()).componentWiseDiv(logical));
      require(center.x >= 4.f && center.y >= 4.f && center.x + 4.f < pixels.getSize().x && center.y + 4.f < pixels.getSize().y,
        "La muestra del velo queda fuera de la imagen de evidencia");
      std::array<double, 3> color{};
      for (int y = -3; y <= 3; ++y) for (int x = -3; x <= 3; ++x) {
        const auto pixel = pixels.getPixel(sf::Vector2u(center + sf::Vector2f{static_cast<float>(x), static_cast<float>(y)}));
        color[0] += pixel.r / 49.; color[1] += pixel.g / 49.; color[2] += pixel.b / 49.;
      }
      return color;
    };
    std::array<std::array<double, 3>, 3> groundColors{};
    std::array<std::array<double, 3>, 2> middleColors{};
    const std::array<std::uint32_t, 3> depthSupports{{1, 11, 23}};
    for (std::size_t index = 0; index < depthSupports.size(); ++index) {
      const auto *surface = WorldGeometry::FindSurface(depthWorld, depthSupports[index]);
      require(surface && WorldGeometry::ContainsSupport(*surface, depthActor.position), "La galería del velo sitúa el observador fuera de una planta real");
      depthActor.supportId = surface->id; depthActor.elevation = WorldGeometry::SurfaceHeight(*surface, depthActor.position);
      const auto name = "level-lower-floor-veil-z-" + std::to_string(static_cast<int>(depthActor.elevation)) + ".png";
      const auto pixels = capture(depthRenderer, depthWorld, output / name, false);
      groundColors[index] = patch(pixels, {700.f, 360.f}, 0.f);
      if (index > 0) middleColors[index - 1] = patch(pixels, {866.f, 338.f}, 96.f);
      lowerFloorViews.push_back({{"file", name}, {"support", surface->id}, {"viewerElevation", depthActor.elevation}, {"currentFloorOpacity", lowerFog(depthActor.elevation, depthActor.elevation)},
        {"groundSample", {700.f, 360.f, 0.f}}, {"groundOpacity", lowerFog(depthActor.elevation, 0.f)}, {"groundMeanRgb", groundColors[index]},
        {"middleOpacity", lowerFog(depthActor.elevation, 96.f)}, {"isolatedHeightEffect", true}, {"geometryUnchanged", true},
        {"groundMaterialFixture", static_cast<int>(depthFloor->material)}, {"sourceGroundMaterial", static_cast<int>(sourceGroundMaterial)}});
      if (index > 0) {
        lowerFloorViews.back()["middleSample"] = {866.f, 338.f, 96.f};
        lowerFloorViews.back()["middleMeanRgb"] = middleColors[index - 1];
      }
    }
    const auto colorDifference = [](const std::array<double, 3> &a, const std::array<double, 3> &b) {
      return std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
    };
    const auto fogTint = PresentationConfig::LowerFloorFogTint;
    const std::array<double, 3> tintColor{{static_cast<double>(fogTint.r), static_cast<double>(fogTint.g), static_cast<double>(fogTint.b)}};
    require(colorDifference(groundColors[0], tintColor) > 32., "El material de control no ofrece contraste suficiente para medir el velo");
    require(colorDifference(groundColors[0], groundColors[1]) > 8. && colorDifference(groundColors[0], groundColors[2]) > colorDifference(groundColors[0], groundColors[1]) + 4.,
      "El suelo inferior conserva los mismos píxeles al observar desde Z96/Z192 o no intensifica el velo");
    require(colorDifference(middleColors[0], middleColors[1]) > 8., "La planta Z96 no pasa de nítida a velada al observar desde Z192");
    const auto blended = [&](const std::array<double, 3> &base, float opacity) {
      const auto tint = PresentationConfig::LowerFloorFogTint;
      return std::array<double, 3>{{base[0] * (1.f - opacity) + tint.r * opacity, base[1] * (1.f - opacity) + tint.g * opacity, base[2] * (1.f - opacity) + tint.b * opacity}};
    };
    require(colorDifference(groundColors[1], blended(groundColors[0], lowerFog(96.f, 0.f))) < 24. &&
      colorDifference(groundColors[2], blended(groundColors[0], lowerFog(192.f, 0.f))) < 24. &&
      colorDifference(middleColors[1], blended(middleColors[0], lowerFog(192.f, 96.f))) < 24.,
      "Los píxeles del piso inferior no corresponden al tinte y la intensidad de altura configurados");
    depthFloor->material = sourceGroundMaterial;
    require(Level::GeometryHash(depthWorld) == depthGeometry, "Aislar la presentación del velo modifica la geometría militar");

    WorldRenderer transitionRenderer(context);
    const auto transitionCapture = [&](const WorldState &scene, const Agent &actor, const std::string &name, const char *kind) {
      const auto *surface = WorldGeometry::FindSurface(scene, actor.supportId);
      require(surface && std::abs(WorldGeometry::SurfaceHeight(*surface, actor.position) - actor.elevation) <= LevelConfig::SupportTolerance,
        "La captura intermedia deja al observador fuera de su apoyo físico");
      require(lowerFog(actor.elevation, WorldGeometry::SurfaceHeight(*surface, actor.position)) == 0.f,
        "La escalera o cabina actual recibe niebla por conservar el índice de una planta inferior");
      capture(transitionRenderer, scene, output / name, false);
      heightTransitions.push_back({{"file", name}, {"kind", kind}, {"support", actor.supportId}, {"layer", surface->layer},
        {"position", {actor.position.x, actor.position.y}}, {"elevation", actor.elevation},
        {"groundOpacity", lowerFog(actor.elevation, 0.f)}, {"currentSupportOpacity", lowerFog(actor.elevation, WorldGeometry::SurfaceHeight(*surface, actor.position))}});
    };
    const std::array<float, 5> heights{{0.f, 24.f, 48.f, 72.f, 96.f}};
    Simulation stairs; auto &stairWorld = MultilevelTests::QuietMission(stairs); auto &stairActor = stairWorld.agents.front();
    stairActor.position = {240.f, 510.f}; stairActor.elevation = 0.f; stairActor.supportId = 1; stairActor.facing = {0.f, -1.f}; stairActor.weaponAim = stairActor.facing;
    const auto stairGeometry = Level::GeometryHash(stairWorld);
    transitionCapture(stairWorld, stairActor, "level-stair-veil-z-0.png", "stair");
    std::size_t stairHeight = 1;
    ActionCommand ascend; ascend.moveY = -1.f;
    for (unsigned int frame = 0; frame < 240 && stairHeight < heights.size(); ++frame) {
      const float previous = stairActor.elevation;
      stairs.Step(1.f / 60.f, {{stairActor.id, ascend}});
      require(stairActor.elevation >= previous && stairActor.elevation - previous <= GameplayConfig::WalkSpeed / 60.f + .01f,
        "La galería del velo sustituye la escalera física por un salto de altura");
      if (stairActor.elevation >= heights[stairHeight]) {
        transitionCapture(stairWorld, stairActor, "level-stair-veil-z-" + std::to_string(static_cast<int>(heights[stairHeight])) + ".png", "stair");
        ++stairHeight;
      }
    }
    require(stairHeight == heights.size() && Level::GeometryHash(stairWorld) == stairGeometry, "La galería no alcanza todas las alturas de la escalera o modifica su geometría");
    Simulation elevator; elevator.ResetBase(); auto &liftWorld = const_cast<WorldState &>(elevator.GetState()); auto &liftActor = liftWorld.agents.front();
    liftActor.position = TrainingConfig::TraversalCourse::ElevatorPosition; liftActor.elevation = 0.f; liftActor.supportId = TrainingConfig::TraversalCourse::CabinSupport;
    transitionCapture(liftWorld, liftActor, "training-elevator-veil-z-0.png", "elevator");
    ActionCommand travel; travel.interact = true; elevator.Step(1.f / 60.f, {{liftActor.id, travel}});
    require(liftWorld.elevators.front().requested, "La galería del velo no solicita el ascensor mediante E");
    std::size_t liftHeight = 1;
    for (unsigned int frame = 0; frame < 600 && liftHeight < heights.size(); ++frame) {
      const float previous = liftActor.elevation;
      elevator.Step(1.f / 60.f, {});
      require(liftActor.supportId == TrainingConfig::TraversalCourse::CabinSupport && liftActor.elevation == liftWorld.elevators.front().elevation &&
        liftActor.elevation >= previous && liftActor.elevation - previous <= LevelConfig::ElevatorSpeed / 60.f + .01f,
        "La galería del velo no conserva la altura continua del ocupante y la cabina");
      if (liftActor.elevation >= heights[liftHeight]) {
        transitionCapture(liftWorld, liftActor, "training-elevator-veil-z-" + std::to_string(static_cast<int>(heights[liftHeight])) + ".png", "elevator");
        ++liftHeight;
      }
    }
    require(liftHeight == heights.size(), "La galería no alcanza las alturas intermedias del ascensor de prácticas");
    auto report = nlohmann::json{{"status", "PASS"}, {"checks", checks}, {"views", views}, {"floorOcclusion", true}, {"instantTableSpriteAndCollision", true}, {"physicalWallAttach", true}, {"traversalShadows", shadowViews},
      {"heightTransitions", heightTransitions},
      {"lowerFloorVeil", lowerFloorViews},
      {"lowerFloorFogTint", {PresentationConfig::LowerFloorFogTint.r, PresentationConfig::LowerFloorFogTint.g, PresentationConfig::LowerFloorFogTint.b}},
      {"training", {{"groundGripPixels", groundGripPixels}, {"physicalTraversal", true}, {"bridgeWalk", true}, {"views", trainingViews}}}};
    std::ofstream(output / "level-fixtures.json") << report.dump(2);
    return report;
  }
}
