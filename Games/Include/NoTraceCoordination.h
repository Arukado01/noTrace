#pragma once

#include "Types/NoTraceGameplayTypes.h"
#include <algorithm>
#include <limits>

namespace NoTrace::Coordination {
  inline unsigned int ButtonCount(const Interactable &object) {
    return std::clamp(object.puzzle.requiredAgents, DevicesConfig::CoordinationMinimumButtons, DevicesConfig::CoordinationMaximumButtons);
  }
  inline unsigned int FullMask(const Interactable &object) { return (1u << ButtonCount(object)) - 1u; }
  inline unsigned int ButtonBit(unsigned int button) { return button >= 1 && button <= DevicesConfig::CoordinationMaximumButtons ? 1u << (button - 1u) : 0u; }
  inline sf::Vector2f ButtonPosition(const Interactable &object, unsigned int button) {
    const auto facing = object.facing.lengthSquared() > 0.01f ? object.facing.normalized() : sf::Vector2f{1.f, 0.f};
    const sf::Vector2f side{-facing.y, facing.x};
    const unsigned int index = std::clamp(button, 1u, ButtonCount(object)) - 1u;
    const float lateral = (index % 2 == 0 ? -0.5f : 0.5f) * DevicesConfig::CoordinationButtonSpacing;
    const float depth = ButtonCount(object) > 2 ? (index < 2 ? -0.35f : 0.35f) * DevicesConfig::CoordinationButtonSpacing : 0.f;
    return object.position + side * lateral + facing * depth;
  }
  inline unsigned int RequiredAgents(const WorldState &world, const Interactable &object) {
    const auto available = std::ranges::count_if(world.agents, [](const Agent &agent) { return agent.connected && agent.alive; });
    return available == 1 && object.puzzle.soloAllowed ? 1u : std::max(2u, object.puzzle.requiredAgents);
  }
  inline unsigned int AvailableButton(const WorldState &world, const Agent &agent, const Interactable &object) {
    unsigned int occupied = 0;
    for (const auto &other : world.agents) if (other.id != agent.id && other.puzzle.target == object.id)
      occupied |= ButtonBit(other.puzzle.coordinationButton);
    unsigned int selected = 0; float distance = std::numeric_limits<float>::max();
    for (unsigned int button = 1; button <= ButtonCount(object); ++button) {
      const float candidate = (ButtonPosition(object, button) - agent.position).lengthSquared();
      if (!(occupied & ButtonBit(button)) && candidate < distance) { selected = button; distance = candidate; }
    }
    return selected;
  }
  inline void Refresh(WorldState &world, Interactable &object) {
    auto &puzzle = object.puzzle; puzzle.coordinationMask = puzzle.coordinationReservedMask = 0;
    if (puzzle.state == PuzzleState::Solved) {
      puzzle.coordinationMask = puzzle.coordinationReservedMask = static_cast<std::uint8_t>(FullMask(object)); return;
    }
    for (const auto &agent : world.agents) if (agent.connected && agent.alive && !agent.incapacitated && agent.puzzle.target == object.id) {
      const auto bit = ButtonBit(agent.puzzle.coordinationButton);
      puzzle.coordinationReservedMask |= static_cast<std::uint8_t>(bit);
      if (agent.puzzle.holding) puzzle.coordinationMask |= static_cast<std::uint8_t>(bit);
    }
    // El relé auxiliar energiza los controles restantes sólo mientras el único operador mantiene el suyo.
    if (RequiredAgents(world, object) == 1 && puzzle.coordinationMask)
      puzzle.coordinationMask = puzzle.coordinationReservedMask = static_cast<std::uint8_t>(FullMask(object));
  }
}
