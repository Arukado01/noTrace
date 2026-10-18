import argparse
import copy
import hashlib
import json
import math
from pathlib import Path

from PIL import Image


REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_ROOT = REPOSITORY / "Art/NoTrace/Characters"
BASELINE_NAME = "pre-urgent-character-raster-20261018.json"
REFERENCES_NAME = "pre-urgent-character-canonical-references-20261018.json"
TOE_WITNESS_NAME = "pre-urgent-posture-toes-witness-20261018.json"
REGISTRATION_FILES = ("Manifests/rig-pieces.registration.json", "Manifests/weapons.registration.json", "Manifests/pre-recovery-original-raster.json")
CANONICAL_JSON = "UTF-8; sort_keys=True; separators=(',', ':'); ensure_ascii=False; allow_nan=False"
PACKING_FIELDS = {"art", "page", "rect", "pixelSha256"}
ANCHOR_FIELDS = ("groundAnchor", "pelvis", "muzzle", "grip", "carryGrip", "supportWidth")
IDENTITY_FIELDS = ("index", "clip", "stance", "direction", "phase")
TOE_JOINTS = ("left_toe", "right_toe")
TOE_IDENTITIES = {
    512 + direction_index * 64 + first + phase: {"clip": clip, "stance": "Standing", "direction": direction, "phase": phase}
    for direction_index, direction in enumerate(["S", "SE", "E", "NE", "N", "NW", "W", "SW"])
    for clip, first, phases in [("LieDown", 0, range(3, 7)), ("StandUp", 24, range(1, 7)), ("Dive", 32, [1])]
    for phase in phases
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def canonical_json(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False, allow_nan=False)


def canonical_sha(value):
    return hashlib.sha256(canonical_json(value).encode("utf-8")).hexdigest()


def valid_sha(value):
    return isinstance(value, str) and len(value) == 64 and all(character in "0123456789abcdef" for character in value)


def semantic_record(entry):
    return {key: value for key, value in entry.items() if key not in PACKING_FIELDS}


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
    semantic = semantic_record(entry)
    layers = {name: reconstruct_layer(pages, region, size, entry["index"], name) for name, region in sorted(entry["art"].items())}
    require(layers.get("full") == entry["pixelSha256"], "RGBA real y pixelSha256 difieren en índice " + str(entry["index"]))
    return {
        **{key: entry[key] for key in IDENTITY_FIELDS},
        "anchors": {key: entry[key] for key in ANCHOR_FIELDS},
        "semanticSha256": canonical_sha(semantic),
        "jointsSha256": canonical_sha(entry["joints"]),
        "rgbaSha256": layers,
    }


def snapshot(root, provenance, semantic_records=None):
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
        if semantic_records is not None and index in TOE_IDENTITIES:
            semantic_records[index] = semantic_record(entry)
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


def create_toe_witness(baseline, original_metadata_path):
    require(file_sha(original_metadata_path) == baseline["provenance"]["metadataSha256"], "El testigo exige bytes de metadata originales")
    metadata = read_json(original_metadata_path)
    before = {entry["index"]: entry for entry in baseline["frames"]}
    original = {entry["index"]: entry for entry in metadata["frames"]}
    require(len(before) == len(original) == len(metadata["frames"]) == 4544 and set(before) == set(original), "La metadata original no contiene la matriz exacta")
    for index, entry in original.items():
        require(canonical_sha(semantic_record(entry)) == before[index]["semanticSha256"], "Semántica original distinta: " + str(index))
        require(canonical_sha(entry["joints"]) == before[index]["jointsSha256"], "Joints originales distintos: " + str(index))
    witness = {
        "schemaVersion": 1, "baselineCanonicalSha256": canonical_sha(baseline),
        "originalMetadataSha256": baseline["provenance"]["metadataSha256"], "allowedJoints": list(TOE_JOINTS),
        "frames": [{"index": index, "semantic": semantic_record(original[index]),
                    "semanticSha256": before[index]["semanticSha256"], "jointsSha256": before[index]["jointsSha256"]}
                   for index in sorted(TOE_IDENTITIES)],
    }
    validate_toe_witness(baseline, witness)
    return witness


def validate_toe_witness(baseline, witness):
    require(isinstance(witness, dict) and set(witness) == {"schemaVersion", "baselineCanonicalSha256", "originalMetadataSha256", "allowedJoints", "frames"}, "Campos del testigo de pies incorrectos")
    require(type(witness["schemaVersion"]) is int and witness["schemaVersion"] == 1, "Versión del testigo incorrecta")
    require(witness["baselineCanonicalSha256"] == canonical_sha(baseline), "Testigo ligado a otro baseline")
    require(witness["originalMetadataSha256"] == baseline["provenance"]["metadataSha256"], "SHA de metadata del testigo distinto")
    require(witness["allowedJoints"] == list(TOE_JOINTS), "El testigo sólo admite las dos puntas")
    require(isinstance(witness["frames"], list) and len(witness["frames"]) == 88, "El testigo debe contener 88 registros")
    before = {entry["index"]: entry for entry in baseline["frames"]}
    records = {}
    for row in witness["frames"]:
        require(isinstance(row, dict) and set(row) == {"index", "semantic", "semanticSha256", "jointsSha256"}, "Registro del testigo incorrecto")
        index = row["index"]
        require(type(index) is int and index in TOE_IDENTITIES and index in before and index not in records, "Índice del testigo inválido o duplicado")
        semantic = row["semantic"]
        require(isinstance(semantic, dict) and isinstance(semantic.get("joints"), dict), "Semántica del testigo inválida")
        require(canonical_sha({key: semantic.get(key) for key in TOE_IDENTITIES[index]}) == canonical_sha(TOE_IDENTITIES[index]) and type(semantic.get("index")) is int and semantic["index"] == index, "Identidad del testigo distinta")
        require(all(joint in semantic["joints"] for joint in TOE_JOINTS), "Faltan puntas originales")
        require(row["semanticSha256"] == before[index]["semanticSha256"] == canonical_sha(semantic), "Semántica del testigo no coincide con baseline")
        require(row["jointsSha256"] == before[index]["jointsSha256"] == canonical_sha(semantic["joints"]), "Joints del testigo no coinciden con baseline")
        records[index] = semantic
    require(set(records) == set(TOE_IDENTITIES), "El testigo cambia el conjunto de 88 índices")
    return records


def exception_contract(baseline, current, exceptions, toe_witness=None, semantic_records=None):
    if exceptions is None:
        return {}
    require(isinstance(exceptions, dict) and set(exceptions) == {"schemaVersion", "baselineCanonicalSha256", "toeWitnessCanonicalSha256", "entries"}, "Campos de excepciones incorrectos")
    require(type(exceptions["schemaVersion"]) is int and exceptions["schemaVersion"] == 1, "Versión de excepciones incorrecta")
    require(exceptions["baselineCanonicalSha256"] == canonical_sha(baseline), "Excepciones ligadas a otro baseline")
    entries = exceptions["entries"]
    require(isinstance(entries, list) and entries, "La lista explícita de excepciones está vacía")
    before = {entry["index"]: entry for entry in baseline["frames"]}
    after = {entry["index"]: entry for entry in current["frames"]}
    require(len(before) == len(baseline["frames"]) and len(after) == len(current["frames"]), "Índices duplicados antes de validar excepciones")
    result, witnessed = {}, None
    for row in entries:
        require(isinstance(row, dict) and set(row) == {"index", "identity", "reason", "rgbaSha256", "semantic"}, "Entrada de excepción incorrecta")
        require(isinstance(row["reason"], str) and bool(row["reason"].strip()), "El motivo de excepción debe ser texto no vacío")
        index = row["index"]
        require(type(index) is int and index in before and index in after and index not in result, "Índice de excepción inválido o duplicado")
        original, candidate = before[index], after[index]
        identity = {key: original[key] for key in IDENTITY_FIELDS if key != "index"}
        require(isinstance(row["identity"], dict) and canonical_sha(row["identity"]) == canonical_sha(identity), "Identidad de excepción distinta: " + str(index))
        layers = row["rgbaSha256"]
        require(isinstance(layers, dict), "Las capas esperadas deben ser un objeto")
        require(set(layers) <= set(original["rgbaSha256"]) and set(layers) <= set(candidate["rgbaSha256"]), "La excepción inventa una capa")
        for layer, expected in layers.items():
            require(valid_sha(expected), "SHA RGBA esperado inválido")
            require(expected != original["rgbaSha256"][layer], "Excepción RGBA ociosa: " + str(index) + "/" + layer)
            require(candidate["rgbaSha256"][layer] == expected, "RGBA no coincide con expected SHA: " + str(index) + "/" + layer)
        semantic = row["semantic"]
        require(layers or semantic is not None, "Entrada de excepción ociosa")
        if semantic is not None:
            require(isinstance(semantic, dict) and set(semantic) == {"kind", "expectedSemanticSha256", "expectedJointsSha256"}, "Excepción semántica incorrecta")
            require(semantic["kind"] == "posture_toes_v1" and index in TOE_IDENTITIES, "Semántica permitida sólo en los 88 índices de pies")
            require(identity == TOE_IDENTITIES[index], "Identidad semántica de pies distinta")
            require(all(valid_sha(semantic[key]) for key in ["expectedSemanticSha256", "expectedJointsSha256"]), "SHA semántico esperado inválido")
            require(semantic["expectedSemanticSha256"] != original["semanticSha256"] and semantic["expectedJointsSha256"] != original["jointsSha256"], "Excepción semántica ociosa")
            require(toe_witness is not None and semantic_records is not None and index in semantic_records, "Falta testigo o semántica real para pies")
            require(valid_sha(exceptions["toeWitnessCanonicalSha256"]) and exceptions["toeWitnessCanonicalSha256"] == canonical_sha(toe_witness), "SHA del testigo semántico distinto")
            if witnessed is None:
                witnessed = validate_toe_witness(baseline, toe_witness)
            actual = semantic_records[index]
            require(isinstance(actual, dict) and isinstance(actual.get("joints"), dict), "Semántica actual inválida")
            expected = copy.deepcopy(witnessed[index])
            for joint in TOE_JOINTS:
                point = actual["joints"].get(joint)
                require(isinstance(point, (list, tuple)) and len(point) == 3 and all(type(value) in [int, float] and math.isfinite(value) for value in point), "Punta no finita o inválida")
                expected["joints"][joint] = point
            require(canonical_sha(actual) == canonical_sha(expected), "La excepción cambia campos o joints ajenos a las puntas: " + str(index))
            require(canonical_sha(actual) == candidate["semanticSha256"] == semantic["expectedSemanticSha256"], "Semántica no coincide con expected SHA: " + str(index))
            require(canonical_sha(actual["joints"]) == candidate["jointsSha256"] == semantic["expectedJointsSha256"], "Joints no coinciden con expected SHA: " + str(index))
        result[index] = row
    require(witnessed is not None or exceptions["toeWitnessCanonicalSha256"] is None, "Testigo declarado sin excepciones semánticas")
    return result


def compare(baseline, current, references=None, contracts=None, exceptions=None, toe_witness=None, semantic_records=None):
    errors = []
    for label, document in [("baseline", baseline), ("actual", current)]:
        indices = [frame["index"] for frame in document["frames"]]
        require(all(type(index) is int for index in indices) and len(indices) == len(set(indices)), "Índices inválidos o duplicados en " + label)
    approved = exception_contract(baseline, current, exceptions, toe_witness, semantic_records)
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
        exception = approved.get(index)
        for key in sorted(set(original) | set(candidate)):
            if key == "rgbaSha256":
                for layer in sorted(set(original[key]) | set(candidate[key])):
                    if original[key].get(layer) != candidate[key].get(layer):
                        if exception is None or exception["rgbaSha256"].get(layer) != candidate[key].get(layer):
                            errors.append("RGBA distinto: índice " + str(index) + ", capa " + layer)
            elif canonical_json(original.get(key)) != canonical_json(candidate.get(key)):
                if exception is None or exception["semantic"] is None or key not in {"semanticSha256", "jointsSha256"}:
                    errors.append("Semántica distinta: índice " + str(index) + ", campo " + key)
    return errors


def validate_current(root, baseline_path=None, references_path=None, exceptions_path=None, toe_witness_path=None):
    baseline_path = baseline_path or root / "Manifests" / BASELINE_NAME
    references_path = references_path or root / "Manifests" / REFERENCES_NAME
    baseline, references = read_json(baseline_path), read_json(references_path)
    validate_references(baseline, references)
    exceptions = read_json(exceptions_path) if exceptions_path is not None else None
    require(exceptions is None or isinstance(exceptions, dict), "El contrato de excepciones debe ser un objeto")
    needs_witness = exceptions is not None and isinstance(exceptions.get("entries"), list) and any(isinstance(row, dict) and row.get("semantic") is not None for row in exceptions["entries"])
    toe_witness = read_json(toe_witness_path or root / "Manifests" / TOE_WITNESS_NAME) if needs_witness else None
    semantics = {}
    current = snapshot(root, baseline["provenance"]["commit"], semantics)
    contracts = registration_contracts(root)
    errors = compare(baseline, current, references, contracts, exceptions, toe_witness, semantics)
    return {"status": "FAIL" if errors else "PASS", "frames": len(current["frames"]),
            "layers": sum(len(frame["rgbaSha256"]) for frame in current["frames"]),
            "canonicalSourceContracts": len(contracts), "baselineCanonicalSha256": canonical_sha(baseline),
            "exactExceptionIndices": sorted(row["index"] for row in exceptions["entries"]) if exceptions is not None else [],
            "toeWitnessCanonicalSha256": canonical_sha(toe_witness) if toe_witness is not None else None, "errors": errors}


def main():
    parser = argparse.ArgumentParser(description="Congela o compara RGBA y semántica por índice sin depender del packing")
    parser.add_argument("mode", choices=["freeze", "freeze-references", "freeze-toe-witness", "check"])
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--references", type=Path)
    parser.add_argument("--exceptions", type=Path)
    parser.add_argument("--toe-witness", type=Path)
    parser.add_argument("--original-metadata", type=Path)
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
    if args.mode == "freeze-toe-witness":
        require(args.original_metadata is not None, "Crear el testigo exige --original-metadata explícito")
        witness_path = args.toe_witness or args.root / "Manifests" / TOE_WITNESS_NAME
        require(not witness_path.exists(), "El testigo existente es inmutable: " + str(witness_path))
        witness = create_toe_witness(baseline, args.original_metadata)
        with witness_path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(canonical_json(witness) + "\n")
        print(json.dumps({"status": "TOE_WITNESS_FROZEN", "witness": str(witness_path), "records": len(witness["frames"]), "canonicalSha256": canonical_sha(witness)}))
        return
    if args.mode == "freeze-references":
        require(not references_path.exists(), "El sidecar existente es inmutable: " + str(references_path))
        references = create_references(args.root, baseline)
        with references_path.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(canonical_json(references) + "\n")
        print(json.dumps({"status": "REFERENCES_FROZEN", "references": str(references_path), "baselineCanonicalSha256": references["baselineCanonicalSha256"], "referencesCanonicalSha256": canonical_sha(references)}))
        return
    report = validate_current(args.root, baseline_path, references_path, args.exceptions, args.toe_witness)
    print(json.dumps(report, ensure_ascii=False))
    require(not report["errors"], "El atlas difiere del baseline inmutable fuera de sus excepciones exactas")


if __name__ == "__main__":
    main()
