import copy
import hashlib
import unittest

from PIL import Image

from ValidateNoTraceCharacterBaseline import compare, frame_record


class CharacterBaselineTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
