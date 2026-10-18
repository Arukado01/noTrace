#pragma once

#include "Config/NoTraceCombatConfig.h"
#include "Types/NoTraceGameplayTypes.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace NoTrace::CombatGeometry {
  struct AimSolution { sf::Vector2f ground; float slope = 0.f; };
  struct HitVolume {
    sf::Vector2f center;
    sf::Vector2f forward;
    sf::Vector2f halfSize;
    float bottom = 0.f;
    float top = 0.f;
    HitZone zone = HitZone::None;
  };
  struct HitIntersection { float distance = 0.f; float height = 0.f; HitZone zone = HitZone::None; };

  inline sf::Vector2f Unit(sf::Vector2f direction) {
    const float length = std::hypot(direction.x, direction.y);
    return length > 0.0001f ? direction / length : sf::Vector2f{1.f, 0.f};
  }

  inline float WeaponHeight(Stance stance) {
    return stance == Stance::Prone ? CombatConfig::ProneWeaponHeight : stance == Stance::Crouched ? CombatConfig::CrouchedWeaponHeight : CombatConfig::StandingWeaponHeight;
  }

  inline float HeadCenterHeight(Stance stance) {
    return stance == Stance::Prone ? (CombatConfig::ProneHeadBottom + CombatConfig::ProneHeight) * 0.5f :
        (stance == Stance::Crouched ? CombatConfig::CrouchedHeight : CombatConfig::StandingHeight) - CombatConfig::HeadHeight * 0.5f;
  }

  inline float TorsoCenterHeight(Stance stance) {
    return stance == Stance::Prone ? (CombatConfig::ProneTorsoBottom + CombatConfig::ProneTorsoTop) * 0.5f :
        stance == Stance::Crouched ? (CombatConfig::CrouchedTorsoBottom + CombatConfig::CrouchedHeight - CombatConfig::HeadHeight) * 0.5f :
        (CombatConfig::StandingTorsoBottom + CombatConfig::StandingHeight - CombatConfig::HeadHeight) * 0.5f;
  }

  inline std::vector<HitVolume> ActorVolumes(sf::Vector2f position, sf::Vector2f facing, Stance stance, bool down = false, float elevation = 0.f) {
    using namespace CombatConfig;
    if (down) stance = Stance::Prone;
    const auto forward = Unit(facing);
    const sf::Vector2f side{-forward.y, forward.x};
    const auto volume = [&](sf::Vector2f center, sf::Vector2f halfSize, float bottom, float top, HitZone zone) {
      return HitVolume{center, forward, halfSize, bottom + elevation, top + elevation, zone};
    };
    if (stance == Stance::Prone) {
      return {
        volume(position + forward * ProneHeadOffset, {HeadHalfLength, HeadHalfWidth}, ProneHeadBottom, ProneHeight, HitZone::Head),
        volume(position, {ProneTorsoHalfLength, ProneTorsoHalfWidth}, ProneTorsoBottom, ProneTorsoTop, HitZone::Torso),
        volume(position + forward * ProneArmForwardOffset + side * ProneArmSideOffset, {ProneArmHalfLength, ProneArmHalfWidth}, ProneArmBottom, ProneArmTop, HitZone::Limbs),
        volume(position + forward * ProneArmForwardOffset - side * ProneArmSideOffset, {ProneArmHalfLength, ProneArmHalfWidth}, ProneArmBottom, ProneArmTop, HitZone::Limbs),
        volume(position - forward * ProneLegOffset + side * LegSideOffset, {ProneLegHalfLength, LegHalfWidth}, 0.f, ProneLegTop, HitZone::Limbs),
        volume(position - forward * ProneLegOffset - side * LegSideOffset, {ProneLegHalfLength, LegHalfWidth}, 0.f, ProneLegTop, HitZone::Limbs)
      };
    }
    const float height = stance == Stance::Crouched ? CrouchedHeight : StandingHeight;
    const float torsoBottom = stance == Stance::Crouched ? CrouchedTorsoBottom : StandingTorsoBottom;
    const float torsoTop = height - HeadHeight;
    return {
      volume(position + forward * HeadForwardOffset, {HeadHalfLength, HeadHalfWidth}, torsoTop, height, HitZone::Head),
      volume(position, {TorsoHalfLength, TorsoHalfWidth}, torsoBottom, torsoTop, HitZone::Torso),
      volume(position + side * ArmSideOffset, {ArmHalfLength, ArmHalfWidth}, torsoBottom + ArmBottomMargin, torsoTop - ArmTopMargin, HitZone::Limbs),
      volume(position - side * ArmSideOffset, {ArmHalfLength, ArmHalfWidth}, torsoBottom + ArmBottomMargin, torsoTop - ArmTopMargin, HitZone::Limbs),
      volume(position + side * LegSideOffset, {LegHalfLength, LegHalfWidth}, 0.f, torsoBottom, HitZone::Limbs),
      volume(position - side * LegSideOffset, {LegHalfLength, LegHalfWidth}, 0.f, torsoBottom, HitZone::Limbs)
    };
  }

  inline std::optional<float> IntersectVolume(sf::Vector2f from, sf::Vector2f direction, float fromHeight, float slope, float range, const HitVolume &volume) {
    float near = 0.f, far = range;
    const auto axis = [&](float origin, float delta, float minimum, float maximum) {
      if (std::abs(delta) < 0.0001f) return origin >= minimum && origin <= maximum;
      float first = (minimum - origin) / delta, second = (maximum - origin) / delta;
      if (first > second) std::swap(first, second);
      near = std::max(near, first); far = std::min(far, second);
      return near <= far;
    };
    const auto offset = from - volume.center;
    const sf::Vector2f side{-volume.forward.y, volume.forward.x};
    if (!axis(offset.dot(volume.forward), direction.dot(volume.forward), -volume.halfSize.x, volume.halfSize.x) ||
        !axis(offset.dot(side), direction.dot(side), -volume.halfSize.y, volume.halfSize.y) ||
        !axis(fromHeight, slope, volume.bottom, volume.top)) return std::nullopt;
    return near <= range && far >= 0.f ? std::optional<float>{near} : std::nullopt;
  }

  inline std::optional<HitIntersection> IntersectActor(sf::Vector2f from, sf::Vector2f direction, float fromHeight, float slope, float range,
      sf::Vector2f position, sf::Vector2f facing, Stance stance, bool down = false, float elevation = 0.f) {
    direction = Unit(direction);
    std::optional<HitIntersection> closest;
    for (const auto &volume : ActorVolumes(position, facing, stance, down, elevation)) {
      const auto distance = IntersectVolume(from, direction, fromHeight, slope, range, volume);
      if (distance && (!closest || *distance < closest->distance)) closest = HitIntersection{*distance, fromHeight + slope * *distance, volume.zone};
    }
    return closest;
  }

  inline float LethalDamage(HitZone zone) {
    return zone == HitZone::Head ? CombatConfig::LethalHeadDamage : zone == HitZone::Torso ? CombatConfig::LethalTorsoDamage : zone == HitZone::Limbs ? CombatConfig::LethalLimbDamage : 0.f;
  }

  inline float StunDamage(HitZone zone) {
    return zone == HitZone::Head ? CombatConfig::NonLethalHeadStun : zone == HitZone::Torso ? CombatConfig::NonLethalTorsoStun : zone == HitZone::Limbs ? CombatConfig::NonLethalLimbStun : 0.f;
  }
}
