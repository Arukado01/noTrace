#pragma once

#include "Config/NoTraceCharacterArtConfig.h"
#include <SFML/Graphics/Color.hpp>
#include <SFML/Graphics/Rect.hpp>
#include <array>
#include <cstddef>
#include <vector>

struct EngineContext;
namespace sf { class Texture; }

namespace NoTrace {
  class CharacterArt {
  public:
    enum class Layer : std::size_t { Full, Lower, Upper, CqcBody, CqcArms };
    enum class Weapon : std::size_t { Pistol, NonLethal, GuardRifle };
    struct Region { std::size_t page = 0; sf::IntRect rect; sf::Vector2f offset; };
    struct Frame {
      std::array<Region, 5> art;
      sf::Vector2f anchor, pelvis, carryGrip, muzzle{-1.f, -1.f}, grip{-1.f, -1.f};
      float supportWidth = 0.f;
      bool valid = false;
    };
    struct WeaponFrame { sf::IntRect rect; sf::Vector2f grip, muzzle; };
    explicit CharacterArt(EngineContext &context);
    const Frame &Metadata(std::size_t index) const;
    const WeaponFrame &WeaponMetadata(Weapon weapon, bool suppressed, std::size_t direction) const;
    static float Scale();
    sf::Vector2f Map(std::size_t index, sf::Vector2f point, sf::Vector2f feet) const;
    sf::Vector2f AlignUpper(std::size_t lower, std::size_t upper, sf::Vector2f feet) const;
    sf::Vector2f Muzzle(std::size_t index, sf::Vector2f feet, Weapon weapon, bool suppressed, std::size_t direction) const;
    void Draw(std::size_t index, Layer layer, sf::Vector2f feet, sf::Color color) const;
    void DrawWeapon(std::size_t index, sf::Vector2f feet, Weapon weapon, bool suppressed, std::size_t direction, sf::Color color, bool carry = false) const;
  private:
    EngineContext &context_;
    std::vector<sf::Texture *> pages_;
    sf::Texture *weapons_ = nullptr;
    std::vector<Frame> frames_;
    std::array<WeaponFrame, 48> weaponFrames_;
  };
}
