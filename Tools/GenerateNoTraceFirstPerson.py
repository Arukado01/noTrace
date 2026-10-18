import argparse
import hashlib
import io
import json
from pathlib import Path

from PIL import Image, ImageDraw


ACTOR_STATES = ["Idle", "WalkA", "WalkB", "Fire", "Hurt", "Crouch", "Prone", "Down", "Dead"]
VARIANTS = ["GuardA", "GuardB", "Ally", "Training"]
WEAPON_STATES = ["Idle", "Fire", "ReloadOut", "ReloadIn"]
ORIGINALS = {
    "GuardA": "exec-60d4df74-a62a-4c24-ad55-d22f8ea55b44.png",
    "GuardB": "exec-bd79c5b3-31a6-4fb0-80a6-565362e0f804.png",
    "Ally": "exec-b2a2d9b1-ef1f-4255-ac50-54ef09afd432.png",
    "Training": "exec-34f9bc4a-c5dd-4d85-80e2-ff949a470fb6.png",
    "ViewmodelPistol": "exec-a34d80bf-8e6e-4737-be14-aae3f28ebec4.png",
    "ViewmodelNonLethal": "exec-1e477bbc-b1b0-4f4d-8f76-e2629e9344de.png",
    "GuardA-WalkB-v2": "exec-88bcf53d-be86-4d83-82e9-0fbd91165e46.png",
    "GuardB-WalkB-v2": "exec-5db941f5-05f6-4615-a22c-f68e8c49d740.png",
    "Ally-WalkB-v2": "exec-1c88bc80-4a92-40b4-a3e2-48cd7e3a3f9c.png",
}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, data):
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".writing")
    temporary.write_bytes(data)
    temporary.replace(path)


def text(path, value):
    write(path, json.dumps(value, indent=2, ensure_ascii=False).encode("utf-8"))


def png(path, image):
    data = io.BytesIO()
    image.save(data, format="PNG")
    write(path, data.getvalue())


def bounds(image, threshold=32):
    return image.getchannel("A").point(lambda value: 255 if value >= threshold else 0).getbbox()


def source_rect(variant, state):
    if variant == "Training":
        pose = 1 if state == 4 else 2 if state in [5, 6, 7] else 3 if state == 8 else 0
        return [(0, 0, 768, 710), (768, 0, 1536, 710), (0, 710, 768, 1024), (768, 710, 1536, 1024)][pose], pose
    if state < 6:
        row, column = divmod(state, 3)
        return (column * 400, row * 505, min(1199, (column + 1) * 400), 505 if row == 0 else 1000), state
    edges = [0, 410, 815, 1199]
    return (edges[state - 6], 1000, edges[state - 5], 1312), state


def extract(image, rect, label):
    crop = image.crop(rect)
    box = bounds(crop)
    if not box:
        raise ValueError("Fuente vacía: " + label)
    solid = bounds(crop, 128)
    if solid and (solid[0] == 0 or solid[1] == 0 or solid[2] == crop.width or solid[3] == crop.height):
        raise ValueError("El rectángulo corta una silueta o invade otra: " + label + str(solid))
    return crop.crop(box), [rect[0] + box[0], rect[1] + box[1], rect[0] + box[2], rect[1] + box[3]]


