import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from GenerateNoTraceCharactersActions import body_pose, solve
from GenerateNoTraceCharactersGuides import add
from GenerateNoTraceCharactersRig import ANCHOR, DIRECTIONS, project, read_parts, render


def contact_pose(clip, frame):
    points = body_pose(clip, frame)
    angle = math.radians(14.0)
    forward, height = 20.0 * math.sin(angle), 35.0 + 20.0 * math.cos(angle)
    points["neck"] = (forward, 0.0, height)
    points["head"] = (forward + 7.0 * math.sin(angle), 0.0, height + 7.0 * math.cos(angle))
    points["backpack"] = (forward - 3.0, 0.0, height - 9.0)
    squeeze = math.sin(frame / 8.0 * math.tau) * (1.8 if clip == "Choke" else .35)
    for side, sign in [("left", 1.0), ("right", -1.0)]:
        points[side + "_shoulder"] = (forward + 3.0, sign * 6.5, height - 6.0)
        points[side + "_wrist"] = (33.5 if side == "right" else 33.0, sign * (4.5 - squeeze), 52.0 if side == "right" else 51.5)
    return solve(points)


def generate():
    root = Path("Art/NoTrace/Characters")
    output = root / "Prototype"
    canvas = Image.new("RGBA", (240 * 8, 170 * 4), (25, 33, 42, 255))
    draw = ImageDraw.Draw(canvas)
    factor = 46.0 / 72.0
    evidence = []
    for direction_index, direction in enumerate(DIRECTIONS):
        parts = read_parts(root, direction)[0]
        target_points = body_pose("GripVictimHold", 0)
        for clip_index, clip in enumerate(["Grab", "Choke"]):
            for candidate in [False, True]:
                row = clip_index * 2 + int(candidate)
                points = contact_pose(clip, 2) if candidate else body_pose(clip, 2)
                actor = render(parts, points, direction)
                victim = render(parts, target_points, direction)
                actor = actor.resize((round(actor.width * factor), round(actor.height * factor)), Image.Resampling.NEAREST)
                victim = victim.resize(actor.size, Image.Resampling.NEAREST)
                base = (direction_index * 240 + 88.0, row * 170 + 125.0)
                target_delta = project((28.0 / factor, 0.0, 0.0), direction)
                target_feet = (base[0] + (target_delta[0] - ANCHOR[0]) * factor, base[1] + (target_delta[1] - ANCHOR[1]) * factor)
                placements = [(actor, base), (victim, target_feet)]
                if target_feet[1] < base[1]:
                    placements.reverse()
                for image, feet in placements:
                    canvas.alpha_composite(image, (round(feet[0] - ANCHOR[0] * factor), round(feet[1] - ANCHOR[1] * factor)))
                draw.text((direction_index * 240 + 4, row * 170 + 153), direction + "/" + clip + (" candidate14deg" if candidate else " current"), fill=(198, 218, 218))
                evidence.append({"clip": clip, "direction": direction, "candidate": candidate, "physicalSeparation": 28.0, "joints": points})
    canvas.save(output / "CqcContact28-current-vs-candidate.png")
    canvas.resize((canvas.width * 4, canvas.height * 4), Image.Resampling.NEAREST).save(output / "CqcContact28-current-vs-candidate-4x.png")
    (output / "CqcContact28-current-vs-candidate.json").write_text(json.dumps({"status": "candidate_not_integrated", "evidence": evidence}, indent=2), encoding="utf-8")
    print(output / "CqcContact28-current-vs-candidate.png")


if __name__ == "__main__":
    generate()
