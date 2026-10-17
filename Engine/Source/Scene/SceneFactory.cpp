#include "Scene/SceneFactory.h"

#include "Bounce.h"
#include "Clicker.h"
#include "NoTrace.h"
#include "NoTraceGame.h"
#include "Config/NoTraceGameplayConfig.h"
#include "Config/NoTraceConfig.h"

namespace SceneFactory {
  Scenes CreateScenes(EngineContext &context) {
    Scenes scenes;

    scenes.emplace(NoTrace::MENU_SCENE, std::make_unique<NoTrace::Menu>(context));
    scenes.emplace(NoTrace::RECEPTION_SCENE, std::make_unique<NoTrace::Reception>(context));
    scenes.emplace(std::string(NoTrace::GameplayConfig::GameScene), std::make_unique<NoTrace::Game>(context));

    scenes.emplace("Bounce", std::make_unique<Bounce::Game>(context));
    scenes.emplace("Clicker", std::make_unique<Clicker::Game>(context));

    return scenes;
  }
} // namespace SceneFactory
