import argparse
import hashlib
import io
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from GenerateNoTraceCharactersGuides import add, elbow, length, scale, unit
from GenerateNoTraceCharactersRig import ANCHOR, CELL, DIRECTIONS, bone, project, read_parts, render, skeleton


CLIPS = [
    ("Idle", 0, 0, 3, True), ("Walk", 0, 8, 10, True), ("Run", 0, 16, 16, True),
    ("CrouchIdle", 0, 24, 3, True), ("CrouchWalk", 0, 32, 9, True), ("CrouchRun", 0, 40, 14, True),
    ("LieDown", 1, 0, 24, False), ("ProneIdle", 1, 8, 3, True), ("Crawl", 1, 16, 8, True), ("StandUp", 1, 24, 24, False),
    ("Dive", 1, 32, 16, False), ("Roll", 1, 40, 16, False),
    ("Aim", 3, 0, 4, True), ("Shoot", 3, 8, 32, False), ("Reload", 3, 16, 10, False),
    ("Punch", 2, 0, 24, False), ("Kick", 2, 8, 20, False), ("Grab", 2, 16, 6, True), ("Choke", 2, 24, 12, True),
    ("Push", 7, 0, 24, False), ("Throw", 3, 24, 24, False), ("WallBang", 2, 32, 24, False), ("Damage", 2, 40, 16, False),
    ("Fall", 4, 0, 20, False), ("Unconscious", 4, 8, 2, True), ("Interact", 4, 16, 8, True),
    ("HideEnter", 4, 24, 20, False), ("HideExit", 4, 32, 20, False),
    ("CoverEnter", 4, 40, 20, False), ("CoverIdle", 4, 48, 3, True), ("CoverMove", 4, 56, 8, True),
    ("CrouchEnter", 1, 48, 16, False), ("CrouchExit", 1, 56, 16, False),
    ("DragStart", 8, 0, 16, False), ("DragLoop", 8, 8, 8, True), ("DragStop", 8, 16, 16, False),
    ("Death", 9, 0, 16, False), ("GripVictimEnter", 10, 0, 16, False), ("GripVictimHold", 10, 8, 4, True), ("GripVictimRelease", 10, 16, 16, False),
    ("SuppressorAttach", 11, 0, 12, False), ("SuppressorDetach", 11, 8, 12, False),
    ("WeaponHolster", 12, 0, 16, False), ("WeaponDraw", 12, 8, 16, False),
    ("TraversalAttach", 13, 0, 16, False), ("TraversalMove", 13, 8, 8, True), ("TraversalRelease", 13, 16, 16, False),
    ("TraversalHang", 13, 24, 4, True), ("TraversalShimmy", 13, 32, 8, True), ("TraversalClimb", 13, 40, 8, True),
    ("TraversalTransfer", 13, 48, 12, False), ("TraversalDrop", 13, 56, 16, False),
    ("ReviveRise", 14, 0, 28, False),
]
WEAPON_CLIPS = ["Aim", "Shoot", "Reload", "Throw"]


