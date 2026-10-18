#pragma once

#include "Config/NoTraceFirstPersonArtConfig.h"
#include "Types/NoTraceGameplayTypes.h"
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/Rect.hpp>

struct EngineContext;

namespace NoTrace {
  class FirstPersonArt {
  public:
    struct Frame { sf::IntRect rect; sf::Vector2f anchor; };
    explicit FirstPersonArt(EngineContext &context);
    const sf::Image &ActorsPixels() const;
    const sf::Image &ViewmodelPixels() const;
    static Frame ActorFrame(unsigned int variant, unsigned int state);
    static Frame ViewmodelFrame(WeaponKind weapon, bool suppressed, unsigned int state);
  private:
    sf::Image actors_, viewmodels_;
  };
}
