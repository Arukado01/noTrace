#pragma once

#include "Core/EngineContext.h"

namespace NoTrace {
  class Backdrop {
  private:
    EngineContext &ctx_;
    sf::Texture *background_;
    sf::Texture *logo_;
    sf::Texture *emblem_;
    sf::IntRect logoBounds_;

  public:
    explicit Backdrop(EngineContext &context);
    void Render() const;
  };
}
