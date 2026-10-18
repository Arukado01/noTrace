import argparse
import hashlib
import json
import math
from pathlib import Path

from PIL import Image, ImageDraw

from GenerateNoTraceCharactersGuides import add, dot, elbow, length, scale, unit


NAMES = ["head", "torso", "pelvis", "backpack", "left_upper_arm", "left_forearm", "left_hand", "right_upper_arm", "right_forearm", "right_hand", "left_thigh", "left_shin", "left_boot", "right_thigh", "right_shin", "right_boot"]
CLIPS = ["Idle", "Walk", "Run", "CrouchIdle", "CrouchWalk"]
DIRECTIONS = ["S", "SE", "E", "NE", "N", "NW", "W", "SW"]
CELL = 192
ANCHOR = (96.0, 144.0)


def local(point, direction="S"):
    forward, side, height = point
    angle = math.pi / 4.0 - DIRECTIONS.index(direction) * math.pi / 4.0
    return (forward * math.cos(angle) + side * math.sin(angle), forward * math.sin(angle) - side * math.cos(angle), height)


def project(point, direction="S"):
    x, y, height = local(point, direction)
    return (ANCHOR[0] + (x - y) * 1.35, ANCHOR[1] + (x + y) * 0.675 - height)


def skeleton(clip, frame):
    phase = frame / 8.0 * math.tau
    walking = clip in ["Walk", "Run", "CrouchWalk"]
    running = clip == "Run"
    crouched = clip.startswith("Crouch")
    amplitude = 12.0 if running else 7.0 if walking else 0.0
    pelvis_height = (20.0 if crouched else 32.0 if running else 34.5 if walking else 35.0) + (math.sin(phase * 2.0) * 0.4 if walking else 0.0)
    lean = 4.0 if running else 2.5 if crouched else 0.0
    pelvis = (0.0, 0.0, pelvis_height)
    neck = (lean, 0.0, pelvis_height + (21.0 if crouched else 22.0 if running else 20.0) + (math.sin(phase) * 0.7 if not walking else 0.0))
    points = {"pelvis": pelvis, "neck": neck, "head": add(neck, (0.0, 0.0, 7.0)), "backpack": add(neck, (-3.0, 0.0, -9.0))}
    for side, sign in [("left", 1.0), ("right", -1.0)]:
        step = math.sin(phase + (0.0 if side == "left" else math.pi))
        foot = (amplitude * step, sign * 5.5, 2.0 + (max(0.0, math.cos(phase + (0.0 if side == "left" else math.pi))) * (7.0 if running else 4.0) if walking else 0.0))
        hip = add(pelvis, (0.0, sign * 4.0, 0.0))
        knee = elbow(hip, foot, (1.0, sign * 0.1, 0.0), 17.0)
        shoulder = add(neck, (-3.0, sign * 6.5, -6.0))
        hand = add(shoulder, (-step * amplitude * 0.75, sign * 0.5, -23.0 if not crouched else -20.0))
        middle = elbow(shoulder, hand, (1.0, sign * 0.08, -0.1), 13.0)
        points.update({side + "_hip": hip, side + "_knee": knee, side + "_ankle": foot, side + "_toe": add(foot, (5.0, sign * 1.5, -2.0)),
                       side + "_shoulder": shoulder, side + "_elbow": middle, side + "_wrist": hand})
    return points


def read_parts(root, direction):
    result, metadata = {}, []
    for index, name in enumerate(NAMES):
        source = root / "Pieces" / (direction + "-" + name + ".png")
        piece = Image.open(source).convert("RGBA")
        mip_scale = min(1.0, 64.0 / max(piece.size))
        result[name] = piece.resize((max(1, round(piece.width * mip_scale)), max(1, round(piece.height * mip_scale))), Image.Resampling.BOX)
        metadata.append({"piece": name, "file": source.as_posix(), "sourceSha256": hashlib.sha256(source.read_bytes()).hexdigest()})
    return result, metadata


