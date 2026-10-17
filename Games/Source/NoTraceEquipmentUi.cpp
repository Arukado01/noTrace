#include "NoTraceGame.h"
#include "Config/NoTraceConfig.h"
#include <algorithm>
#include <format>

namespace NoTrace {
  void Game::BuildWeapons() {
    Label("EQUIPO TÁCTICO", {30.f, 20.f}, {714.f, 45.f}, 29);
    const auto *agent = LocalAgent();
    if (!agent) { Button("VOLVER", 520.f, [this] { ClosePanel(); }); return; }
    Label("Selecciona el arma. Clic derecho apunta; clic izquierdo dispara.\nH monta o desmonta el silenciador del arma seleccionada.", {30.f, 80.f}, {714.f, 70.f}, 18);
    for (int index = 0; index < 2; ++index) {
      const bool pistol = index == 0;
      const int magazine = pistol ? agent->lethalMagazine : agent->nonLethalMagazine;
      const int total = pistol ? agent->loadout.lethalAmmo : agent->loadout.nonLethalAmmo;
      const float durability = pistol ? agent->pistolSuppressor : agent->nonLethalSuppressor;
      const bool attached = pistol ? agent->pistolSuppressorEquipped : agent->nonLethalSuppressorEquipped;
      Button(std::format("{}  ·  {}/{}  ·  {}", pistol ? "1 / PISTOLA LETAL" : "2 / ARMA NO LETAL", magazine, total,
          durability <= 0.f ? "SILENCIADOR ROTO" : attached ? std::format("SIL. {:.0f}%", durability) : "SIN SILENCIADOR"),
          180.f + index * 90.f, [this, index] { pendingWeaponChoice_ = index; ClosePanel(); });
    }
    Label(std::format("EQUIPO ESPECIAL\nG / Discos señuelo: {}\nT / Herramientas: {}\nCambiar de arma conserva munición y desgaste.", agent->loadout.decoys, agent->loadout.tools),
        {30.f, 370.f}, {714.f, 130.f}, 20);
    Button("VOLVER AL JUEGO", 520.f, [this] { ClosePanel(); });
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
