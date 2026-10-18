#include "NoTraceGame.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include <algorithm>
#include <format>
#include <TGUI/Backend/Renderer/SFML-Graphics/CanvasSFML.hpp>
#include <SFML/Graphics/Sprite.hpp>

namespace NoTrace {
  void Game::BuildWeapons() {
    panel_->setSize({780.f, 330.f}); panel_->setPosition({410.f, 280.f});
    for (int index = 0; index < 2; ++index) {
      auto slot = tgui::Button::create(); slot->setPosition({30.f + index * 375.f, 25.f}); slot->setSize({345.f, 275.f});
      slot->onPress([this, index] { pendingWeaponChoice_ = index; }); panel_->add(slot); weaponSlots_[index] = slot;
      auto art = tgui::CanvasSFML::create({310.f, 180.f}); art->setPosition({48.f + index * 375.f, 45.f}); art->setIgnoreMouseEvents(true);
      art->clear(sf::Color::Transparent);
      if (const auto *texture = ctx.resources.FetchTexture("NoTrace/Characters/V2/WeaponSelection.png")) {
        sf::Sprite sprite(*texture, {{index * 320, 0}, {320, 192}});
        sprite.setPosition({5.f, 0.f}); sprite.setScale({.9375f, .9375f}); art->draw(sprite);
      }
      art->display(); panel_->add(art);
      weaponAmmo_[index] = Label("", {55.f + index * 375.f, 239.f}, {295.f, 48.f}, 30);
      weaponAmmo_[index]->setHorizontalAlignment(tgui::HorizontalAlignment::Center);
    }
    RefreshWeapons();
  }

  void Game::RefreshWeapons() {
    const auto *agent = LocalAgent(); if (!agent || page_ != Page::Weapons) return;
    for (int index = 0; index < 2; ++index) {
      if (!weaponSlots_[index] || !weaponAmmo_[index]) continue;
      const bool selected = (pendingWeaponChoice_ >= 0 ? pendingWeaponChoice_ : static_cast<int>(agent->selectedWeapon)) == index;
      auto renderer = weaponSlots_[index]->getRenderer();
      renderer->setBackgroundColor(selected ? tgui::Color{31, 62, 68} : tgui::Color{15, 28, 39});
      renderer->setBackgroundColorHover({47, 71, 78}); renderer->setBorderColor(selected ? tgui::Color{115, 230, 198} : tgui::Color{68, 86, 99});
      renderer->setBorders(selected ? 4.f : 1.f);
      weaponAmmo_[index]->setText(std::format("{} / {}", index == 0 ? agent->lethalMagazine : agent->nonLethalMagazine,
        index == 0 ? WeaponConfig::PistolCapacity : WeaponConfig::NonLethalCapacity));
    }
  }

  void Game::BuildControls() {
    BuildControlsUi(ctx, panel_, [this] { ctx.input.Clear(); BindInputs(); Open(Page::Options); },
                    [this] { Open(Page::Options); });
  }
}