def bone(canvas, piece, first, second, width, source_prox=(0.5, 0.12), source_dist=(0.5, 0.88)):
    source_a = (piece.width * source_prox[0], piece.height * source_prox[1])
    source_b = (piece.width * source_dist[0], piece.height * source_dist[1])
    source_delta = add(source_b, scale(source_a, -1.0))
    source_axis = unit(source_delta)
    source_side = (-source_axis[1], source_axis[0])
    target_delta = add(second, scale(first, -1.0))
    target_length = length(target_delta)
    if target_length < 0.1:
        return
    target_axis = unit(target_delta)
    target_side = (-target_axis[1], target_axis[0])
    along = target_length / length(source_delta)
    across = width / piece.width
    matrix = [source_axis[0] * target_axis[0] / along + source_side[0] * target_side[0] / across,
              source_axis[0] * target_axis[1] / along + source_side[0] * target_side[1] / across,
              0.0,
              source_axis[1] * target_axis[0] / along + source_side[1] * target_side[0] / across,
              source_axis[1] * target_axis[1] / along + source_side[1] * target_side[1] / across,
              0.0]
    matrix[2] = source_a[0] - matrix[0] * first[0] - matrix[1] * first[1]
    matrix[5] = source_a[1] - matrix[3] * first[0] - matrix[4] * first[1]
    canvas.alpha_composite(piece.transform(canvas.size, Image.Transform.AFFINE, matrix, resample=Image.Resampling.NEAREST))


def object_piece(canvas, piece, center, height):
    factor = height / piece.height
    resized = piece.resize((max(1, round(piece.width * factor)), round(height)), Image.Resampling.NEAREST)
    canvas.alpha_composite(resized, (round(center[0] - resized.width * 0.5), round(center[1] - resized.height * 0.5)))


def render(parts, points, direction="S", layer="full"):
    image = Image.new("RGBA", (CELL, CELL))
    projected = {name: project(point, direction) for name, point in points.items()}
    torso_axis = unit(add(points["neck"], scale(points["pelvis"], -1.0)))
    sides = sorted(["right", "left"], key=lambda side: sum(local(points[side + "_hip"], direction)[:2]))
    if layer not in ["lower", "cqcArms"] and direction not in ["NE", "N", "NW"]:
        bone(image, parts["backpack"], project(add(points["backpack"], scale(torso_axis, -9.0)), direction),
             project(add(points["backpack"], scale(torso_axis, 9.0)), direction), 16.0, (0.5, 0.9), (0.5, 0.1))
    if layer not in ["upper", "cqcArms"]:
        for side in sides:
            bone(image, parts[side + "_thigh"], projected[side + "_hip"], projected[side + "_knee"], 10.0)
            bone(image, parts[side + "_shin"], projected[side + "_knee"], projected[side + "_ankle"], 9.0)
            bone(image, parts[side + "_boot"], projected[side + "_ankle"], projected[side + "_toe"], 9.0, (0.6, 0.14), (0.4, 0.8))
        bone(image, parts["pelvis"], project(add(points["pelvis"], scale(torso_axis, 4.0)), direction),
             project(add(points["pelvis"], scale(torso_axis, -4.0)), direction), 14.0, (0.5, 0.12), (0.5, 0.88))
    if layer == "lower":
        return image
    if layer != "cqcBody":
        for side in sides:
            bone(image, parts[side + "_upper_arm"], projected[side + "_shoulder"], projected[side + "_elbow"], 8.0)
    chest_width = 18.0 if direction in ["E", "W"] else 22.0 if direction in ["SE", "NE", "NW", "SW"] else 25.0
    neck_x = 0.38 if direction == "E" else 0.62 if direction == "W" else 0.5
    if layer != "cqcArms":
        bone(image, parts["torso"], projected["pelvis"], projected["neck"], chest_width, (0.5, 0.9), (neck_x, 0.08))
    if layer not in ["cqcBody", "cqcArms"] and direction not in ["S", "N"]:
        near = sides[-1]
        overlap = 0.3 if direction in ["E", "W"] else 0.2
        bone(image, parts[near + "_upper_arm"], projected[near + "_shoulder"], projected[near + "_elbow"], 9.0, (0.5, overlap), (0.5, 0.88))
    if layer != "cqcBody":
        for side in sides:
            bone(image, parts[side + "_forearm"], projected[side + "_elbow"], projected[side + "_wrist"], 7.5, (0.5, 0.12), (0.5, 0.72 if direction == "E" else 0.88))
            if direction != "E":
                object_piece(image, parts[side + "_hand"], projected[side + "_wrist"], 5.5)
    head_axis = unit(add(points["head"], scale(points["neck"], -1.0)))
    if layer != "cqcArms":
        bone(image, parts["head"], project(add(points["head"], scale(head_axis, -6.0)), direction),
             project(add(points["head"], scale(head_axis, 6.0)), direction), 14.0, (0.5, 0.94), (0.5, 0.08))
    return image


