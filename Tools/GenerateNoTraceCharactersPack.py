import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


NAMES = ["head", "torso", "pelvis", "backpack", "left_upper_arm", "left_forearm", "left_hand", "right_upper_arm", "right_forearm", "right_hand", "left_thigh", "left_shin", "left_boot", "right_thigh", "right_shin", "right_boot"]
VERSIONS = {"S": 2, "SE": 3, "E": 3, "NE": 2, "N": 2, "NW": 3, "W": 3, "SW": 2}
DIRECTIONS = list(VERSIONS)
WEAPONS = ["Pistol", "NonLethal", "GuardRifle"]
GRIPS = [(0.5, 0.58), (0.30, 0.65), (0.29, 0.67), (0.37, 0.74), (0.5, 0.75), (0.70, 0.72), (0.71, 0.67), (0.70, 0.66)]
MUZZLES = [(0.5, 0.94), (0.92, 0.72), (0.97, 0.27), (0.83, 0.12), (0.5, 0.04), (0.10, 0.13), (0.03, 0.27), (0.08, 0.74)]


def register_weapons(root):
    records = []
    output = root / "Pieces"
    for weapon in WEAPONS:
        source = root / "Sources" / ("Weapon-" + weapon + "-16views-v1.png")
        image = Image.open(source).convert("RGBA")
        mask = image.getchannel("A").point(lambda alpha: 255 if alpha >= 64 else 0)
        pixels = bytearray(mask.tobytes())
        components = []
        for pixel, alpha in enumerate(pixels):
            if not alpha:
                continue
            pixels[pixel] = 0
            pending = [pixel]
            left = right = pixel % image.width
            top = bottom = pixel // image.width
            count = 0
            while pending:
                current = pending.pop()
                x, y = current % image.width, current // image.width
                left, top, right, bottom = min(left, x), min(top, y), max(right, x), max(bottom, y)
                count += 1
                for neighbor in (current - 1, current + 1, current - image.width, current + image.width):
                    if 0 <= neighbor < len(pixels) and pixels[neighbor] and abs(neighbor % image.width - x) + abs(neighbor // image.width - y) == 1:
                        pixels[neighbor] = 0
                        pending.append(neighbor)
            if count > 1000:
                components.append((left, top, right + 1, bottom + 1))
        if len(components) != 16:
            raise ValueError("Se esperaban16 armas completas aisladas en " + weapon)
        ordered = []
        for row in range(4):
            group = sorted(components, key=lambda box: box[1])[:4]
            ordered.extend(sorted(group, key=lambda box: box[0]))
            components = [box for box in components if box not in group]
        for index in range(16):
            box = ordered[index]
            rect = (max(0, box[0] - 2), max(0, box[1] - 2), min(image.width, box[2] + 2), min(image.height, box[3] + 2))
            cropped = image.crop(rect)
            name = "Weapon-" + weapon + "-" + DIRECTIONS[index % 8] + ("-suppressed" if index >= 8 else "") + ".png"
            cropped.save(output / name)
            records.append({"weapon": weapon, "direction": DIRECTIONS[index % 8], "suppressed": index >= 8, "file": name,
                            "source": source.as_posix(), "sourceSha256": digest(source), "sourceRect": [rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]],
                            "grip": GRIPS[index % 8], "muzzle": MUZZLES[index % 8], "socketUnits": "normalized_full_prop", "sha256": digest(output / name)})
    report = {"author": "Carlos Cortina", "date": "2026-10-18", "weapons": records}
    (root / "Manifests/weapons.registration.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    review = Image.new("RGBA", (96 * 16, 96 * 3), (25, 33, 42, 255))
    for index, record in enumerate(records):
        prop = Image.open(output / record["file"])
        prop.thumbnail((88, 88), Image.Resampling.BOX)
        review.alpha_composite(prop, (index % 16 * 96 + (96 - prop.width) // 2, index // 16 * 96 + (96 - prop.height) // 2))
    review.save(output / "Weapons48-review.png")
    return report


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def separators(mask, vertical):
    extent = mask.width if vertical else mask.height
    result = [0]
    for partition in range(1, 4):
        expected = extent * partition / 4
        candidates = []
        for position in range(max(1, round(expected - extent * 0.09)), min(extent, round(expected + extent * 0.09))):
            line = mask.crop((position, 0, position + 1, mask.height) if vertical else (0, position, mask.width, position + 1))
            if not line.getbbox():
                candidates.append(position)
        if not candidates:
            raise ValueError("Las piezas no admiten una separación completa y limpia en el eje " + ("X" if vertical else "Y"))
        result.append(min(candidates, key=lambda candidate: (abs(candidate - expected), candidate)))
    return result + [extent]


def register(root):
    records = []
    for direction, version in VERSIONS.items():
        source = root / "Sources" / ("OperativeRigParts-" + direction + "-v" + str(version) + ".png")
        image = Image.open(source).convert("RGBA")
        mask = image.getchannel("A").point(lambda alpha: 255 if alpha >= 32 else 0)
        columns = separators(mask, True)
        rows = [separators(mask.crop((columns[column], 0, columns[column + 1], image.height)), False) for column in range(4)]
        pieces = []
        for index, name in enumerate(NAMES):
            column, row = index % 4, index // 4
            cell = (columns[column], rows[column][row], columns[column + 1], rows[column][row + 1])
            bounds = mask.crop(cell).getbbox()
            if not bounds:
                raise ValueError("Falta " + direction + "/" + name)
            rect = [max(cell[0], cell[0] + bounds[0] - 2), max(cell[1], cell[1] + bounds[1] - 2), min(cell[2], cell[0] + bounds[2] + 2), min(cell[3], cell[1] + bounds[3] + 2)]
            pieces.append({"piece": name, "sourceRect": [rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]], "sourcePixelSha256": hashlib.sha256(image.crop(rect).tobytes()).hexdigest(),
                           "proximal": [0.5, 0.12], "distal": [0.5, 0.88], "semanticReview": "direction_inspected_joint_pivots_pending"})
        records.append({"direction": direction, "file": source.as_posix(), "sha256": digest(source), "size": list(image.size), "columnSeparators": columns, "rowSeparatorsByColumn": rows, "pieces": pieces})
    report = {"author": "Carlos Cortina", "date": "2026-10-18", "schemaVersion": 2, "status": "registered_original_parts_joint_review_pending",
              "registrationRule": "Rectángulos completos separados por gutters transparentes, congelados con hashes; no son celdas de cuadrícula supuestas", "sources": records}
    output = root / "Manifests/rig-pieces.registration.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    return report


def pack(root):
    report = json.loads((root / "Manifests/rig-pieces.registration.json").read_text(encoding="utf-8"))
    output = root / "Pieces"
    output.mkdir(parents=True, exist_ok=True)
    atlas = Image.new("RGBA", (64 * 16, 64 * len(report["sources"])))
    frames = []
    for row, source in enumerate(report["sources"]):
        path = Path(source["file"])
        if digest(path) != source["sha256"]:
            raise ValueError("Cambió una fuente registrada: " + str(path))
        original = Image.open(path).convert("RGBA")
        for column, piece in enumerate(source["pieces"]):
            x, y, width, height = piece["sourceRect"]
            cropped = original.crop((x, y, x + width, y + height))
            if hashlib.sha256(cropped.tobytes()).hexdigest() != piece["sourcePixelSha256"]:
                raise ValueError("Cambió un rectángulo de pieza")
            factor = min(60.0 / width, 60.0 / height)
            size = (max(1, round(width * factor)), max(1, round(height * factor)))
            preview = cropped.resize(size, Image.Resampling.BOX)
            atlas.alpha_composite(preview, (column * 64 + (64 - size[0]) // 2, row * 64 + (64 - size[1]) // 2))
            piece_file = source["direction"] + "-" + piece["piece"] + ".png"
            cropped.save(output / piece_file)
            frames.append({"direction": source["direction"], "piece": piece["piece"], "file": piece_file, "sha256": digest(output / piece_file), "sourceRect": piece["sourceRect"]})
    atlas.save(output / "OperativeAuthoredParts-review.png")
    result = {"author": "Carlos Cortina", "date": "2026-10-18", "status": "authored_parts_packed_not_game_atlas", "pieces": frames,
              "atlasSha256": digest(output / "OperativeAuthoredParts-review.png"), "registrationSha256": digest(root / "Manifests/rig-pieces.registration.json")}
    (output / "OperativeAuthoredParts.manifest.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    register_weapons(root)
    print(json.dumps({"pieces": len(frames), "directions": len(report["sources"]), "atlasSha256": result["atlasSha256"], "output": str(output)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Registra y empaqueta piezas completas originales; no inventa fotogramas ni toca Content")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/Characters"))
    parser.add_argument("--register", action="store_true")
    arguments = parser.parse_args()
    if arguments.register:
        register(arguments.root)
    pack(arguments.root)
