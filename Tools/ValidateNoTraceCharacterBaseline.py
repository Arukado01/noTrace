import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_ROOT = REPOSITORY / "Art/NoTrace/Characters"
BASELINE_NAME = "pre-urgent-character-raster-20261018.json"
REFERENCES_NAME = "pre-urgent-character-canonical-references-20261018.json"
REGISTRATION_FILES = ("Manifests/rig-pieces.registration.json", "Manifests/weapons.registration.json", "Manifests/pre-recovery-original-raster.json")
CANONICAL_JSON = "UTF-8; sort_keys=True; separators=(',', ':'); ensure_ascii=False; allow_nan=False"
PACKING_FIELDS = {"art", "page", "rect", "pixelSha256"}
ANCHOR_FIELDS = ("groundAnchor", "pelvis", "muzzle", "grip", "carryGrip", "supportWidth")
IDENTITY_FIELDS = ("index", "clip", "stance", "direction", "phase")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def canonical_sha(value):
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def file_sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_json(path):
    def unique_object(pairs):
        result = {}
        for key, value in pairs:
            require(key not in result, "Clave JSON duplicada: " + key + " en " + str(path))
            result[key] = value
        return result

    def invalid_constant(value):
        raise ValueError("Constante JSON inválida: " + value + " en " + str(path))

    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=unique_object, parse_constant=invalid_constant)


def registration_contracts(root):
    return [{"file": name, "canonicalSha256": canonical_sha(read_json(root / name))} for name in REGISTRATION_FILES]


def create_references(root, baseline):
    original = {entry["file"]: entry["sha256"] for entry in baseline["sourceRegistrations"]}
    require(len(original) == len(baseline["sourceRegistrations"]) == 3 and set(original) == set(REGISTRATION_FILES), "El baseline no contiene los tres contratos originales")
    # Esta creación explícita exige los bytes originales antes de fijar contratos portables.
    for name in REGISTRATION_FILES:
        require(file_sha(root / name) == original[name], "SHA original distinto antes de crear referencias: " + name)
    return {"schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(baseline), "canonicalJson": CANONICAL_JSON, "sourceRegistrations": registration_contracts(root)}


def validate_references(baseline, references):
    require(set(references) == {"schemaVersion", "baselineCanonicalSha256", "canonicalJson", "sourceRegistrations"}, "Campos del sidecar canónico incorrectos")
    require(references["schemaVersion"] == 1 and references["canonicalJson"] == CANONICAL_JSON, "Contrato canónico incorrecto")
    require(references["baselineCanonicalSha256"] == canonical_sha(baseline), "Sidecar ligado a otro baseline")
    contracts = references["sourceRegistrations"]
    names = [entry["file"] for entry in contracts]
    require(len(names) == 3 and set(names) == set(REGISTRATION_FILES), "El sidecar debe contener los tres contratos originales")
    for entry in contracts:
        require(set(entry) == {"file", "canonicalSha256"} and len(entry["canonicalSha256"]) == 64 and all(character in "0123456789abcdef" for character in entry["canonicalSha256"]), "Hash canónico inválido: " + entry["file"])


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
    header = {key: value for key, value in metadata.items() if key not in {"frames", "pages", "authoringPages", "status"}}
    return {
        "author": "Carlos Cortina", "organization": "CIO", "date": "2026-10-18", "schemaVersion": 1,
        "provenance": {"commit": provenance, "metadataSha256": file_sha(metadata_path), "runtimeSha256": file_sha(runtime_path), "pages": page_records},
        "definition": {
            "cellSize": 192, "channels": "RGBA", "reconstruction": "Crop packed rect and paste all four channels without a mask at offset into a transparent 192x192 canvas; SHA256 of row-major bytes",
            "canonicalJson": CANONICAL_JSON,
            "semanticExcludedFields": sorted(PACKING_FIELDS), "packingCoordinatesFrozen": False,
        },
        "headerSha256": canonical_sha(header),
        "sourceRegistrations": [{"file": name, "sha256": file_sha(root / name)} for name in REGISTRATION_FILES],
        "originalSources": sources,
        "weaponFiles": [{"file": "Atlas/" + name, "sha256": file_sha(root / "Atlas" / name)} for name in ["WeaponProps.png", "WeaponSelection.png"]],
        "frames": frames,
    }


def compare(baseline, current, references=None, contracts=None):
    errors = []
    if references is not None:
        validate_references(baseline, references)
        require(contracts is not None, "Faltan los contratos actuales")
        expected = {entry["file"]: entry["canonicalSha256"] for entry in references["sourceRegistrations"]}
        actual = {entry["file"]: entry["canonicalSha256"] for entry in contracts}
        require(len(contracts) == len(actual) == 3 and set(actual) == set(REGISTRATION_FILES), "Contratos actuales incompletos o duplicados")
        for name in REGISTRATION_FILES:
            if actual[name] != expected[name]:
                errors.append("Contrato JSON distinto: " + name)
    elif baseline["sourceRegistrations"] != current["sourceRegistrations"]:
        errors.append("Contrato o fuente distintos: sourceRegistrations")
    for key in ["definition", "headerSha256", "originalSources", "weaponFiles"]:
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
    parser.add_argument("mode", choices=["freeze", "freeze-references", "check"])
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--references", type=Path)
    parser.add_argument("--provenance", default="1280d50")
    args = parser.parse_args()
    baseline_path = args.baseline or args.root / "Manifests" / BASELINE_NAME
    references_path = args.references or args.root / "Manifests" / REFERENCES_NAME
    if args.mode == "freeze":
        require(not baseline_path.exists(), "El baseline existente es inmutable: " + str(baseline_path))
        current = snapshot(args.root, args.provenance)
        with baseline_path.open("x", encoding="utf-8", newline="\n") as stream:
            json.dump(current, stream, ensure_ascii=False, separators=(",", ":"), allow_nan=False)
            stream.write("\n")
        print(json.dumps({"status": "FROZEN", "baseline": str(baseline_path), "sha256": file_sha(baseline_path), "frames": len(current["frames"]), "layers": sum(len(frame["rgbaSha256"]) for frame in current["frames"]), "originalSources": len(current["originalSources"])}))
        return
    baseline = read_json(baseline_path)
    if args.mode == "freeze-references":
        require(not references_path.exists(), "El sidecar existente es inmutable: " + str(references_path))
        references = create_references(args.root, baseline)
        with references_path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(canonical_json(references) + "\n")
        print(json.dumps({"status": "REFERENCES_FROZEN", "references": str(references_path), "baselineCanonicalSha256": references["baselineCanonicalSha256"], "referencesCanonicalSha256": canonical_sha(references)}))
        return
    references = read_json(references_path)
    validate_references(baseline, references)
    contracts = registration_contracts(args.root)
    current = snapshot(args.root, baseline["provenance"]["commit"])
    errors = compare(baseline, current, references, contracts)
    print(json.dumps({"status": "FAIL" if errors else "PASS", "frames": len(current["frames"]), "canonicalSourceContracts": len(contracts), "baselineCanonicalSha256": canonical_sha(baseline), "errors": errors}, ensure_ascii=False))
    require(not errors, "El atlas difiere del baseline inmutable")


if __name__ == "__main__":
    main()