def generate(root, directions):
    output = root / "Prototype"
    output.mkdir(parents=True, exist_ok=True)
    sheet = Image.new("RGBA", (CELL * 8, CELL * len(CLIPS) * len(directions)))
    review = Image.new("RGBA", (CELL * len(directions), CELL * len(CLIPS)), (25, 33, 42, 255))
    frames, sequences, metadata = [], [], []
    for direction_index, direction in enumerate(directions):
        parts, sources = read_parts(root, direction)
        metadata.extend(sources)
        for row, clip in enumerate(CLIPS):
            sequence = []
            for frame in range(8):
                points = skeleton(clip, frame)
                image = render(parts, points, direction)
                sheet.alpha_composite(image, (frame * CELL, (direction_index * len(CLIPS) + row) * CELL))
                if frame == 2:
                    review.alpha_composite(image, (direction_index * CELL, row * CELL))
                frames.append({"clip": clip, "frame": frame, "direction": direction, "joints": points, "groundAnchor": ANCHOR, "pixelSha256": hashlib.sha256(image.tobytes()).hexdigest()})
                sequence.append(image)
            sequences.append(sequence)
    label = "S" if directions == ["S"] else "8dir"
    sheet.save(output / ("RigQualityGate-" + label + "-current-native.png"))
    enlarged = sheet.resize((sheet.width * 4, sheet.height * 4), Image.Resampling.NEAREST)
    background = Image.new("RGBA", enlarged.size, (25, 33, 42, 255)); background.alpha_composite(enlarged)
    background.save(output / ("RigQualityGate-" + label + "-4x.png"))
    review.resize((review.width * 4, review.height * 4), Image.Resampling.NEAREST).save(output / ("RigQualityGate-" + label + "-review4x.png"))
    gallery = []
    for frame in range(8):
        view = Image.new("RGBA", (CELL * len(CLIPS), (CELL + 16) * len(directions)), (25, 33, 42, 255))
        drawing = ImageDraw.Draw(view)
        for column, sequence in enumerate(sequences):
            x, y = column % len(CLIPS) * CELL, column // len(CLIPS) * (CELL + 16)
            view.alpha_composite(sequence[frame], (x, y))
            drawing.text((x + 4, y + CELL + 1), directions[column // len(CLIPS)] + "/" + CLIPS[column % len(CLIPS)], fill=(198, 218, 218))
        gallery.append(view.resize((view.width * 4, view.height * 4), Image.Resampling.NEAREST))
    gallery[0].save(output / ("RigQualityGate-" + label + "-animated.png"), save_all=True, append_images=gallery[1:], duration=125, loop=0)
    report = {"author": "Carlos Cortina", "date": "2026-10-18", "status": "prototype_requires_visual_review", "directions": directions, "pieces": metadata, "frames": frames,
              "warning": "Prototipo de cinco ciclos; el atlas vigente se genera con GenerateNoTraceCharactersActions.py"}
    (output / ("RigQualityGate-" + label + ".json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({"prototype": str(output / ("RigQualityGate-" + label + "-current-native.png")), "frames": len(frames), "clips": CLIPS, "directions": directions}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Prueba rig articulado con piezas originales autoradas y longitudes anatómicas constantes")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    parser.add_argument("--direction", choices=DIRECTIONS)
    arguments = parser.parse_args()
    generate(arguments.root, [arguments.direction] if arguments.direction else DIRECTIONS)
