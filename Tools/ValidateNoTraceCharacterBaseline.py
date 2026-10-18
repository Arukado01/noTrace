import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_ROOT = REPOSITORY / "Art/NoTrace/Characters"
BASELINE_NAME = "pre-urgent-character-raster-20261018.json"
PACKING_FIELDS = {"art", "page", "rect", "pixelSha256"}
ANCHOR_FIELDS = ("groundAnchor", "pelvis", "muzzle", "grip", "carryGrip", "supportWidth")
IDENTITY_FIELDS = ("index", "clip", "stance", "direction", "phase")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical_sha(value):
    payload = json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def file_sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def source_records(root):
    records = {}

    def verify(path, expected):
        path = path.resolve()
        actual = file_sha(path)
        require(actual == expected, "SHA original distinto: " + str(path))
        name = path.relative_to(REPOSITORY).as_posix()
        require(name not in records or records[name] == actual, "Fuente registrada con dos SHA: " + name)
        records[name] = actual

    for entry in read_json(root / "Manifests/rig-pieces.registration.json")["sources"]:
        verify(REPOSITORY / entry["file"], entry["sha256"])
    for entry in read_json(root / "Manifests/weapons.registration.json")["weapons"]:
        verify(REPOSITORY / entry["source"], entry["sourceSha256"])
        verify(root / "Pieces" / entry["file"], entry["sha256"])
    return [{"file": name, "sha256": value} for name, value in sorted(records.items())]


def reconstruct_layer(pages, region, size, index, layer):
    label = "índice " + str(index) + ", capa " + layer
    page_index = region["page"]
    require(type(page_index) is int and 0 <= page_index < len(pages), "Página inválida: " + label)
    page = pages[page_index]
    rect, offset = region["rect"], region["offset"]
    require(len(rect) == 4 and len(offset) == 2 and all(type(value) is int for value in rect + offset), "Región inválida: " + label)
    x, y, width, height = rect
    left, top = offset
    require(0 <= x < x + width <= page.width and 0 <= y < y + height <= page.height, "Recorte fuera del atlas: " + label)
    require(0 <= left < left + width <= size and 0 <= top < top + height <= size, "Recorte fuera de celda: " + label)
    canvas = Image.new("RGBA", (size, size))
    # Copiar sin máscara conserva los cuatro canales, incluidos RGB transparentes.
    canvas.paste(page.crop((x, y, x + width, y + height)), (left, top))
    return hashlib.sha256(canvas.tobytes()).hexdigest()


def frame_record(entry, pages, size):
    semantic = {key: value for key, value in entry.items() if key not in PACKING_FIELDS}
    layers = {name: reconstruct_layer(pages, region, size, entry["index"], name) for name, region in sorted(entry["art"].items())}
    require(layers.get("full") == entry["pixelSha256"], "RGBA real y pixelSha256 difieren en índice " + str(entry["index"]))
    return {
        **{key: entry[key] for key in IDENTITY_FIELDS},
        "anchors": {key: entry[key] for key in ANCHOR_FIELDS},
        "semanticSha256": canonical_sha(semantic),
        "jointsSha256": canonical_sha(entry["joints"]),
        "rgbaSha256": layers,
    }