def save_bytes(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    temporary = path.with_name(path.name + ".writing")
    temporary.write_bytes(data)
    temporary.replace(path)


def save_text(path, text):
    save_bytes(path, text.encode("utf-8"))


def save_png(image, path, **options):
    data = io.BytesIO()
    image.save(data, format="PNG", **options)
    save_bytes(path, data.getvalue())


def write_config(path):
    names = ", ".join(entry[0] for entry in CLIPS)
    entries = ",\n".join("    {" + str(family) + ", " + str(first) + ", 8, " + str(fps) + ".f, " + ("true" if loop else "false") + "}" for _, family, first, fps, loop in CLIPS)
    source = '''#pragma once

#include "Config/NoTraceCharacterArtConfig.h"
#include <array>
#include <cstddef>

namespace NoTrace::SpriteConfig {
  inline constexpr const char *Atlas = "NoTrace/Characters/OperativeAtlas.png";
  inline constexpr const char *FrameTable = "Content/Textures/NoTrace/Characters/OperativeAtlas.frames";
  inline constexpr const char *MuzzleTable = "Content/Textures/NoTrace/Characters/OperativeAtlas.muzzles";
  inline constexpr const char *AtlasMetadata = CharacterArtConfig::AtlasMetadata;
  inline constexpr std::size_t CellSize = 192;
  inline constexpr std::size_t Columns = 64;
  inline constexpr std::size_t Directions = 8;
  inline constexpr std::size_t FamilyFrames = Columns * Directions;
  inline constexpr std::size_t FamilyCount = 15;
  inline constexpr std::size_t FrameCount = FamilyFrames * FamilyCount;
  inline constexpr std::size_t AuthoredFrameCount = 4544;
  inline constexpr std::size_t LegacyFrameCount = 896;
  inline constexpr float FeetX = 96.f;
  inline constexpr float FeetY = 144.f;
  inline constexpr float StandingPixels = 72.f;
  inline constexpr float DirectionHysteresisDegrees = 3.f;
  inline constexpr unsigned int SupportAlpha = 220;
  inline constexpr int SupportBandPixels = 6;
  inline constexpr float StandingTorsoPivotY = 109.f;
  inline constexpr float CrouchedTorsoPivotY = 124.f;
  inline constexpr float ProneTorsoPivotY = 133.f;
  enum class Clip { NAMES, Count };
  enum class Family { Locomotion, Posture, Melee, StandingWeapons, Context, CrouchedWeapons, ProneWeapons, Push, Drag, Death, GripVictim, Suppressor, WeaponSwitch, Traversal, Recovery };
  struct Animation { std::size_t family, first, count; float fps; bool loop = true, reverse = false; };
  // Los índices de familias se conservan; cada clip tiene ocho poses propias y las páginas se resuelven por metadatos.
  inline constexpr std::array<Animation, static_cast<std::size_t>(Clip::Count)> Animations{{
ENTRIES
  }};
}
'''.replace("NAMES", names).replace("ENTRIES", entries)
    save_text(path, source)


def mix(a, b, fraction):
    return tuple(first + (second - first) * fraction for first, second in zip(a, b))


def smooth(value):
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def keyed(keys, progress):
    for (first_time, first), (last_time, last) in zip(keys, keys[1:]):
        if progress <= last_time:
            return mix(first, last, smooth((progress - first_time) / (last_time - first_time)))
    return keys[-1][1]


def solve(points, prone=False):
    for side, sign in [("left", 1.0), ("right", -1.0)]:
        for proximal, distal, middle, size, bend in [("hip", "ankle", "knee", 17.0, (1.0, sign * 0.15, 0.0)),
                                                   ("shoulder", "wrist", "elbow", 13.0, (0.0, sign * 0.6, -1.0) if prone else (1.0, sign * 0.15, -0.15))]:
            first, last = points[side + "_" + proximal], points[side + "_" + distal]
            delta = add(last, scale(first, -1.0))
            if length(delta) > size * 1.999:
                last = add(first, scale(unit(delta), size * 1.999))
                points[side + "_" + distal] = last
                if distal == "ankle":
                    points[side + "_toe"] = add(last, (5.0, sign * 1.5, -2.0))
            points[side + "_" + middle] = elbow(first, last, bend, size)
    return points


def prone_pose(frame, crawl=False):
    phase = frame / 8.0 * math.tau
    breath = math.sin(phase) * 0.25
    points = {"pelvis": (-5.0, 0.0, 11.0), "neck": (18.0, 0.0, 13.0 + breath), "head": (25.0, 0.0, 16.0 + breath), "backpack": (6.0, 0.0, 18.0 + breath)}
    for side, sign in [("left", 1.0), ("right", -1.0)]:
        step = math.sin(phase + (0.0 if sign > 0 else math.pi)) if crawl else 0.0
        foot = (-36.0 + step * 4.0, sign * (7.0 + max(0.0, step) * 3.0), 2.0)
        points.update({side + "_hip": (-5.0, sign * 4.0, 10.0), side + "_ankle": foot, side + "_toe": add(foot, (-4.0, sign, -1.0)),
                       side + "_shoulder": (15.0, sign * 6.0, 12.0 + breath), side + "_wrist": (27.0 + step * 3.0, sign * 12.0, 3.0)})
    return solve(points, True)


def blend_poses(first, second, amount):
    return {name: mix(first[name], second[name], amount) for name in first}


def blend_toes(points, first, second, amount):
    # El pie gira alrededor del tobillo actual sin colapsar al mezclar puntas opuestas.
    for side, sign in [("left", 1.0), ("right", -1.0)]:
        ankle, toe = side + "_ankle", side + "_toe"
        a = add(first[toe], scale(first[ankle], -1.0))
        b = add(second[toe], scale(second[ankle], -1.0))
        if a == b or amount <= 0.0:
            offset = a
        elif amount >= 1.0:
            offset = b
        else:
            first_angle, second_angle = math.atan2(a[1], a[0]), math.atan2(b[1], b[0])
            turn = math.atan2(math.sin(second_angle - first_angle), math.cos(second_angle - first_angle))
            if abs(abs(turn) - math.pi) < 1e-8:
                turn = sign * math.pi
            angle = first_angle + turn * amount
            horizontal = math.hypot(a[0], a[1]) * (1.0 - amount) + math.hypot(b[0], b[1]) * amount
            offset = (math.cos(angle) * horizontal, math.sin(angle) * horizontal, a[2] + (b[2] - a[2]) * amount)
        points[toe] = add(points[ankle], offset)
    return points


def recovery_pose(frame, stance):
    fallen = body_pose("Unconscious", 0)
    destination = prone_pose(0) if stance == "Prone" else skeleton("CrouchIdle" if stance == "Crouched" else "Idle", 0)
    if frame == 0:
        return fallen
    if frame == 7:
        return destination
    t = frame / 7.0
    if stance == "Prone":
        yaw = math.pi * (1.0 - smooth(t))
        axis = (math.cos(yaw), math.sin(yaw), 0.0)
        lateral = (-axis[1], axis[0], 0.0)
        pelvis = mix(fallen["pelvis"], destination["pelvis"], smooth(t))
        neck = add(pelvis, add(scale(axis, 23.0), (0.0, 0.0, 2.0)))
        points = {"pelvis": pelvis, "neck": neck, "head": add(neck, add(scale(axis, 7.0), (0.0, 0.0, 3.0))),
                  "backpack": add(pelvis, add(scale(axis, 11.0), (0.0, 0.0, 7.0)))}
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            stepping = max(0.0, math.sin(t * math.tau + (0.0 if sign > 0 else math.pi)))
            points[side + "_hip"] = add(pelvis, add(scale(lateral, sign * 4.0), (0.0, 0.0, -1.0)))
            foot_axis = (math.cos(yaw + sign * 0.18 * math.sin(t * math.pi)), math.sin(yaw + sign * 0.18 * math.sin(t * math.pi)), 0.0)
            points[side + "_ankle"] = add(pelvis, add(scale(foot_axis, -31.0 + stepping * 6.0), add(scale(lateral, sign * 8.0), (0.0, 0.0, 2.0 - pelvis[2] + stepping * 2.0))))
            points[side + "_toe"] = add(points[side + "_ankle"], add(scale(foot_axis, -4.0), (0.0, 0.0, -1.0)))
            points[side + "_shoulder"] = add(pelvis, add(scale(axis, 20.0), add(scale(lateral, sign * 6.0), (0.0, 0.0, 1.0))))
            points[side + "_wrist"] = add(pelvis, add(scale(axis, 28.0 - stepping * 5.0), add(scale(lateral, sign * (12.0 + stepping * 3.0)), (0.0, 0.0, 2.0 - pelvis[2] + stepping * 2.0))))
    else:
        crouched = stance == "Crouched"
        pelvis_keys = [(0, fallen["pelvis"]), (1/7, (7, 0, 9)), (2/7, (8, 0, 12)), (3/7, (8, 0, 19)),
                       (4/7, (6, 0, 19 if crouched else 24)), (5/7, (3, 0, 20 if crouched else 29)), (6/7, (1, 0, 20 if crouched else 33)), (1, destination["pelvis"])]
        neck_keys = [(0, fallen["neck"]), (1/7, (-15, 0, 17)), (2/7, (-10, 0, 25)), (3/7, (-4, 0, 35)),
                     (4/7, (1 if crouched else 0, 0, 39 if crouched else 43)), (5/7, (1 if crouched else 0, 0, 40.5 if crouched else 49)),
                     (6/7, (2 if crouched else .3, 0, 41 if crouched else 53)), (1, destination["neck"])]
        pelvis, neck = keyed(pelvis_keys, t), keyed(neck_keys, t)
        axis = unit(add(neck, scale(pelvis, -1.0)))
        points = {"pelvis": pelvis, "neck": neck, "head": add(neck, scale(axis, 7.0)), "backpack": add(neck, add(scale(axis, -9.0), (-3.0, 0.0, 0.0)))}
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            ankle = keyed([(0, fallen[side + "_ankle"]), (1/7, (33, sign * 8, 2)), (2/7, (29 if sign > 0 else 27, sign * 8, 2)),
                           (3/7, (18 if sign > 0 else 16, sign * 7, 2)), (4/7, (9 if sign > 0 else -3, sign * 6, 2)),
                           (5/7, (2 if sign > 0 else -3, sign * 5.5, 2)), (6/7, (0, sign * 5.5, 2)), (1, destination[side + "_ankle"])], t)
            wrist = keyed([(0, fallen[side + "_wrist"]), (1/7, (-18 if sign > 0 else -8, sign * 15, 2)), (2/7, (-16 if sign > 0 else -8, sign * 12, 2)),
                           (3/7, (-10 if sign > 0 else -3, sign * 12, 7 if sign > 0 else 8)),
                           (4/7, (-5 if sign > 0 else 3, sign * 10, (16 if crouched else 18) + (0 if sign > 0 else 2))),
                           (5/7, (-3 if sign > 0 else 1, sign * 8, 19 if crouched else 24)),
                           (6/7, (-1 if crouched else -2, sign * 7, 16 if crouched else 26)), (1, destination[side + "_wrist"])], t)
            points.update({side + "_hip": add(pelvis, (0, sign * 4, 0)), side + "_ankle": ankle,
                           side + "_toe": add(ankle, (5, sign * 1.5, -2)),
                           side + "_shoulder": add(neck, add(scale(axis, -6), (-3, sign * 6.5, 0))), side + "_wrist": wrist})
    # Las manos apoyadas resuelven los codos hacia fuera y no bajo el suelo.
    points = solve(points)
    for side, sign in [("left", 1.0), ("right", -1.0)]:
        points[side + "_elbow"] = elbow(points[side + "_shoulder"], points[side + "_wrist"], (0.0, sign, 0.15), 13.0)
    return points


def body_pose(clip, frame, stance="Standing"):
    t = frame / 7.0
    phase = frame / 8.0 * math.tau
    if clip == "ReviveRise":
        return recovery_pose(frame, stance)
    if clip in ["Idle", "Walk", "Run", "CrouchIdle", "CrouchWalk"]:
        return skeleton(clip, frame)
    if clip == "CrouchRun":
        points = skeleton("CrouchWalk", frame)
        for side in ["left", "right"]:
            foot = points[side + "_ankle"]
            points[side + "_ankle"] = (foot[0] * 1.4, foot[1], foot[2] + 1.5)
        return solve(points)
    if clip in ["ProneIdle", "Crawl"]:
        return prone_pose(frame, clip == "Crawl")
    standing, crouched, prone = skeleton("Idle", frame), skeleton("CrouchIdle", frame), prone_pose(frame)
    if clip in ["CrouchEnter", "CrouchExit"]:
        amount = smooth(t) if clip == "CrouchEnter" else smooth(1.0 - t)
        points = blend_poses(standing, crouched, amount)
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            points[side + "_wrist"] = add(points[side + "_wrist"], (4.0 * math.sin(t * math.pi), 0.0, 2.0 * math.sin(t * math.pi)))
        return solve(points)
    if clip in ["LieDown", "StandUp"]:
        if clip == "LieDown":
            first, second, amount = (standing, crouched, smooth(t / 0.4)) if t < 0.4 else (crouched, prone, smooth((t - 0.4) / 0.6))
            points = blend_poses(first, second, amount)
            for side, sign in [("left", 1.0), ("right", -1.0)]:
                points[side + "_wrist"] = mix(points[side + "_wrist"], (20.0, sign * 13.0, 3.0), math.sin(t * math.pi) * 0.8)
        else:
            kneel = blend_poses(crouched, prone, 0.25)
            kneel["left_ankle"] = (12.0, 6.0, 2.0)
            kneel["right_ankle"] = (-17.0, -6.0, 2.0)
            blend_toes(kneel, crouched, prone, 0.25)
            kneel["right_wrist"] = (22.0, -12.0, 2.0)
            first, second, amount = (prone, kneel, smooth(t / 0.55)) if t < 0.55 else (kneel, standing, smooth((t - 0.55) / 0.45))
            points = blend_poses(first, second, amount)
        return blend_toes(solve(points, t > 0.7 if clip == "LieDown" else t < 0.3), first, second, amount)
    if clip == "Dive":
        amount = smooth(t / 0.28) if t < 0.28 else 1.0
        if t < 0.28:
            points = blend_poses(standing, prone, amount)
        else:
            points = prone.copy()
        airborne = math.sin(max(0.0, min(1.0, (t - 0.15) / 0.7)) * math.pi) * 15.0
        points = {name: add(point, (0.0, 0.0, airborne)) for name, point in points.items()}
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            points[side + "_wrist"] = mix(points[side + "_wrist"], (32.0, sign * 11.0, 3.0 + airborne), math.sin(t * math.pi))
        points = solve(points, True)
        return blend_toes(points, standing, prone, amount) if t < 0.28 else points
    if clip == "Roll":
        points = crouched.copy()
        tuck = math.sin(t * math.pi)
        points["pelvis"] = (0.0, 0.0, 20.0 - tuck * 7.0)
        points["neck"] = (4.0, 0.0, 40.0 - tuck * 13.0)
        points["head"] = (8.0, 0.0, 46.0 - tuck * 15.0)
        points["backpack"] = (-3.0, 0.0, 31.0 - tuck * 10.0)
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            points[side + "_hip"] = add(points["pelvis"], (0.0, sign * 4.0, 0.0))
            points[side + "_ankle"] = (8.0 * tuck, sign * 5.5, 2.0 + tuck * 8.0)
            points[side + "_toe"] = add(points[side + "_ankle"], (4.0, sign, -1.0))
            points[side + "_shoulder"] = add(points["neck"], (-2.0, sign * 6.0, -5.0))
            points[side + "_wrist"] = add(points["head"], (4.0, sign * 7.0, -7.0))
        points = solve(points)
        angle = t * math.tau
        pivot = (0.0, 0.0, 22.0)
        rotated = {}
        for name, point in points.items():
            forward, side, height = add(point, scale(pivot, -1.0))
            rotated[name] = (forward * math.cos(angle) + height * math.sin(angle), side, 22.0 - forward * math.sin(angle) + height * math.cos(angle))
        minimum = min(point[2] for point in rotated.values())
        return {name: add(point, (0.0, 0.0, max(0.0, 1.0 - minimum))) for name, point in rotated.items()}
    if clip in ["Fall", "Unconscious", "Death"]:
        amount = smooth(t) if clip == "Fall" else 1.0
        fallen = prone.copy()
        for name, point in fallen.items():
            fallen[name] = (-point[0], point[1] * 1.15, max(2.0, point[2] - 3.0))
        fallen["left_wrist"] = (-18.0, 21.0, 2.0)
        fallen["right_wrist"] = (-7.0, -20.0, 2.0)
        if clip == "Death":
            fallen["head"] = (-24.0, 3.0 + t * 2.0, 12.0)
            fallen["right_ankle"] = (30.0, -14.0, 2.0)
            amount = smooth(t)
        points = blend_poses(standing, fallen, amount)
        return solve(points, amount > 0.7)
    if clip in WEAPON_CLIPS or clip.startswith("Suppressor") or clip.startswith("Weapon"):
        points = prone if stance == "Prone" else crouched if stance == "Crouched" else standing
        height = {"Standing": 22.0, "Crouched": 10.0, "Prone": 6.0}[stance] * 72.0 / 46.0
        forward = 30.0 if stance == "Prone" else 14.0
        recoil = [0.0, 0.8, 2.8, 3.2, 1.8, 0.8, 0.2, 0.0][frame] if clip == "Shoot" else 0.0
        grip = (forward - recoil, -1.5, height + recoil * 0.25)
        points["right_wrist"] = grip
        points["left_wrist"] = add(grip, (1.0, 3.0, -0.7))
        if clip == "Reload":
            points["right_wrist"] = mix(grip, (8.0, -3.0, height + 5.0), math.sin(t * math.pi))
            points["left_wrist"] = keyed([(0.0, add(grip, (0.0, 2.0, -1.0))), (0.27, (0.0, 7.0, 24.0 if stance == "Standing" else height)),
                                           (0.58, add(grip, (-1.0, 2.0, -5.0))), (0.82, add(grip, (2.0, 3.0, 3.0))), (1.0, add(grip, (1.0, 3.0, -0.7)))], t)
        if clip == "Throw":
            points["right_wrist"] = keyed([(0.0, (6.0, -9.0, height)), (0.28, (-6.0, -10.0, height + 17.0)),
                                            (0.58, (17.0, -4.0, height + 17.0)), (0.74, (28.0, -4.0, height + 4.0)), (1.0, (8.0, -8.0, height))], t)
            points["left_wrist"] = add(points["left_shoulder"], (5.0, 2.0, -18.0))
        if clip.startswith("Suppressor"):
            points["right_wrist"] = (11.0, -2.0, height + 3.0)
            points["left_wrist"] = (21.0 - t * 2.0 if clip.endswith("Attach") else 19.0 + t * 3.0, math.sin(t * math.tau * 2.0) * 2.0, height + 3.0)
        if clip.startswith("Weapon"):
            holster = (0.0, -7.0, 25.0)
            points["right_wrist"] = mix(grip, holster, smooth(t) if clip == "WeaponHolster" else smooth(1.0 - t))
            points["left_wrist"] = mix(add(grip, (1.0, 3.0, 0.0)), (-1.0, 8.0, 30.0), math.sin(t * math.pi))
        points["weapon_grip"] = points["right_wrist"]
        points["weapon_muzzle"] = add(points["weapon_grip"], (11.0, 0.0, 0.0))
        return solve(points, stance == "Prone")
    points = standing.copy()
    if clip in ["CoverIdle", "CoverEnter", "CoverMove", "HideEnter", "HideExit"]:
        moving = clip == "CoverMove"
        target = skeleton("Walk" if moving else "Idle", frame) if clip.startswith("Cover") else skeleton("CrouchIdle", frame)
        if clip.startswith("Cover"):
            for name in ["neck", "head", "backpack", "left_shoulder", "right_shoulder"]:
                target[name] = add(target[name], (-3.0, 0.0, 0.0))
        amount = smooth(t) if clip in ["CoverEnter", "HideEnter"] else smooth(1.0 - t) if clip == "HideExit" else 1.0
        points = blend_poses(standing, target, amount)
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            points[side + "_wrist"] = add(points[side + "_shoulder"], (8.0, sign * 2.0, -12.0))
        return solve(points)
    if clip in ["Punch", "WallBang"]:
        points["right_wrist"] = keyed([(0.0, (6.0, -8.0, 43.0)), (0.22, (-6.0, -9.0, 47.0)),
                                        (0.43, (26.0, -5.0, 46.0)), (0.65, (24.0, -4.0, 45.0)), (1.0, (6.0, -8.0, 43.0))], t)
        points["left_wrist"] = (7.0, 7.0, 43.0)
        lean = math.sin(t * math.pi) * (3.0 if clip == "Punch" else 5.0)
        for name in ["neck", "head", "backpack", "left_shoulder", "right_shoulder"]:
            points[name] = add(points[name], (lean, 0.0, 0.0))
    elif clip == "Kick":
        points["right_ankle"] = keyed([(0.0, (0.0, -6.0, 2.0)), (0.25, (9.0, -5.0, 18.0)),
                                        (0.5, (30.0, -4.0, 27.0)), (0.67, (25.0, -4.0, 20.0)), (1.0, (0.0, -6.0, 2.0))], t)
        points["right_toe"] = add(points["right_ankle"], (5.0, 0.0, -1.0))
        points["left_wrist"], points["right_wrist"] = (7.0, 8.0, 44.0), (3.0, -10.0, 44.0)
    elif clip in ["Grab", "Choke", "Push"]:
        if clip == "Push":
            extension = math.sin(t * math.pi)
            points["left_ankle"], points["right_ankle"] = (-7.0, 7.0, 2.0), (10.0, -6.0, 2.0)
            for side, sign in [("left", 1.0), ("right", -1.0)]:
                points[side + "_wrist"] = (10.0 + extension * 15.0, sign * 7.0, 42.0)
        else:
            angle = math.radians(25.0)
            points["pelvis"] = (6.0, 0.0, 31.5)
            def torso_point(forward, side, height):
                return add(points["pelvis"], (forward * math.cos(angle) + height * math.sin(angle), side,
                                               -forward * math.sin(angle) + height * math.cos(angle)))
            points["neck"] = torso_point(0.0, 0.0, 20.0)
            points["head"] = torso_point(0.0, 0.0, 27.0)
            points["backpack"] = torso_point(-3.0, 0.0, 11.0)
            squeeze = [0.0, 0.5, 1.1, 1.5, 2.1, 1.8, 1.0, 0.35][frame] if clip == "Choke" else math.sin(phase) * 0.35
            for side, sign in [("left", 1.0), ("right", -1.0)]:
                points[side + "_hip"] = add(points["pelvis"], (0.0, sign * 4.0, 0.0))
                points[side + "_shoulder"] = torso_point(-3.0, sign * 6.5, 14.0)
                points[side + "_wrist"] = (33.5 if side == "right" else 33.0, sign * (4.5 - squeeze), 52.0 if side == "right" else 51.5)
    elif clip == "Damage":
        recoil = math.sin(t * math.pi)
        for name in ["neck", "head", "backpack", "left_shoulder", "right_shoulder"]:
            points[name] = add(points[name], (-7.0 * recoil, 0.0, -3.0 * recoil))
    elif clip == "Interact":
        points = blend_poses(standing, crouched, 0.35)
        points["right_wrist"] = (19.0, -4.0, 34.0 + math.sin(phase) * 3.0)
        points["left_wrist"] = (17.0, 6.0, 34.0 - math.sin(phase) * 2.0)
    elif clip.startswith("Drag"):
        points = skeleton("CrouchWalk" if clip == "DragLoop" else "CrouchIdle", frame)
        amount = smooth(t) if clip == "DragStart" else smooth(1.0 - t) if clip == "DragStop" else 1.0
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            points[side + "_wrist"] = mix(points[side + "_wrist"], (19.0, sign * 6.0, 11.0), amount)
    elif clip.startswith("GripVictim"):
        amount = smooth(t) if clip.endswith("Enter") else smooth(1.0 - t) if clip.endswith("Release") else 1.0
        for name in ["neck", "head", "backpack", "left_shoulder", "right_shoulder"]:
            points[name] = add(points[name], (-4.0 * amount, 0.0, -3.0 * amount))
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            points[side + "_wrist"] = mix(points[side + "_wrist"], (-4.0, sign * 6.0, 43.0 + math.sin(phase) * 0.25), amount)
    elif clip.startswith("Traversal"):
        amount = smooth(t) if clip.endswith("Attach") else smooth(1.0 - t) if clip.endswith("Release") or clip.endswith("Drop") else 1.0
        for side, sign in [("left", 1.0), ("right", -1.0)]:
            offset = math.sin(phase + (0.0 if sign > 0 else math.pi)) if clip in ["TraversalClimb", "TraversalMove", "TraversalShimmy"] else 0.0
            hand = (7.0 + offset * 3.0, sign * (8.0 + (offset * 5.0 if clip == "TraversalShimmy" else 0.0)), 70.0 + offset * 5.0)
            if clip == "TraversalTransfer" and side == "right":
                hand = (8.0 + t * 12.0, -9.0 - t * 6.0, 70.0 + math.sin(t * math.pi) * 5.0)
            points[side + "_wrist"] = mix(points[side + "_wrist"], hand, amount)
            points[side + "_ankle"] = mix(points[side + "_ankle"], (8.0 + offset * 4.0, sign * 5.0, 7.0 + max(0.0, offset) * 9.0), amount)
            points[side + "_toe"] = add(points[side + "_ankle"], (4.0, 0.0, -2.0))
    return solve(points)


def weapon_image(root, direction, points, weapon="Pistol", suppressed=False):
    registration = json.loads((root / "Manifests/weapons.registration.json").read_text(encoding="utf-8"))
    entry = next(item for item in registration["weapons"] if item["weapon"] == weapon and item["direction"] == direction and item["suppressed"] == suppressed)
    source = Image.open(root / "Pieces" / entry["file"]).convert("RGBA")
    factor = 64.0 / max(source.size)
    source = source.resize((max(1, round(source.width * factor)), max(1, round(source.height * factor))), Image.Resampling.BOX)
    image = Image.new("RGBA", (CELL, CELL))
    first = project(points["weapon_grip"], direction)
    last = project(add(points["weapon_grip"], (17.0 if weapon == "GuardRifle" else 14.0 if suppressed else 10.0, 0.0, 0.0)), direction)
    bone(image, source, first, last, 10.0 if weapon == "GuardRifle" else 8.0, entry["grip"], entry["muzzle"])
    return image, first, last


def generate(root, content=None):
    output = root / "Atlas"
    output.mkdir(parents=True, exist_ok=True)
    pages, metadata, review_sequences = [], [], []
    clip_variants = [(name, family, first, "Standing") for name, family, first, _, _ in CLIPS if family < 14]
    clip_variants += [(name, family, WEAPON_CLIPS.index(name) * 8, stance) for family, stance in [(5, "Crouched"), (6, "Prone")] for name in WEAPON_CLIPS]
    clip_variants += [(name, family, first + offset, stance) for name, family, first, _, _ in CLIPS if family in [11, 12] for stance, offset in [("Crouched", 16), ("Prone", 32)]]
    clip_variants += [("ReviveRise", 14, offset, stance) for stance, offset in [("Standing", 0), ("Crouched", 8), ("Prone", 16)]]
    parts_by_direction = {direction: read_parts(root, direction)[0] for direction in DIRECTIONS}
    for name, family, first, stance in clip_variants:
        page_number = len(pages)
        filename = name + ("-" + stance if stance != "Standing" else "")
        full = Image.new("RGBA", (CELL * 8, CELL * 8))
        lower = Image.new("RGBA", full.size)
        upper = Image.new("RGBA", full.size)
        cqc_layers = {layer: Image.new("RGBA", full.size) for layer in ["cqcBody", "cqcArms"]} if name in ["Grab", "Choke"] else {}
        sequence = []
        for direction_index, direction in enumerate(DIRECTIONS):
            for frame in range(8):
                points = body_pose(name, frame, stance)
                rendered = render(parts_by_direction[direction], points, direction)
                rendered_lower = render(parts_by_direction[direction], points, direction, "lower")
                rendered_upper = render(parts_by_direction[direction], points, direction, "upper")
                muzzle, grip = [-1.0, -1.0], [-1.0, -1.0]
                if "weapon_grip" in points and name != "Throw":
                    prop, grip, muzzle = weapon_image(root, direction, points)
                rect = [frame * CELL, direction_index * CELL, CELL, CELL]
                full.alpha_composite(rendered, rect[:2])
                lower.alpha_composite(rendered_lower, rect[:2])
                upper.alpha_composite(rendered_upper, rect[:2])
                for layer, sheet in cqc_layers.items():
                    sheet.alpha_composite(render(parts_by_direction[direction], points, direction, layer), rect[:2])
                bounds = rendered.getchannel("A").point(lambda value: 255 if value >= 32 else 0).getbbox()
                if bounds and (bounds[0] <= 1 or bounds[1] <= 1 or bounds[2] >= CELL - 1 or bounds[3] >= CELL - 1):
                    raise ValueError("La pose invade borde: " + filename + "/" + direction + "/" + str(frame) + str(bounds))
                index = family * 512 + direction_index * 64 + first + frame
                metadata.append({"index": index, "clip": name, "stance": stance, "direction": direction, "phase": frame, "page": page_number, "rect": rect,
                                 "groundAnchor": ANCHOR, "pelvis": project(points["pelvis"], direction), "muzzle": muzzle, "grip": grip, "carryGrip": project(points["right_wrist"], direction),
                                 "supportWidth": 52.0 if stance == "Prone" or name in ["ProneIdle", "Crawl", "Unconscious"] else 24.0,
                                 "joints": points, "pixelSha256": hashlib.sha256(rendered.tobytes()).hexdigest()})
                if direction == "SE":
                    preview = rendered.copy()
                    if "weapon_grip" in points and name != "Throw":
                        preview.alpha_composite(prop)
                    sequence.append(preview)
        for layer, image in [("full", full), ("lower", lower), ("upper", upper), *cqc_layers.items()]:
            save_png(image, output / (filename + "-" + layer + ".png"))
        page = {"name": filename, "full": filename + "-full.png", "lower": filename + "-lower.png", "upper": filename + "-upper.png"}
        page.update({layer: filename + "-" + layer + ".png" for layer in cqc_layers})
        pages.append(page)
        review_sequences.append((filename, sequence))
        print(filename, flush=True)
    packed_pages = pack_layers(output, pages, metadata)
    weapons = pack_weapons(root, output)
    report = {"author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-18", "schemaVersion": 3,
              "cellSize": CELL, "standingPixels": 72, "groundAnchor": ANCHOR, "logicalColumns": 64, "directions": DIRECTIONS,
              "families": ["Locomotion", "Posture", "Melee", "StandingWeapons", "Context", "CrouchedWeapons", "ProneWeapons", "Push", "Drag", "Death", "GripVictim", "Suppressor", "WeaponSwitch", "Traversal", "Recovery"],
              "sourceRegistration": "../Manifests/rig-pieces.registration.json", "weaponRegistration": "../Manifests/weapons.registration.json",
              "clips": [{"name": name, "family": family, "first": first, "count": 8, "fps": fps, "loop": loop} for name, family, first, fps, loop in CLIPS],
              "pages": packed_pages, "authoringPages": pages, "weapons": weapons, "frames": metadata, "status": "generated_requires_runtime_and_visual_validation"}
    save_text(output / "CharacterAtlasMetadata.json", json.dumps(report, indent=2))
    runtime = {name: report[name] for name in ["author", "date", "schemaVersion", "cellSize", "standingPixels", "groundAnchor", "directions", "pages", "weapons"]}
    runtime["frames"] = [{key: entry[key] for key in ["index", "groundAnchor", "pelvis", "muzzle", "grip", "carryGrip", "supportWidth", "art"]} for entry in metadata]
    save_text(output / "CharacterAtlasRuntime.json", json.dumps(runtime, separators=(",", ":")))
    save_text(output / "CharacterAtlasMetadata.frames", "".join(" ".join(str(value) for value in [item["index"], item["page"], *item["rect"], *item["groundAnchor"], *item["pelvis"], *item["muzzle"], *item["grip"], item["supportWidth"]]) + "\n" for item in metadata))
    save_text(output / "CharacterAtlasMetadata.pages", "\n".join(" ".join([item["full"], item["lower"], item["upper"]]) for item in pages) + "\n")
    gallery = []
    columns = 8
    rows = math.ceil(len(review_sequences) / columns)
    for phase in range(8):
        view = Image.new("RGBA", (CELL * columns, (CELL + 18) * rows), (25, 33, 42, 255))
        drawing = ImageDraw.Draw(view)
        for index, (name, sequence) in enumerate(review_sequences):
            x, y = index % columns * CELL, index // columns * (CELL + 18)
            view.alpha_composite(sequence[phase], (x, y))
            drawing.text((x + 3, y + CELL), name, fill=(198, 218, 218))
        gallery.append(view)
    save_png(gallery[2], output / "CharacterActions-review.png")
    save_png(gallery[0], output / "CharacterActions-animated.png", save_all=True, append_images=gallery[1:], duration=125, loop=0)
    quality_galleries(root, output, parts_by_direction)
    if content:
        content.mkdir(parents=True, exist_ok=True)
        for path in list((output / "Packed").glob("*.png")) + [output / "WeaponProps.png", output / "WeaponSelection.png"]:
            save_bytes(content / path.name, path.read_bytes())
        for path in [output / "CharacterAtlasMetadata.json"]:
            save_bytes(content / path.name, path.read_bytes())
        save_bytes(content / "CharacterAtlasRuntime.json", (output / "CharacterAtlasRuntime.json").read_bytes())
    print(json.dumps({"clips": len(CLIPS), "pages": len(pages), "frames": len(metadata), "layers": 3, "pairedCqcLayers": 2, "content": str(content) if content else None}))


def pack_layers(output, pages, metadata):
    destination = output / "Packed"
    destination.mkdir(parents=True, exist_ok=True)
    items = []
    for page_index, page in enumerate(pages):
        page_frames = [entry for entry in metadata if entry["page"] == page_index]
        for layer in ["full", "lower", "upper", "cqcBody", "cqcArms"]:
            if layer not in page:
                continue
            source = Image.open(output / page[layer]).convert("RGBA")
            for entry in page_frames:
                x, y, width, height = entry["rect"]
                cell = source.crop((x, y, x + width, y + height))
                bounds = cell.getchannel("A").getbbox()
                if not bounds:
                    bounds = (0, 0, 1, 1)
                bounds = (max(0, bounds[0] - 2), max(0, bounds[1] - 2), min(CELL, bounds[2] + 2), min(CELL, bounds[3] + 2))
                items.append((entry, layer, cell.crop(bounds), bounds[:2]))
    items.sort(key=lambda item: (item[0]["index"] >= 14 * 512, -item[2].height, -item[2].width, item[0]["index"], item[1]))
    atlases, x, y, row_height = [Image.new("RGBA", (2048, 2048))], 2, 2, 0
    appended = False
    for entry, layer, image, offset in items:
        if entry["index"] >= 14 * 512 and not appended:
            atlases.append(Image.new("RGBA", (2048, 2048)))
            x, y, row_height, appended = 2, 2, 0, True
        if x + image.width + 2 > 2048:
            x, y, row_height = 2, y + row_height + 2, 0
        if y + image.height + 2 > 2048:
            atlases.append(Image.new("RGBA", (2048, 2048)))
            x, y, row_height = 2, 2, 0
        atlases[-1].alpha_composite(image, (x, y))
        entry.setdefault("art", {})[layer] = {"page": len(atlases) - 1, "rect": [x, y, image.width, image.height], "offset": list(offset)}
        x += image.width + 2
        row_height = max(row_height, image.height)
    records = []
    for index, atlas in enumerate(atlases):
        filename = "Characters-" + str(index).zfill(2) + ".png"
        save_png(atlas, destination / filename)
        records.append({"file": filename, "size": list(atlas.size), "sha256": hashlib.sha256((destination / filename).read_bytes()).hexdigest()})
    return records


def pack_weapons(root, output):
    records = []
    atlas = Image.new("RGBA", (192 * 8, 192 * 6))
    for weapon_index, weapon in enumerate(["Pistol", "NonLethal", "GuardRifle"]):
        for suppressed in [False, True]:
            row = weapon_index * 2 + int(suppressed)
            for direction_index, direction in enumerate(DIRECTIONS):
                points = {"weapon_grip": (0.0, 0.0, 48.0)}
                image, grip, muzzle = weapon_image(root, direction, points, weapon, suppressed)
                atlas.alpha_composite(image, (direction_index * CELL, row * CELL))
                records.append({"weapon": weapon, "suppressed": suppressed, "direction": direction, "rect": [direction_index * CELL, row * CELL, CELL, CELL],
                                "grip": grip, "muzzle": muzzle, "barrelLength": 17.0 if weapon == "GuardRifle" else 14.0 if suppressed else 10.0})
    save_png(atlas, output / "WeaponProps.png")
    selection = Image.new("RGBA", (320 * 3, 192))
    for index, weapon in enumerate(["Pistol", "NonLethal", "GuardRifle"]):
        prop = Image.open(root / "Pieces" / ("Weapon-" + weapon + "-E.png")).convert("RGBA")
        prop.thumbnail((300, 172), Image.Resampling.BOX)
        selection.alpha_composite(prop, (index * 320 + (320 - prop.width) // 2, (192 - prop.height) // 2))
    save_png(selection, output / "WeaponSelection.png")
    return {"file": "WeaponProps.png", "frames": records, "source": "../Manifests/weapons.registration.json"}


def quality_galleries(root, output, parts_by_direction):
    target = output / "QualityGate"
    target.mkdir(exist_ok=True)
    for clip, stance in [("Aim", "Prone"), ("Shoot", "Prone"), ("Walk", "Standing"), ("Roll", "Standing"), ("Dive", "Standing"),
                         ("Grab", "Standing"), ("Push", "Standing"), ("CoverMove", "Standing"), ("TraversalClimb", "Standing"),
                         ("ReviveRise", "Standing"), ("ReviveRise", "Crouched"), ("ReviveRise", "Prone")]:
        gallery = Image.new("RGBA", (CELL * 8, CELL * 8), (25, 33, 42, 255))
        animation = []
        for frame in range(8):
            view = Image.new("RGBA", (CELL * 4, CELL * 2), (25, 33, 42, 255))
            for direction_index, direction in enumerate(DIRECTIONS):
                points = body_pose(clip, frame, stance)
                image = render(parts_by_direction[direction], points, direction)
                if "weapon_grip" in points:
                    image.alpha_composite(weapon_image(root, direction, points, "Pistol", True)[0])
                gallery.alpha_composite(image, (frame * CELL, direction_index * CELL))
                view.alpha_composite(image, (direction_index % 4 * CELL, direction_index // 4 * CELL))
            animation.append(view.resize((view.width * 4, view.height * 4), Image.Resampling.NEAREST))
        name = clip + "-" + stance
        save_png(gallery, target / (name + "-8dir8phase.png"))
        save_png(gallery.resize((gallery.width * 4, gallery.height * 4), Image.Resampling.NEAREST), target / (name + "-8dir8phase-4x.png"))
        save_png(animation[0], target / (name + "-animated4x.png"), save_all=True, append_images=animation[1:], duration=125, loop=0)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Autoría IK de clips propios desde piezas originales; capas completas sin recortes de cuerpos")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    parser.add_argument("--content", type=Path)
    arguments = parser.parse_args()
    generate(arguments.root, arguments.content)
