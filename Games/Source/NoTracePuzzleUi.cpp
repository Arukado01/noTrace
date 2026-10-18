#include "NoTraceGame.h"
#include "Config/NoTraceDevicesConfig.h"
#include "NoTraceCoordination.h"
#include <algorithm>
#include <array>
#include <format>
#include <memory>

namespace NoTrace {
  void Game::BuildPuzzle() {
    const auto *agent = LocalAgent(); if (!agent || !agent->puzzle.target) return;
    const auto &puzzle = agent->puzzle;
    puzzleStageRendered_ = puzzle.stage; puzzleTargetRendered_ = puzzle.target; puzzleProgress_.reset();
    coordinationButtons_.fill(nullptr); coordinationHold_.reset();
    Label("INTERVENCIÓN TÁCTICA CIO", {30.f, 20.f}, {714.f, 45.f}, 28);
    Label("La operación y los temporizadores continúan. ESC cancela.", {30.f, 76.f}, {714.f, 35.f}, 18);
    if (puzzle.family == PuzzleFamily::Arrows) {
      const std::array<std::string, 4> arrows{"ARRIBA", "DERECHA", "ABAJO", "IZQUIERDA"};
      const std::array<std::string, 4> keys{"W", "D", "S", "A"};
      const std::array<std::string, 4> glyphs{"^ ARRIBA", "> DERECHA", "V ABAJO", "< IZQUIERDA"};
      Label(std::format("SECUENCIA DE FLECHAS  ·  Paso {} / {}", puzzle.stage + 1, puzzle.length), {30.f, 130.f}, {714.f, 45.f}, 24);
      const std::array<tgui::Color, 3> stageColors{{{95, 112, 125}, {226, 173, 78}, {110, 200, 147}}};
      for (unsigned int index = 0; index < puzzle.sequence.size(); ++index) {
        if (puzzle.sequence[index] < 1 || puzzle.sequence[index] > glyphs.size()) continue;
        auto stage = Label(glyphs[puzzle.sequence[index] - 1], {30.f + static_cast<float>(index % 3) * 238.f, 250.f + static_cast<float>(index / 3) * 38.f}, {226.f, 34.f}, 18);
        stage->getRenderer()->setTextColor(stageColors[index < puzzle.stage ? 2 : index == puzzle.stage ? 1 : 0]);
        stage->getRenderer()->setBackgroundColor(index == puzzle.stage ? tgui::Color{43, 49, 48} : tgui::Color{13, 23, 31});
        stage->getRenderer()->setBorders(1); stage->getRenderer()->setBorderColor(stageColors[index < puzzle.stage ? 2 : index == puzzle.stage ? 1 : 0]);
      }
      Label("Reproduce el circuito con W, A, S y D. Cada pulsación avanza un paso.", {30.f, 192.f}, {714.f, 48.f}, 18);
      for (unsigned int index = 0; index < arrows.size(); ++index) {
        auto button = Button(keys[index] + "  " + arrows[index], 340.f, [this, index] { pendingPuzzleInput_ = static_cast<int>(index + 1); });
        button->setPosition({30.f + index * 183.f, 340.f}); button->setSize({165.f, 58.f}); button->setTextSize(18);
      }
    } else if (puzzle.family == PuzzleFamily::Wires) {
      const std::array<tgui::Color, 4> colors{{{170, 58, 65}, {48, 119, 175}, {159, 127, 37}, {50, 139, 93}}};
      Label(std::format("CABLEADO  ·  {} / 4 enlaces aislados", puzzle.stage), {30.f, 122.f}, {714.f, 40.f}, 24);
      auto selection = Label("Elige un cable y después el puerto del mismo color y letra.", {30.f, 166.f}, {714.f, 55.f}, 18);
      auto selected = std::make_shared<unsigned int>(0);
      for (unsigned int index = 0; index < DevicesConfig::WireCount; ++index) {
        const unsigned int source = index + 1, portLabel = puzzle.wireLabels[index];
        auto input = Button("CABLE " + std::string(1, static_cast<char>('A' + index)), 230.f + index * 53.f, [selected, source, selection] {
          *selected = source; selection->setText("Cable " + std::string(1, static_cast<char>('A' + source - 1)) + " seleccionado: elige su puerto de destino.");
        });
        input->setPosition({30.f, 230.f + index * 53.f}); input->setSize({245.f, 43.f}); input->getRenderer()->setBackgroundColor(colors[index]);
        input->setEnabled(!(puzzle.connectedSources & (1u << index)));
        auto output = Button("PUERTO " + std::to_string(index + 1) + "  /  " + std::string(1, static_cast<char>('A' + portLabel - 1)), 230.f + index * 53.f, [this, selected, index] {
          if (*selected != 0) pendingPuzzleInput_ = WireInput(*selected, index + 1);
        });
        output->setPosition({499.f, 230.f + index * 53.f}); output->setSize({245.f, 43.f});
        if (portLabel >= 1 && portLabel <= colors.size()) output->getRenderer()->setBackgroundColor(colors[portLabel - 1]);
        output->setEnabled(!(puzzle.connectedPorts & (1u << index)));
        Label((puzzle.connectedSources & (1u << index)) ? "AISLADO" : "CONECTAR  >", {294.f, 239.f + index * 53.f}, {190.f, 30.f}, 16);
      }
    } else if (puzzle.family == PuzzleFamily::Coordination) {
      Label("SINCRONIZACIÓN DE INTERRUPTORES", {30.f, 136.f}, {714.f, 45.f}, 24);
      const auto &objects = session_.GetWorld().objects;
      const auto device = std::ranges::find(objects, puzzle.target, &Interactable::id);
      const bool auxiliary = device != objects.end() && device->puzzle.soloAllowed;
      Label(auxiliary ? "Mantén tu botón. Un único agente vivo habilita el relé auxiliar.\nAl soltar, la conexión y el progreso se reinician." :
        "Cada operador mantiene un botón distinto; no hay relé auxiliar.\nAl soltar, la conexión y el progreso se reinician.", {30.f, 194.f}, {714.f, 55.f}, 18);
      if (device != objects.end()) {
        const auto count = Coordination::ButtonCount(*device);
        const float width = 714.f / static_cast<float>(count);
        for (unsigned int index = 0; index < count; ++index) {
          auto indicator = Label("", {30.f + width * index, 256.f}, {width - 10.f, 54.f}, 16);
          indicator->getRenderer()->setBorders(2); coordinationButtons_[index] = indicator;
        }
      }
      puzzleProgress_ = tgui::ProgressBar::create(); puzzleProgress_->setPosition({30.f, 320.f}); puzzleProgress_->setSize({714.f, 34.f});
      puzzleProgress_->setMaximum(100); puzzleProgress_->setValue(static_cast<unsigned int>(std::clamp(puzzle.coordinationProgress / DevicesConfig::CoordinationHoldDuration, 0.f, 1.f) * 100.f));
      puzzleProgress_->getRenderer()->setFillColor({47, 133, 113}); puzzleProgress_->getRenderer()->setBackgroundColor({18, 32, 45}); panel_->add(puzzleProgress_);
      coordinationHold_ = Button("MANTENER INTERRUPTOR", 380.f, [] {});
      coordinationHold_->onMousePress([this] { pendingPuzzleHeld_ = true; });
      coordinationHold_->onMouseRelease([this] { pendingPuzzleHeld_ = false; });
      coordinationHold_->onMouseLeave([this] { pendingPuzzleHeld_ = false; });
      RefreshPuzzleControls();
    } else if (puzzle.family == PuzzleFamily::Levers) {
      Label("SECUENCIA DE PALANCAS DISTRIBUIDAS", {30.f, 130.f}, {714.f, 45.f}, 24);
      std::string sequence;
      for (const auto node : puzzle.sequence) { if (!sequence.empty()) sequence += "  >  "; sequence += static_cast<char>('A' + node - 1); }
      Label(sequence, {30.f, 190.f}, {714.f, 55.f}, 32);
      Label(std::format("Progreso: {} / {}. Cierra el panel y pulsa E en las palancas.\nSigue el orden indicado. Un error reinicia el circuito.", puzzle.stage, puzzle.length), {30.f, 255.f}, {714.f, 92.f}, 18);
      Label("Busca las palancas A, B, C y D junto al computador.\nSus letras aparecen cuando puedes verlas.", {30.f, 360.f}, {714.f, 65.f}, 18);
    }
    Button(puzzle.family == PuzzleFamily::Levers ? "VOLVER A LAS PALANCAS" : "CANCELAR SIN RESOLVER", 480.f, [this] { pendingPuzzleHeld_ = false; pendingPuzzleInput_ = 0; });
  }