def snapshot(root, provenance):
    metadata_path = root / "Atlas/CharacterAtlasMetadata.json"
    runtime_path = root / "Atlas/CharacterAtlasRuntime.json"
    metadata, runtime = read_json(metadata_path), read_json(runtime_path)
    require(metadata["schemaVersion"] == 3 and metadata["cellSize"] == 192, "Contrato de atlas incorrecto")
    require(len(metadata["frames"]) == 4544 and len(metadata["clips"]) == 53, "La matriz completa debe contener 4544 poses y 53 clips")
    runtime_frames = {entry["index"]: entry for entry in runtime["frames"]}
    require(len(runtime_frames) == 4544 and len(runtime["frames"]) == 4544, "Índices runtime incompletos o duplicados")
    for key, value in runtime.items():
        if key != "frames":
            require(metadata.get(key) == value, "Cabeceras metadata/runtime distintas: " + key)
    pages, page_records = [], []
    for entry in metadata["pages"]:
        path = root / "Atlas/Packed" / entry["file"]
        actual = file_sha(path)
        require(actual == entry["sha256"], "SHA de página incorrecto: " + str(path))
        with Image.open(path) as image:
            page = image.convert("RGBA")
        require(list(page.size) == entry["size"] and max(page.size) <= 2048, "Dimensiones de página incorrectas")
        pages.append(page)
        page_records.append({"file": entry["file"], "sha256": actual})
    frames, seen = [], set()
    for entry in sorted(metadata["frames"], key=lambda frame: frame["index"]):
        index = entry["index"]
        require(type(index) is int and index not in seen, "Índice metadata duplicado o inválido: " + str(index))
        seen.add(index)
        require(index in runtime_frames, "Falta índice runtime " + str(index))
        projected = {key: entry[key] for key in runtime_frames[index]}
        require(projected == runtime_frames[index], "Metadata/runtime difieren en índice " + str(index))
        frames.append(frame_record(entry, pages, metadata["cellSize"]))
    require(seen == set(runtime_frames), "Los conjuntos de índices metadata/runtime difieren")
    sources = source_records(root)
    manifests = ["rig-pieces.registration.json", "weapons.registration.json", "pre-recovery-original-raster.json"]
    header = {key: value for key, value in metadata.items() if key not in {"frames", "pages", "authoringPages", "status"}}
    return {
        "author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-18", "schemaVersion": 1,
        "provenance": {"commit": provenance, "metadataSha256": file_sha(metadata_path), "runtimeSha256": file_sha(runtime_path), "pages": page_records},
        "definition": {
            "cellSize": 192, "channels": "RGBA", "reconstruction": "Crop packed rect and paste all four channels without a mask at offset into a transparent 192x192 canvas; SHA256 of row-major bytes",
            "canonicalJson": "UTF-8; sort_keys=True; separators=(',', ':'); ensure_ascii=False; allow_nan=False",
            "semanticExcludedFields": sorted(PACKING_FIELDS), "packingCoordinatesFrozen": False,
        },
        "headerSha256": canonical_sha(header),
        "sourceRegistrations": [{"file": "Manifests/" + name, "sha256": file_sha(root / "Manifests" / name)} for name in manifests],
        "originalSources": sources,
        "weaponFiles": [{"file": "Atlas/" + name, "sha256": file_sha(root / "Atlas" / name)} for name in ["WeaponProps.png", "WeaponSelection.png"]],
        "frames": frames,
    }


def compare(baseline, current):
    errors = []
    for key in ["definition", "headerSha256", "sourceRegistrations", "originalSources", "weaponFiles"]:
        if baseline[key] != current[key]:
            errors.append("Contrato o fuente distintos: " + key)
    before = {frame["index"]: frame for frame in baseline["frames"]}
    after = {frame["index"]: frame for frame in current["frames"]}
    if set(before) != set(after):
        errors.append("El conjunto de índices cambió")
    for index in sorted(set(before) & set(after)):
        original, candidate = before[index], after[index]
        for key in sorted(set(original) | set(candidate)):
            if key == "rgbaSha256":
                for layer in sorted(set(original[key]) | set(candidate[key])):
                    if original[key].get(layer) != candidate[key].get(layer):
                        errors.append("RGBA distinto: índice " + str(index) + ", capa " + layer)
            elif original.get(key) != candidate.get(key):
                errors.append("Semántica distinta: índice " + str(index) + ", campo " + key)
    return errors


def main():
    parser = argparse.ArgumentParser(description="Congela o compara RGBA y semántica por índice sin depender del packing")
    parser.add_argument("mode", choices=["freeze", "check"])
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--provenance", default="1280d50")
    args = parser.parse_args()
    baseline_path = args.baseline or args.root / "Manifests" / BASELINE_NAME
    if args.mode == "freeze":
        require(not baseline_path.exists(), "El baseline existente es inmutable: " + str(baseline_path))
        current = snapshot(args.root, args.provenance)
        with baseline_path.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(current, stream, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
            stream.write("\n")
        print(json.dumps({"status": "FROZEN", "baseline": str(baseline_path), "sha256": file_sha(baseline_path), "frames": len(current["frames"]), "layers": sum(len(frame["rgbaSha256"]) for frame in current["frames"]), "originalSources": len(current["originalSources"])}))
        return
    baseline = read_json(baseline_path)
    current = snapshot(args.root, baseline["provenance"]["commit"])
    errors = compare(baseline, current)
    print(json.dumps({"status": "FAIL" if errors else "PASS", "frames": len(current["frames"]), "errors": errors}, ensure_ascii=False))
    require(not errors, "El atlas difiere del baseline inmutable")


if __name__ == "__main__":
    main()
