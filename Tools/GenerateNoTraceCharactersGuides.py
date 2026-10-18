import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw


DIRECTIONS = ["S", "SE", "E", "NE", "N", "NW", "W", "SW"]
ANGLES = [math.pi / 4 - index * math.pi / 4 for index in range(8)]
PHASES = ["aim_settled", "inhale", "trigger_preload", "shot_recoil", "recoil_peak", "recover", "exhale", "aim_return"]
RECOIL = [0.0, 0.15, 0.4, 2.5, 3.5, 1.6, 0.5, 0.1]


def add(first, second):
    return tuple(a + b for a, b in zip(first, second))


def scale(vector, factor):
    return tuple(value * factor for value in vector)


def dot(first, second):
    return sum(a * b for a, b in zip(first, second))


def length(vector):
    return math.sqrt(dot(vector, vector))


def unit(vector):
    return scale(vector, 1.0 / length(vector))


def elbow(shoulder, wrist, bend, bone=14.0):
    delta = add(wrist, scale(shoulder, -1))
    distance = length(delta)
    if not 0 < distance < bone * 2:
        raise ValueError("La mano no está al alcance de sus dos huesos")
    axis = unit(delta)
    normal = unit(add(bend, scale(axis, -dot(bend, axis))))
    midpoint = add(shoulder, scale(delta, 0.5))
    return add(midpoint, scale(normal, math.sqrt(bone * bone - distance * distance * 0.25)))


def pose(frame):
    recoil = RECOIL[frame]
    breath = [0.0, 0.25, 0.35, 0.15, 0.0, -0.1, -0.2, -0.05][frame]
    joints = {"pelvis": (22.0, 0.0, 6.0), "chest": (38.0, 0.0, 9.0 + breath), "neck": (49.0, 0.0, 12.0 + breath),
              "head": (55.0, 0.0, 13.0 + breath), "muzzle": (73.0 - recoil, -1.0, 11.0 + recoil * 0.12)}
    for side, sign in [("left", 1.0), ("right", -1.0)]:
        shoulder = (44.0, sign * 7.0, 10.0 + breath)
        wrist = (64.0 - recoil if side == "right" else 61.0 - recoil, -1.0 if side == "right" else 1.0, 10.0 + recoil * 0.12)
        hip = (22.0, sign * 5.0, 6.0)
        ankle = (-6.0, sign * 7.0, 2.0)
        joints.update({side + "_shoulder": shoulder, side + "_elbow": elbow(shoulder, wrist, (0.0, sign, -1.0)),
                       side + "_wrist": wrist, side + "_hip": hip, side + "_knee": elbow(hip, ankle, (0.0, sign, 0.0), 15.0),
                       side + "_ankle": ankle, side + "_toe": (-10.0, sign * 7.0, 0.0)})
    return joints


def project(point, angle):
    forward, lateral, height = point
    x = forward * math.cos(angle) - lateral * math.sin(angle)
    y = forward * math.sin(angle) + lateral * math.cos(angle)
    return ((x - y) * 1.35, (x + y) * 0.675 - height)


def generate(output):
    output.mkdir(parents=True, exist_ok=True)
    cell = 192
    image = Image.new("RGB", (cell * 8, cell * 8), "white")
    drawing = ImageDraw.Draw(image)
    cells = []
    maximum_error = 0.0
    for direction, angle in enumerate(ANGLES):
        reference = [project(point, angle) for point in pose(0).values()]
        minimum = (min(point[0] for point in reference), min(point[1] for point in reference))
        maximum = (max(point[0] for point in reference), max(point[1] for point in reference))
        center = ((minimum[0] + maximum[0]) * 0.5, (minimum[1] + maximum[1]) * 0.5)
        for frame in range(8):
            joints = pose(frame)
            raw = {name: project(point, angle) for name, point in joints.items()}
            points = {name: (frame * cell + cell * 0.5 + (point[0] - center[0]) * 0.9,
                             direction * cell + cell * 0.52 + (point[1] - center[1]) * 0.9) for name, point in raw.items()}
            drawing.rectangle((frame * cell, direction * cell, (frame + 1) * cell - 1, (direction + 1) * cell - 1), outline=(215, 220, 225))
            drawing.text((frame * cell + 7, direction * cell + 7), DIRECTIONS[direction] + " / " + str(frame), fill=(40, 45, 50))
            drawing.line([points["pelvis"], points["chest"], points["neck"], points["head"]], fill=(40, 65, 95), width=5)
            for side, color in [("left", (30, 135, 125)), ("right", (160, 75, 60))]:
                arm = [side + suffix for suffix in ["_shoulder", "_elbow", "_wrist"]]
                leg = [side + suffix for suffix in ["_hip", "_knee", "_ankle", "_toe"]]
                drawing.line([points[name] for name in arm], fill=color, width=4)
                drawing.line([points[name] for name in leg], fill=color, width=4)
                for first, second, expected in [(arm[0], arm[1], 14.0), (arm[1], arm[2], 14.0), (leg[0], leg[1], 15.0), (leg[1], leg[2], 15.0)]:
                    maximum_error = max(maximum_error, abs(length(add(joints[first], scale(joints[second], -1))) - expected))
            drawing.line([points["right_wrist"], points["muzzle"]], fill=(20, 25, 30), width=4)
            for name, point in points.items():
                radius = 5 if name == "head" else 2
                drawing.ellipse((point[0] - radius, point[1] - radius, point[0] + radius, point[1] + radius), fill=(30, 35, 40))
            cells.append({"direction": DIRECTIONS[direction], "frame": frame, "phase": PHASES[frame], "joints": joints,
                          "cell": [frame * cell, direction * cell, cell, cell], "projectedJoints": points})
    if maximum_error > 1e-8:
        raise ValueError("Las guías cambian la longitud anatómica de brazos o piernas")
    png = output / "ProneAimFire8x8-guides-v1.png"
    image.save(png)
    for row, direction in enumerate(DIRECTIONS):
        sheet = Image.new("RGB", (cell * 4, cell * 2), "white")
        for frame in range(8):
            whole_guide = image.crop((frame * cell, row * cell, (frame + 1) * cell, (row + 1) * cell))
            sheet.paste(whole_guide, ((frame % 4) * cell, (frame // 4) * cell))
        sheet.save(output / ("ProneAimFire-" + direction + "-guides-v1.png"))
    report = {"author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-17", "role": "anatomical_authoring_guide_not_game_art",
              "directions": DIRECTIONS, "phases": PHASES, "size": list(image.size), "cell": cell, "projection": [1.35, 0.675],
              "maximumBoneLengthError": maximum_error, "sha256": hashlib.sha256(png.read_bytes()).hexdigest(), "cells": cells}
    (output / "ProneAimFire8x8-guides-v1.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"guide": str(png), "cells": len(cells), "maximumBoneLengthError": maximum_error, "sha256": report["sha256"]}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Genera guías anatómicas reproducibles; no fabrica sprites finales con primitivas")
    parser.add_argument("--output", type=Path, default=Path("Art/NoTrace/Characters/Guides"))
    generate(parser.parse_args().output)
