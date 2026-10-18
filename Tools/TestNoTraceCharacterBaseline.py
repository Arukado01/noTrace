import copy
import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

from ValidateNoTraceCharacterBaseline import REFERENCES_NAME, REGISTRATION_FILES, compare, create_references, file_sha, frame_record, main, read_json, registration_contracts


class CharacterBaselineTests(unittest.TestCase):
    def temporary_directory(self):
        workspace = Path(__file__).resolve().parents[1]
        directory = (workspace / "build/qa-character-baseline-tests").resolve()
        self.assertTrue(directory.is_relative_to(workspace.resolve()))
        directory.mkdir(parents=True, exist_ok=True)
        return tempfile.TemporaryDirectory(dir=directory)

    def fixture(self, position=(1, 2)):
        pixels = Image.new("RGBA", (3, 2), (24, 46, 68, 0))
        pixels.putpixel((1, 0), (80, 100, 120, 160))
        page = Image.new("RGBA", (16, 16))
        page.paste(pixels, position)
        canvas = Image.new("RGBA", (192, 192))
        canvas.paste(pixels, (82, 96))
        entry = {
            "index": 42, "clip": "Idle", "stance": "Standing", "direction": "E", "phase": 0,
            "groundAnchor": [96.0, 144.0], "pelvis": [96.0, 109.0], "muzzle": [-1.0, -1.0],
            "grip": [-1.0, -1.0], "carryGrip": [85.0, 102.0], "supportWidth": 24.0,
            "joints": {"pelvis": [0.0, 0.0, 35.0]}, "page": 0, "rect": [0, 0, 192, 192],
            "pixelSha256": hashlib.sha256(canvas.tobytes()).hexdigest(),
            "art": {"full": {"page": 0, "rect": [*position, 3, 2], "offset": [82, 96]}},
        }
        return entry, [page]

    def baseline(self, record):
        return {"definition": {}, "headerSha256": "header", "sourceRegistrations": [], "originalSources": [], "weaponFiles": [], "frames": [record]}

    def contracts_fixture(self, root):
        entry, pages = self.fixture()
        baseline = self.baseline(frame_record(entry, pages, 192))
        (root / "Manifests").mkdir()
        for name in REGISTRATION_FILES:
            (root / name).write_bytes('{\n  "source": "Élite",\n  "nested": {"value": 1}\n}\n'.encode("utf-8"))
        baseline["sourceRegistrations"] = [{"file": name, "sha256": file_sha(root / name)} for name in REGISTRATION_FILES]
        return baseline, create_references(root, baseline)

    def test_repacking_preserves_canonical_rgba_and_semantics(self):
        before, old_pages = self.fixture((1, 2))
        after, new_pages = self.fixture((9, 11))
        after["page"], after["rect"] = 23, [192, 384, 192, 192]
        self.assertEqual(frame_record(before, old_pages, 192), frame_record(after, new_pages, 192))

    def test_transparent_rgb_tampering_is_detected(self):
        entry, pages = self.fixture()
        pages[0].putpixel((1, 2), (25, 46, 68, 0))
        with self.assertRaisesRegex(ValueError, "RGBA real"):
            frame_record(entry, pages, 192)

    def test_unmodified_index_anchor_and_joint_are_exact(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        for field in ["groundAnchor", "joints"]:
            changed = copy.deepcopy(entry)
            if field == "groundAnchor":
                changed[field][0] += 0.001
            else:
                changed[field]["pelvis"][2] += 0.001
            errors = compare(before, self.baseline(frame_record(changed, pages, 192)))
            self.assertTrue(any("índice 42" in error for error in errors), field)

    def test_layer_change_is_diagnosed_by_index_and_layer(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        after = copy.deepcopy(before)
        after["frames"][0]["rgbaSha256"]["full"] = "different"
        self.assertIn("RGBA distinto: índice 42, capa full", compare(before, after))

    def test_sources_and_index_set_cannot_change(self):
        entry, pages = self.fixture()
        before = self.baseline(frame_record(entry, pages, 192))
        after = copy.deepcopy(before)
        after["originalSources"] = [{"file": "source.png", "sha256": "new"}]
        after["frames"][0]["index"] = 43
        self.assertIn("Contrato o fuente distintos: originalSources", compare(before, after))
        self.assertIn("El conjunto de índices cambió", compare(before, after))

    def test_lf_crlf_contracts_are_equivalent(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            before, references = self.contracts_fixture(root)
            for name in REGISTRATION_FILES:
                path = root / name
                path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            after = copy.deepcopy(before)
            after["sourceRegistrations"] = [{"file": name, "sha256": file_sha(root / name)} for name in REGISTRATION_FILES]
            self.assertNotEqual(before["sourceRegistrations"], after["sourceRegistrations"])
            baseline_path, references_path = root / "baseline.json", root / "references.json"
            for newline in [b"\n", b"\r\n"]:
                baseline_path.write_bytes(json.dumps(before, ensure_ascii=False, indent=2).encode("utf-8").replace(b"\n", newline))
                references_path.write_bytes(json.dumps(references, ensure_ascii=False, indent=2).encode("utf-8").replace(b"\n", newline))
                self.assertEqual([], compare(read_json(baseline_path), after, read_json(references_path), registration_contracts(root)))

    def test_contract_value_change_is_rejected(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, references = self.contracts_fixture(root)
            path = root / REGISTRATION_FILES[0]
            path.write_bytes(path.read_bytes().replace(b'"value": 1', b'"value": 2'))
            errors = compare(baseline, baseline, references, registration_contracts(root))
            self.assertIn("Contrato JSON distinto: " + REGISTRATION_FILES[0], errors)

    def test_duplicate_json_keys_are_rejected_at_every_depth(self):
        with self.temporary_directory() as directory:
            path = Path(directory) / "contract.json"
            for payload in ['{"value":1,"value":1}', '{"nested":{"value":1,"value":2}}']:
                path.write_text(payload, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Clave JSON duplicada"):
                    read_json(path)

    def test_sidecar_for_another_baseline_is_rejected(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, references = self.contracts_fixture(root)
            references["baselineCanonicalSha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "otro baseline"):
                compare(baseline, baseline, references, registration_contracts(root))

    def test_reference_creation_requires_original_raw_sha(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, _ = self.contracts_fixture(root)
            path = root / REGISTRATION_FILES[0]
            path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
            with self.assertRaisesRegex(ValueError, "SHA original distinto"):
                create_references(root, baseline)

    def test_check_does_not_create_a_missing_sidecar(self):
        with self.temporary_directory() as directory:
            root = Path(directory)
            baseline, _ = self.contracts_fixture(root)
            path = root / "baseline.json"
            path.write_text(json.dumps(baseline), encoding="utf-8")
            with patch("sys.argv", ["baseline", "check", "--root", str(root), "--baseline", str(path)]):
                with self.assertRaises(FileNotFoundError):
                    main()
            self.assertFalse((root / "Manifests" / REFERENCES_NAME).exists())


if __name__ == "__main__":
    unittest.main()