  void Game::RefreshPuzzleControls() {
    const auto *agent = LocalAgent();
    if (!agent || agent->puzzle.family != PuzzleFamily::Coordination) return;
    const auto &objects = session_.GetWorld().objects;
    const auto device = std::ranges::find(objects, agent->puzzle.target, &Interactable::id);
    if (device == objects.end()) return;
    for (unsigned int index = 0; index < coordinationButtons_.size(); ++index) {
      const auto &indicator = coordinationButtons_[index];
      if (!indicator) continue;
      const auto bit = 1u << index;
      const bool pressed = (device->puzzle.coordinationMask & bit) != 0;
      const bool reserved = (device->puzzle.coordinationReservedMask & bit) != 0;
      const bool own = agent->puzzle.coordinationButton == index + 1;
      const bool locked = device->puzzle.state == PuzzleState::Failed || device->puzzle.cooldown > 0.f;
      const auto color = locked ? tgui::Color{213, 75, 84} : pressed ? tgui::Color{110, 200, 147} : reserved ? tgui::Color{226, 173, 78} : tgui::Color{103, 178, 197};
      const char *state = locked ? "BLOQUEADO" : pressed ? "PRESIONADO" : reserved ? "RESERVADO" : "DISPONIBLE";
      indicator->setText(std::format("CONTROL {}{}\n{}", static_cast<char>('A' + index), own ? " · TUYO" : "", state));
      indicator->getRenderer()->setTextColor(color); indicator->getRenderer()->setBorderColor(color);
      indicator->getRenderer()->setBackgroundColor(pressed ? tgui::Color{20, 53, 43} : tgui::Color{13, 23, 31});
    }
  }
}