def generate(root, content=None):
    output = root / "Atlas"
    output.mkdir(parents=True, exist_ok=True)
    actors = Image.new("RGBA", (1152, 512))
    viewmodels = Image.new("RGBA", (1024, 640))
    actor_records, weapon_records, sources = [], [], []
    checks = 0
    for name in ORIGINALS:
        stem = name if name.endswith("-v2") else name + "-v1"
        path = root / "Sources" / (stem + ".png")
        image = Image.open(path).convert("RGBA")
        if image.getchannel("A").getextrema()[0] != 0:
            raise ValueError("La fuente carece de transparencia: " + name)
        sources.append({"file": path.as_posix(), "sha256": digest(path), "size": list(image.size), "alphaExtrema": image.getchannel("A").getextrema(),
                        "prompt": (root / "Prompts" / (stem + ".txt")).as_posix(), "promptSha256": digest(root / "Prompts" / (stem + ".txt")),
                        "generator": "OpenAI image_gen builtin", "original": "C:/Users/desar/.codex/generated_images/01a14cd4-aeb2-70b3-b9fc-0b060cadef2e/" + ORIGINALS[name]})
    for variant_index, variant in enumerate(VARIANTS):
        source_file = root / "Sources" / (variant + "-v1.png")
        source = Image.open(source_file).convert("RGBA")
        idle, _ = extract(source, source_rect(variant, 0)[0], variant + "/Idle")
        reference_scale = 112.0 / idle.height
        for state_index, state in enumerate(ACTOR_STATES):
            source_box, source_pose = source_rect(variant, state_index)
            pose_source, pose_file = source, source_file
            if state_index == 2 and variant != "Training":
                pose_file = root / "Sources" / (variant + "-WalkB-v2.png")
                pose_source = Image.open(pose_file).convert("RGBA")
                source_box = (0, 0, pose_source.width, pose_source.height)
            figure, actual_source = extract(pose_source, source_box, variant + "/" + state)
            maximum_height = 34 if state_index >= 6 else 83 if state_index == 5 else 112
            factor = min(reference_scale, maximum_height / figure.height, 120.0 / figure.width)
            size = (max(1, round(figure.width * factor)), max(1, min(maximum_height, round(figure.height * factor))))
            raster = figure.resize(size, Image.Resampling.BOX)
            cell = Image.new("RGBA", (128, 128))
            cell.alpha_composite(raster, (64 - raster.width // 2, 124 - raster.height))
            opaque = bounds(cell)
            if not opaque or opaque[1] < 4 or opaque[3] > 124 or opaque[3] < 122:
                raise ValueError("Pose sin apoyo/ancla o fuera de volumen: " + variant + "/" + state)
            actors.alpha_composite(cell, (state_index * 128, variant_index * 128))
            actor_records.append({"variant": variant, "state": state, "rect": [state_index * 128, variant_index * 128, 128, 128], "anchor": [64, 124],
                                  "sourceFile": pose_file.as_posix(), "sourceRect": actual_source, "sourcePose": source_pose, "sourceScale": factor, "opaqueBounds": list(opaque),
                                  "physicalHeight": 14 if state_index >= 6 else 34 if state_index == 5 else 46,
                                  "sha256": hashlib.sha256(cell.tobytes()).hexdigest(),
                                  "aliasReason": "Static mechanical target has no locomotion/fire cycle" if variant == "Training" and state_index in [1, 2, 3] else None})
            checks += 5
    walking_supports = []
    for variant_index, variant in enumerate(VARIANTS[:3]):
        support = []
        for state in [1, 2]:
            frame = actors.crop((state * 128, variant_index * 128, (state + 1) * 128, (variant_index + 1) * 128))
            left = sum(frame.getpixel((x, y))[3] >= 32 for x in range(64) for y in range(116, 124))
            right = sum(frame.getpixel((x, y))[3] >= 32 for x in range(64, 128) for y in range(116, 124))
            support.append({"state": ACTOR_STATES[state], "leftFoot": left, "rightFoot": right})
        if not (support[0]["leftFoot"] > support[0]["rightFoot"] and support[1]["rightFoot"] > support[1]["leftFoot"]):
            raise ValueError("La marcha no alterna apoyo delantero real: " + variant + str(support))
        walking_supports.append({"variant": variant, "support": support})
        checks += 1
    weapon_sources = {name: Image.open(root / "Sources" / (name + "-v1.png")).convert("RGBA") for name in ["ViewmodelPistol", "ViewmodelNonLethal"]}
    # Los ocho cuadros comparten escala y línea de muñecas; el fogonazo no reduce las manos al normalizar su altura.
    factor = .32
    x_edges = [0, 384, 752, 1152, 1536]
    for weapon_index, (name, source) in enumerate(weapon_sources.items()):
        for suppressed in [False, True]:
            source_y, baseline = (490, 968) if suppressed else (0, 478 if weapon_index == 1 else 471)
            for state_index, state in enumerate(WEAPON_STATES):
                source_box = (x_edges[state_index], source_y, x_edges[state_index + 1], 1024 if suppressed else 490)
                figure, actual_source = extract(source, source_box, name + "/" + str(suppressed) + "/" + state)
                raster = figure.resize((max(1, round(figure.width * factor)), max(1, round(figure.height * factor))), Image.Resampling.BOX)
                target_x = round(128 + (actual_source[0] - (state_index * 384 + 192)) * factor)
                target_y = round(160 + (actual_source[1] - baseline) * factor)
                if target_y < 0 or target_x < 0 or target_x + raster.width > 256:
                    raise ValueError("Arma/manos no caben sin recorte: " + name + "/" + state)
                cell = Image.new("RGBA", (256, 160))
                cell.alpha_composite(raster, (target_x, target_y))
                row = weapon_index * 2 + int(suppressed)
                viewmodels.alpha_composite(cell, (state_index * 256, row * 160))
                weapon_records.append({"weapon": "Pistol" if weapon_index == 0 else "NonLethal", "suppressed": suppressed, "state": state,
                                       "rect": [state_index * 256, row * 160, 256, 160], "anchor": [128, 160], "sourceRect": actual_source,
                                       "sourceScale": factor, "opaqueBounds": list(bounds(cell)), "sha256": hashlib.sha256(cell.tobytes()).hexdigest()})
                checks += 4
    png(output / "Actors.png", actors)
    png(output / "Viewmodels.png", viewmodels)
    report = {"author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-18", "schemaVersion": 1,
              "generator": "Tools/GenerateNoTraceFirstPerson.py", "generatorSha256": digest(Path(__file__)), "sourceMethod": "new_builtin_imagegen_frontal_identity_adaptations_not_axo_crops",
              "actorCell": [128, 128], "actorColumns": ACTOR_STATES, "actorRows": VARIANTS, "standingPixels": 112, "worldStandingHeight": 46,
              "viewmodelCell": [256, 160], "viewmodelColumns": WEAPON_STATES, "sources": sources, "actors": actor_records, "viewmodels": weapon_records,
              "runtimeFiles": [{"file": name, "sha256": digest(output / name)} for name in ["Actors.png", "Viewmodels.png"]],
              "status": "offline_validated_runtime_pending", "scope": "Frontal raster artwork and source registration; renderer depth/gameplay/LAN validated separately"}
    text(output / "Runtime.json", report)
    text(root / "Manifests/sources.json", {"author": report["author"], "date": report["date"], "sources": sources, "method": report["sourceMethod"]})
    validation = {"status": "PASS", "checks": checks, "actorFrames": len(actor_records), "humanUniqueFrames": len({entry["sha256"] for entry in actor_records if entry["variant"] != "Training"}),
                  "weaponFrames": len(weapon_records), "newSourceImages": len(sources), "walkingSupports": walking_supports,
                  "scope": "Offline alpha, complete source rectangles, anchored pose bounds, real alternate walking support and coherent viewmodel scale"}
    text(output / "Validation.json", validation)
    background = Image.new("RGBA", actors.size, (26, 34, 42, 255)); background.alpha_composite(actors)
    png(output / "Actors-review4x.png", background.resize((actors.width * 4, actors.height * 4), Image.Resampling.NEAREST))
    background = Image.new("RGBA", viewmodels.size, (26, 34, 42, 255)); background.alpha_composite(viewmodels)
    png(output / "Viewmodels-review4x.png", background.resize((viewmodels.width * 4, viewmodels.height * 4), Image.Resampling.NEAREST))
    if content:
        for name in ["Actors.png", "Viewmodels.png", "Runtime.json"]:
            write(content / name, (output / name).read_bytes())
    print(json.dumps(validation))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Empaqueta fuentes frontales originales con rectángulos revisados y anclas estables")
    parser.add_argument("--root", type=Path, default=Path("Art/NoTrace/FirstPerson"))
    parser.add_argument("--content", type=Path)
    args = parser.parse_args()
    generate(args.root, args.content)
