"""Empaqueta las fuentes originales del entorno sin alterar su identidad visual."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "Art/NoTrace/Environment/Sources"
MATERIALS_SOURCE = SOURCES / "MilitaryMaterials16-v1.png"
FURNITURE = (
    "FieldDesk", "FieldDeskOverturned", "LockerClosed", "LockerOpen",
    "AmmunitionCrates", "LogisticsCrate", "WasteBin", "ServerRack",
    "SecurityConsole", "LeverOff", "LeverOn", "PushButtons",
    "SecurityDoorClosed", "SecurityDoorOpen", "MedicalCabinet", "FreightLift",
    "CameraMast", "ExplosiveKit", "FieldChair", "MilitaryBench",
    "SupplyBarrels", "SecurityGlass", "VentilationUnit", "SafetyLamp",
)
MATERIALS = (
    "Concrete", "Steel", "Grate", "Industrial", "Soil", "ConcreteWall",
    "SteelWall", "Glass", "SecurityDoor", "Safety", "Brick", "StairTread",
    "Antistatic", "DiamondPlate", "Ventilation", "Fabric",
)
FURNITURE_RECTS = (
    (30, 100, 425, 470), (430, 100, 820, 470), (835, 80, 1130, 470), (1160, 70, 1520, 470),
    (25, 520, 415, 940), (420, 520, 800, 940), (810, 520, 1180, 940), (1200, 520, 1525, 940),
    (20, 100, 435, 480), (450, 180, 800, 480), (800, 100, 1140, 480), (1150, 100, 1520, 480),
    (20, 520, 415, 940), (415, 520, 785, 940), (785, 520, 1130, 940), (1130, 520, 1535, 940),
    (50, 40, 300, 510), (350, 90, 790, 505), (790, 90, 1120, 505), (1125, 150, 1536, 510),
    (15, 510, 360, 970), (395, 510, 780, 970), (790, 550, 1205, 965), (1210, 530, 1515, 970),
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cells(image: Image.Image):
    for row in range(4):
        for column in range(4):
            bounds = (
                round(column * image.width / 4), round(row * image.height / 4),
                round((column + 1) * image.width / 4), round((row + 1) * image.height / 4),
            )
            yield image.crop(bounds), bounds


def furniture(output: Path) -> dict:
    sources = [SOURCES / "MilitaryFurnitureA-v2.png", SOURCES / "MilitaryFurnitureB-v2.png", SOURCES / "MilitaryDevicesC-v2.png"]
    images = [Image.open(source).convert("RGBA") for source in sources]
    atlas = Image.new("RGBA", (768, 1152))
    entries = []
    for index, source_bounds in enumerate(FURNITURE_RECTS):
        cell = images[index // 8].crop(source_bounds)
        mask = cell.getchannel("A").point(lambda alpha: 255 if alpha >= 220 else 0)
        bounds = mask.getbbox()
        if bounds is None:
            raise ValueError(f"Objeto vacío: {FURNITURE[index]}")
        left, top, right, bottom = bounds
        bounds = (max(0, left - 6), max(0, top - 6), min(cell.width, right + 6), min(cell.height, bottom + 6))
        sprite = cell.crop(bounds)
        scale = min(172 / sprite.width, 160 / sprite.height)
        size = (max(1, round(sprite.width * scale)), max(1, round(sprite.height * scale)))
        sprite = sprite.resize(size, Image.Resampling.NEAREST)
        offset = ((192 - size[0]) // 2, 176 - size[1])
        origin = ((index % 4) * 192, (index // 4) * 192)
        atlas.alpha_composite(sprite, (origin[0] + offset[0], origin[1] + offset[1]))
        entries.append({
            "name": FURNITURE[index], "index": index,
            "rect": [*origin, 192, 192], "source": sources[index // 8].name, "sourceCell": source_bounds, "sourceCrop": bounds,
            "artBounds": [*offset, *size], "anchor": [96, 176 - round(size[1] * 0.08)],
            "rgbaSha256": hashlib.sha256(sprite.tobytes()).hexdigest(),
        })
    atlas.save(output / "MilitaryFurniture.png")
    return {"sources": [{"path": str(source.relative_to(ROOT)), "sha256": digest(source)} for source in sources], "frames": entries}


def materials(output: Path, source: Path = MATERIALS_SOURCE) -> dict:
    source = source.resolve()
    image = Image.open(source).convert("RGB")
    atlas = Image.new("RGB", (512, 512))
    entries = []
    for index, (cell, source_bounds) in enumerate(cells(image)):
        cell = cell.crop((2, 2, cell.width - 2, cell.height - 2)).resize((128, 128), Image.Resampling.NEAREST)
        origin = ((index % 4) * 128, (index // 4) * 128)
        atlas.paste(cell, origin)
        entries.append({"name": MATERIALS[index], "index": index, "rect": [*origin, 128, 128], "sourceCell": source_bounds})
    atlas.save(output / "MilitaryMaterials.png")
    source_path = source.relative_to(ROOT) if source.is_relative_to(ROOT) else source
    return {"source": str(source_path), "sourceSha256": digest(source), "frames": entries}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "Art/NoTrace/Environment/Packed")
    parser.add_argument("--materials-source", type=Path, default=MATERIALS_SOURCE, help="Fuente de 16 materiales; por defecto se conserva v1.")
    parser.add_argument("--materials-only", action="store_true", help="Actualiza sólo materiales y conserva el catálogo de muebles del manifiesto base.")
    parser.add_argument("--base-manifest", type=Path, help="Manifiesto base para --materials-only; por defecto usa el existente en --output.")
    args = parser.parse_args()
    if not args.materials_source.is_file():
        parser.error(f"No existe la fuente de materiales: {args.materials_source}")
    if args.materials_only:
        base = args.base_manifest or args.output / "MilitaryEnvironment.manifest.json"
        try:
            manifest = json.loads(base.read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            parser.error(f"No se pudo leer el manifiesto base {base}: {error}")
        if not isinstance(manifest, dict) or not isinstance(manifest.get("furniture"), dict) or not isinstance(manifest.get("outputs"), dict):
            parser.error("El manifiesto base debe conservar furniture y outputs.")
        frames = manifest["furniture"].get("frames")
        if not isinstance(frames, list) or len(frames) != len(FURNITURE) or not manifest["outputs"].get("MilitaryFurniture.png"):
            parser.error("El manifiesto base debe contener los 24 muebles y su hash de salida.")
    else:
        if args.base_manifest is not None:
            parser.error("--base-manifest requiere --materials-only.")
        manifest = {
            "schema": 1, "origin": "Fuentes originales generadas para NO TRACE con imagegen integrado.",
            "processing": "Partición de celdas, recorte de margen y reducción nearest; se conserva el alfa generado.",
        }
    args.output.mkdir(parents=True, exist_ok=True)
    if not args.materials_only:
        manifest["furniture"] = furniture(args.output)
    # En modo aislado se conserva el manifiesto base sin leer ni reconstruir el PNG de muebles.
    manifest["materials"] = materials(args.output, args.materials_source)
    outputs = manifest.setdefault("outputs", {})
    if not args.materials_only:
        outputs["MilitaryFurniture.png"] = digest(args.output / "MilitaryFurniture.png")
    outputs["MilitaryMaterials.png"] = digest(args.output / "MilitaryMaterials.png")
    (args.output / "MilitaryEnvironment.manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    result = "16 materiales; muebles conservados" if args.materials_only else f"{len(FURNITURE)} objetos y 16 materiales"
    print(f"{result}: {args.output}")


if __name__ == "__main__":
    main()
