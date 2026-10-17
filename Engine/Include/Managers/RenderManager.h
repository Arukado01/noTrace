#pragma once
#include <SFML/Graphics.hpp>

#include <vector>
#include <memory>

#include "Graphics/Effect.h"

namespace EngineTests { struct GuiAccess; }

class RenderManager {
private:
  sf::RenderTexture target_;
  sf::Texture backgroundTexture_;
  sf::RectangleShape background_;

  std::vector<std::unique_ptr<Effect>> effects_;
  sf::RenderTexture effectsTarget_;
  bool postProcessingEnabled_ = true;

public:
  RenderManager();
  void Draw(const sf::Drawable &drawable);

  void SetView(const sf::View &view);
  void ResetView();
  void SetPostProcessingEnabled(bool enabled);
  void SetTextureSmoothing(bool enabled);

private:
  friend class Engine;
  friend struct EngineTests::GuiAccess;

  void BeginDrawing();
  const sf::Texture &FinishDrawing();
};
