#include "NoTraceBackdrop.h"
#include "Config/NoTraceConfig.h"
#include "Core/EngineConfig.h"

#include <algorithm>
#include <stdexcept>

namespace NoTrace {
  namespace {
    sf::IntRect VisibleBounds(const sf::Texture &texture) {
      const auto image = texture.copyToImage();
      const auto size = image.getSize();
      unsigned int left = size.x, top = size.y, right = 0, bottom = 0;
      for (unsigned int y = 0; y < size.y; ++y) {
        for (unsigned int x = 0; x < size.x; ++x) {
          if (image.getPixel({x, y}).a >= 64) {
            left = std::min(left, x);
            top = std::min(top, y);
            right = std::max(right, x + 1);
            bottom = std::max(bottom, y + 1);
          }
        }
      }
      if (right <= left || bottom <= top) {
        return {{0, 0}, sf::Vector2i(size)};
      }
      return {{static_cast<int>(left), static_cast<int>(top)},
              {static_cast<int>(right - left), static_cast<int>(bottom - top)}};
    }
  }

  Backdrop::Backdrop(EngineContext &context) : ctx_(context),
      background_(ctx_.resources.FetchTexture(std::string(BACKGROUND_TEXTURE))),
      logo_(ctx_.resources.FetchTexture(std::string(LOGO_TEXTURE))),
      emblem_(ctx_.resources.FetchTexture(std::string(EMBLEM_TEXTURE))) {
    if (!background_ || !logo_ || !emblem_) {
      throw std::runtime_error("Faltan recursos visuales de NO TRACE.");
    }
    logoBounds_ = VisibleBounds(*logo_);
  }

  void Backdrop::Render() const {
    const auto size = gConfig.windowSize;
    const auto imageSize = sf::Vector2f(background_->getSize());
    const float scale = std::max(size.x / imageSize.x, size.y / imageSize.y) * BACKGROUND_ZOOM;
    sf::Sprite background(*background_);
    background.setOrigin(imageSize / 2.f);
    background.setScale({scale, scale});
    background.setPosition(size / 2.f);
    ctx_.renderer.Draw(background);

    sf::VertexArray shade(sf::PrimitiveType::TriangleStrip, 4);
    shade[0] = {{0, 0}, sf::Color(0, 3, 7, 210)};
    shade[1] = {{0, size.y}, sf::Color(0, 3, 7, 225)};
    shade[2] = {{size.x * 0.66f, 0}, sf::Color(0, 3, 7, 0)};
    shade[3] = {{size.x * 0.66f, size.y}, sf::Color(0, 3, 7, 0)};
    ctx_.renderer.Draw(shade);

    sf::Sprite logo(*logo_, logoBounds_);
    const float logoScale = (size.x * 0.46f) / static_cast<float>(logoBounds_.size.x);
    logo.setScale({logoScale, logoScale});
    logo.setPosition({size.x * 0.055f, size.y * 0.085f});
    ctx_.renderer.Draw(logo);

    sf::Sprite emblem(*emblem_);
    const float emblemScale = 64.f / static_cast<float>(emblem_->getSize().x);
    emblem.setScale({emblemScale, emblemScale});
    emblem.setPosition({size.x - 126.f, 65.f});
    ctx_.renderer.Draw(emblem);
  }
}
