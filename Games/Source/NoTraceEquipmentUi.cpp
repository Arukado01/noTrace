#include "NoTraceGame.h"
#include "Config/NoTraceConfig.h"
#include "Config/NoTraceWeaponConfig.h"
#include <algorithm>
#include <format>
#include <TGUI/Backend/Renderer/SFML-Graphics/CanvasSFML.hpp>
#include <SFML/Graphics/ConvexShape.hpp>
#include <SFML/Graphics/RectangleShape.hpp>

namespace NoTrace {
  void Game::BuildWeapons() {
    panel_->setSize({780.f, 330.f}); panel_->setPosition({410.f, 280.f});
    for (int index = 0; index < 2; ++index) {
      auto slot = tgui::Button::create(); slot->setPosition({30.f + index * 375.f, 25.f}); slot->setSize({345.f, 275.f});
      slot->onPress([this, index] { pendingWeaponChoice_ = index; }); panel_->add(slot); weaponSlots_[index] = slot;
      auto art = tgui::CanvasSFML::create({310.f, 180.f}); art->setPosition({48.f + index * 375.f, 45.f}); art->setIgnoreMouseEvents(true);
      art->clear(sf::Color::Transparent);
      const sf::Color steel{175, 192, 197}, shadow{54, 70, 78}, accent = index == 0 ? sf::Color{214, 151, 116} : sf::Color{102, 203, 194};
      const auto block = [&](sf::Vector2f position, sf::Vector2f size, sf::Color color) {
        sf::RectangleShape shape(size); shape.setPosition(position); shape.setFillColor(color); art->draw(shape);
      };
      if (index == 0) {
        block({38.f, 50.f}, {225.f, 36.f}, steel); block({246.f, 56.f}, {29.f, 24.f}, shadow);
        block({72.f, 86.f}, {45.f, 12.f}, steel); block({72.f, 98.f}, {9.f, 22.f}, steel); block({72.f, 120.f}, {45.f, 8.f}, steel);
        sf::ConvexShape grip(4); grip.setPoint(0,{39.f,84.f}); grip.setPoint(1,{74.f,84.f}); grip.setPoint(2,{92.f,155.f}); grip.setPoint(3,{47.f,155.f}); grip.setFillColor(shadow); art->draw(grip);
        block({44.f, 59.f}, {21.f, 6.f}, accent); block({229.f, 44.f}, {7.f, 6.f}, shadow);
      } else {
        block({28.f, 57.f}, {218.f, 30.f}, shadow); block({112.f, 42.f}, {102.f, 49.f}, steel);
        block({245.f, 50.f}, {39.f, 42.f}, accent); block({61.f, 90.f}, {34.f, 66.f}, steel);
        block({103.f, 91.f}, {58.f, 8.f}, steel); block({153.f, 91.f}, {8.f, 29.f}, steel); block({100.f, 113.f}, {61.f, 8.f}, steel);
        for (int cell = 0; cell < 3; ++cell) block({122.f + cell * 26.f, 52.f}, {14.f, 25.f}, accent);
        block({37.f, 87.f}, {15.f, 19.f}, accent);
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
    Label("MOVIMIENTOS TÁCTICOS / TECLAS", {30.f, 20.f}, {714.f, 45.f}, 25);
    Label("Zambullida: correr de pie y pulsar la tecla.\nRodada: estar agachado y pulsar la tecla.\nSe conservan F, R, ALT y los botones del ratón.", {30.f, 80.f}, {714.f, 110.f}, 19);
    const auto selector = [this](const char *name, float y, int selected) {
      Label(name, {30.f, y}, {300.f, 38.f}, 20);
      auto box = tgui::ComboBox::create(); box->setPosition({340.f, y}); box->setSize({404.f, 40.f}); box->setTextSize(20);
      for (const char *key : {"ESPACIO", "CTRL IZQUIERDO", "X", "J", "K"}) box->addItem(key);
      box->setSelectedItemByIndex(std::clamp(selected, 0, 4)); panel_->add(box); return box;
    };
    auto dive = selector("Zambullida", 230.f, ctx.save.Get("NoTrace:DiveKey", 0));
    auto roll = selector("Rodada", 300.f, ctx.save.Get("NoTrace:RollKey", 1));
    auto status = Label("Elige teclas distintas para ambas acciones.", {30.f, 365.f}, {714.f, 60.f}, 18);
    Button("GUARDAR CONTROLES", 450.f, [this, dive, roll, status] {
      const int diveKey = dive->getSelectedItemIndex(), rollKey = roll->getSelectedItemIndex();
      if (diveKey < 0 || rollKey < 0 || diveKey == rollKey) { status->setText("Selecciona dos teclas diferentes."); return; }
      ctx.save.Set("NoTrace:DiveKey", diveKey); ctx.save.Set("NoTrace:RollKey", rollKey);
      ctx.input.Clear(); BindInputs(); Open(Page::Options);
    });
    Button("VOLVER A OPCIONES", 520.f, [this] { Open(Page::Options); });
  }
}
