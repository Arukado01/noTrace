"""Bindings de piezas para el mismo rig; las poses y la jerarquía proceden del generador compartido."""
import hashlib
import json
import math
import statistics
from pathlib import Path

from PIL import Image

import GenerateNoTraceCharactersRig as rig


BONES = {part: joints for part, joints in rig.PART_JOINTS.items() if len(joints) == 2}
ARMS = [side + "_" + part for side in ["left", "right"] for part in ["upper_arm", "forearm"]]
DIAMETERS = {"left_upper_arm": 7.904734603947315, "right_upper_arm": 11.077989664044843, "left_forearm": 6.9621955609520585, "right_forearm": 7.471503594726012,
             "left_thigh": 9.379, "right_thigh": 11.438, "left_shin": 10.073, "right_shin": 11.207, "left_boot": 5.696, "right_boot": 6.003}
POSTURE_CLIPS = {"LieDown", "StandUp", "Dive", "Fall", "Unconscious", "Death", "ReviveRise", "ProneIdle", "Crawl"}


def required(clip, stance):
    return stance == "Prone" or clip in POSTURE_CLIPS


def inclination(vector):
    return math.hypot(vector[0], vector[1]) / max(0.001, math.sqrt(sum(value * value for value in vector)))


def delta(points, first, second):
    return tuple(points[second][index] - points[first][index] for index in range(3))


def authored_direction(direction, axis):
    if math.hypot(axis[0], axis[1]) < 1:
        return direction
    return rig.DIRECTIONS[(rig.DIRECTIONS.index(direction) + round(math.atan2(axis[1], axis[0]) / (math.pi / 4))) % 8]


def canonical_direction(points, direction):
    frame = rig.body_frame(points)
    # De pie mira contra el dorso; al tumbarse el eje longitudinal indica el frente de la postura.
    axis = frame["spine"] if abs(frame["spine"][2]) <= abs(frame["dorsal"][2]) else tuple(-value for value in frame["dorsal"])
    return authored_direction(direction, tuple(value * 10 for value in axis))


def canonical_body_direction(points, direction):
    frame = rig.body_frame(points)
    angle = math.pi/4-rig.DIRECTIONS.index(direction)*math.pi/4
    camera = (math.cos(angle)+math.sin(angle), math.sin(angle)-math.cos(angle), 1.35)
    lateral = sum(camera[index]*frame["left"][index] for index in range(3))
    frontal = -sum(camera[index]*frame["dorsal"][index] for index in range(3))
    # La vista corporal sigue la superficie visible; la cabeza conserva su orientación anatómica independiente.
    return rig.DIRECTIONS[round(math.atan2(-lateral, frontal)/(math.pi/4)) % len(rig.DIRECTIONS)]


def transverse_diameter(image, first, second):
    dx, dy = second[0] - first[0], second[1] - first[1]
    size = math.hypot(dx, dy)
    nx, ny = -dy / size, dx / size
    alpha = image.getchannel("A")
    spans = []
    for fraction in [.3, .4, .5, .6, .7]:
        cx, cy = first[0] + dx * fraction, first[1] + dy * fraction
        samples = []
        for distance in range(-max(image.size), max(image.size) + 1):
            x, y = round(cx + nx * distance), round(cy + ny * distance)
            if 0 <= x < image.width and 0 <= y < image.height and alpha.getpixel((x, y)) >= 96:
                samples.append(distance)
        if samples:
            spans.append(samples[-1] - samples[0] + 1)
    return statistics.median(spans) if spans else min(image.size)


class ProneBindings:
    def __init__(self, root, canonical=False):
        self.root = Path(root)
        self.workspace = self.root.resolve().parents[2]
        self.banks, self.auxiliary, self.original, self.records = {}, [], {}, []
        self.canonical = None
        if canonical:
            self.canonical = {direction: rig.read_registered_parts(self.root, direction) for direction in rig.DIRECTIONS}
            self.original = {direction: {role: view["image"] for role, view in views.items()} for direction, views in self.canonical.items()}
            return
        manifest = json.loads((self.root / "Manifests/prone-rig-pieces.registration.json").read_text(encoding="utf-8"))
        sources = {}

        def read_source(path, expected):
            file = self.workspace / path
            if hashlib.sha256(file.read_bytes()).hexdigest() != expected:
                raise ValueError("Cambió una fuente tumbada registrada: " + path)
            sources[path] = Image.open(file).convert("RGBA")

        for bank in manifest["banks"]:
            read_source(bank["file"], bank["sourceSha256"])
            if list(sources[bank["file"]].size) != bank["sourceSize"]:
                raise ValueError("Dimensiones de fuente tumbada incorrectas")
            views = {}
            for entry in bank["parts"]:
                view = self.read_view(sources[bank["file"]], entry, entry["rgbaSha256"])
                view["id"] = bank["direction"] + "/" + entry["piece"]
                views[entry["piece"]] = view
            self.banks[bank["direction"]] = views
        if set(self.banks) != set(rig.DIRECTIONS):
            raise ValueError("El banco tumbado requiere las ocho direcciones")
        for file, digest in manifest["auxiliarySources"].items():
            read_source(file, digest)
        for entry in manifest["auxiliaryParts"]:
            view = self.read_view(sources[entry["file"]], entry, entry["sourceRgbaSha256"])
            if view["viewKind"] == "axial" and view["role"] in ["left_forearm", "right_forearm"]:
                view["uniformScale"] = DIAMETERS[view["role"]] / view["naturalDiameterPixels"]["source"]
            self.auxiliary.append(view)
        for direction in rig.DIRECTIONS:
            pieces = rig.read_parts(self.root, direction)[0]
            self.original[direction] = pieces

    def resolve_pose(self, points, direction, clip, phase, stance="Standing"):
        if self.canonical is None:
            return dict(points)
        # Exportar y dibujar comparten la misma pose resuelta, sin recolocar piezas durante el dibujo.
        return rig.resolve_registered(self.views_for(points, direction), points, direction, clip, phase, stance)

    def views_for(self, points, direction):
        if self.canonical is None:
            raise ValueError("La selección por superficie necesita el registro canónico")
        views = dict(self.canonical[canonical_body_direction(points, direction)])
        views["head"] = self.canonical[canonical_direction(points, direction)]["head"]
        return views

    @staticmethod
    def read_view(source, entry, digest):
        view = dict(entry)
        x, y, width, height = view["sourceRect"]
        if not (0 <= x < x + width <= source.width and 0 <= y < y + height <= source.height):
            raise ValueError("Recorte tumbado fuera de fuente")
        image = source.crop((x, y, x + width, y + height))
        if hashlib.sha256(image.tobytes()).hexdigest() != digest:
            raise ValueError("RGBA de pieza tumbada no coincide con registro")
        for key in ["proximalPx", "distalPx", "anchorPx", "centrePx"]:
            point = view.get(key)
            if point is not None and (len(point) != 2 or not all(math.isfinite(v) for v in point) or not (0 <= point[0] < width and 0 <= point[1] < height)):
                raise ValueError("Pivote tumbado no válido: " + str(view.get("piece", view.get("id"))))
        if not math.isfinite(view["uniformScale"]) or view["uniformScale"] <= 0:
            raise ValueError("Escala tumbada no positiva")
        if view.get("distalPx") is not None:
            if math.dist(view["proximalPx"], view["distalPx"]) < 1:
                raise ValueError("Eje tumbado sin longitud")
            view["diameterSource"] = transverse_diameter(image, view["proximalPx"], view["distalPx"])
        view["image"] = image
        limit = view.get("mipMaxDimension", max(image.size))
        factor = min(1.0, limit / max(image.size))
        view["mip"] = image.resize((max(1, round(width * factor)), max(1, round(height * factor))), Image.Resampling.BOX) if factor < 1 else image
        return view

    @staticmethod
    def place(canvas, view, pivot, target, factor, angle=0):
        return rig.uniform_piece(canvas, view["image"], pivot, target, factor, angle, view["mip"])

    def bone(self, canvas, view, first, second):
        proximal, distal = view["proximalPx"], view["distalPx"]
        return rig.uniform_bone(canvas, view["image"], proximal, distal, first, second, view["mip"])["scale"]

    def volume_bone(self, canvas, view, first, second, diameter):
        proximal, distal = view["proximalPx"], view["distalPx"]
        return rig.uniform_bone(canvas, view["image"], proximal, distal, first, second, view["mip"], view["diameterSource"], diameter)

    @staticmethod
    def torso_diameter(points, direction):
        axis = delta(points, "pelvis", "neck")
        forward = (axis[0], axis[1], 0)
        horizontal = max(.001, math.hypot(*forward[:2]))
        lateral = (-forward[1]/horizontal*18, forward[0]/horizontal*18, 0)
        a, b = rig.project((0,0,0), direction), rig.project(axis, direction)
        size = max(.001, math.dist(a,b))
        normal = (-(b[1]-a[1])/size, (b[0]-a[0])/size)
        side = rig.project(lateral, direction)
        return abs((side[0]-a[0])*normal[0]+(side[1]-a[1])*normal[1]) + 4*abs(normal[1])

    def legacy_only(self, canvas, parts, names, points, direction):
        selected = {name: parts[name] if name in names else Image.new("RGBA", (1, 1)) for name in rig.NAMES}
        canvas.alpha_composite(rig.render(selected, points, direction))

    def arm(self, canvas, role, projected, points, direction, bank, clip, phase):
        first, second = BONES[role]
        a, b = projected[first], projected[second]
        length = math.dist(a, b)
        candidates = [bank[role]]
        # Las superficies de tela sin insignias comparten vistas de proyección, conservando sus proporciones autoradas.
        for name, view in self.banks["E"].items():
            if name.replace("_idle", "") == role and view not in candidates:
                candidates.append(view)
        candidates += [view for view in self.auxiliary if view["role"] == role]
        valid = []
        for view in candidates:
            if view["viewKind"] == "axial":
                centre = view["centrePx"]
                axis = view["diameterSamplingAxisPx"]
                dx, dy = axis[1][0] - axis[0][0], axis[1][1] - axis[0][1]
                denominator = math.hypot(dx, dy)
                half = length / (2 * view["uniformScale"])
                covered = True
                for sign in [-1, 1]:
                    x, y = round(centre[0] + sign * half * dx / denominator), round(centre[1] + sign * half * dy / denominator)
                    covered = covered and 0 <= x < view["image"].width and 0 <= y < view["image"].height and view["image"].getpixel((x, y))[3] >= 96
                if covered:
                    valid.append((.4, view))
            else:
                reference = math.dist(view["proximalPx"], view["distalPx"]) * view["uniformScale"]
                ratio = length / reference
                diameter = view["diameterSource"] * length / math.dist(view["proximalPx"], view["distalPx"])
                if .75 <= ratio <= 1.25 and .65 <= diameter / DIAMETERS[role] <= 1.4:
                    valid.append((abs(math.log(ratio)) + (.03 if view is not bank[role] else 0), view))
        if valid:
            view = min(valid, key=lambda item: item[0])[1]
        else:
            view = min((item for item in candidates if item["viewKind"] != "axial"), key=lambda item: abs(math.log(length / (math.dist(item["proximalPx"], item["distalPx"]) * item["uniformScale"]))))
        if view["viewKind"] == "axial":
            axis = view["diameterSamplingAxisPx"]
            angle = math.atan2(b[1] - a[1], b[0] - a[0]) - math.atan2(axis[1][1] - axis[0][1], axis[1][0] - axis[0][0]) if length >= 1 else 0
            self.place(canvas, view, view["centrePx"], ((a[0]+b[0])/2, (a[1]+b[1])/2), view["uniformScale"], angle)
        else:
            self.bone(canvas, view, a, b)
        self.records.append({"clip": clip, "phase": phase, "direction": direction, "role": role, "view": view["id"], "moderateViewAvailable": bool(valid)})

    def render(self, points, direction, clip, phase, stance="Standing", layer="full"):
        if self.canonical is not None:
            # La vista procede del mismo registro; cámara, pose y jerarquía siguen siendo las compartidas.
            return rig.render_registered(self.views_for(points, direction), points, direction, layer)
        original = self.original[direction]
        if not required(clip, stance) or layer in ["cqcBody", "cqcArms"]:
            return rig.render(original, points, direction, layer)
        if clip == "ReviveRise" and phase == 7 and stance != "Prone":
            return rig.render(original, points, direction, layer)
        axis = delta(points, "pelvis", "neck")
        pitch = inclination(axis)
        if pitch < .1 and clip in ["LieDown", "StandUp", "Dive", "Fall", "Death", "ReviveRise"] and phase in [0, 7]:
            return rig.render(original, points, direction, layer)
        effective = authored_direction(direction, axis)
        bank = self.banks[effective]
        projected = {name: rig.project(point, direction) for name, point in points.items()}
        image = Image.new("RGBA", (rig.CELL, rig.CELL))
        sides = sorted(["right", "left"], key=lambda side: sum(rig.local(points[side + "_hip"], direction)[:2]))
        prone_torso, prone_pack, prone_pelvis = pitch >= .92, pitch >= .94, pitch >= .8
        supine = rig.body_frame(points)["dorsal"][2] < 0

        def obj(role):
            view = bank[role]
            target = tuple((projected["pelvis"][i]+projected["neck"][i])/2 for i in range(2)) if view.get("anchorJoint") == "torso_midpoint" else projected[view.get("anchorJoint", role.replace("_hand", "_wrist"))]
            self.place(image, view, view.get("anchorPx", view["proximalPx"]), target, view["uniformScale"])

        def pack():
            if prone_pack:
                obj("backpack")
            else:
                self.legacy_only(image, original, {"backpack"}, points, direction)

        if layer != "lower" and (supine or not prone_pack):
            pack()
        if layer != "upper":
            for side in sides:
                for part in ["thigh", "shin", "boot"]:
                    role = side + "_" + part
                    first, second = BONES[role]
                    view = bank[role]
                    length = math.dist(projected[first], projected[second])
                    source_length = math.dist(view["proximalPx"], view["distalPx"])
                    ratio = length / (source_length * view["uniformScale"])
                    if inclination(delta(points, first, second)) >= .65 and ratio >= .7:
                        diameter = view["diameterSource"] * length / source_length
                        if .65 <= diameter / DIAMETERS[role] <= 1.25:
                            self.bone(image, view, projected[first], projected[second])
                        else:
                            alternate = self.banks["E"][role]
                            candidate_diameter = alternate["diameterSource"] * length / math.dist(alternate["proximalPx"], alternate["distalPx"])
                            if not role.endswith("boot") and .65 <= candidate_diameter / DIAMETERS[role] <= 1.25:
                                self.bone(image, alternate, projected[first], projected[second])
                            else:
                                self.partial_uniform(image, original[role], role, projected)
                    else:
                        self.legacy_only(image, original, {role}, points, direction)
            if prone_pelvis:
                obj("pelvis")
            else:
                self.legacy_only(image, original, {"pelvis"}, points, direction)
        if layer == "lower":
            return image
        for role in ARMS:
            self.arm(image, role, projected, points, direction, bank, clip, phase)
        if prone_torso:
            view = bank["torso"]
            if view["viewKind"] == "object":
                obj("torso")
            else:
                if effective in ["NE", "NW"]:
                    self.volume_bone(image, view, projected["pelvis"], projected["neck"], self.torso_diameter(points, direction))
                else:
                    self.bone(image, view, projected["pelvis"], projected["neck"])
        else:
            self.legacy_only(image, original, {"torso"}, points, direction)
        if prone_pack and not supine:
            pack()
        for role in ["left_hand", "right_hand"]:
            pinch = next((view for view in self.auxiliary if view["viewKind"] == "hand"), None)
            if role == "left_hand" and effective == "E" and pinch and (clip.startswith("Suppressor") or clip == "Reload" and phase in range(1, 7)):
                wrist, tip = pinch["proximalPx"], pinch["pinchPx"]
                self.place(image, pinch, wrist, projected["left_wrist"], pinch["uniformScale"], -math.atan2(tip[1]-wrist[1], tip[0]-wrist[0]))
            else:
                obj(role)
        view = bank["head"]
        if pitch >= .8:
            if view["viewKind"] == "object":
                obj("head")
            else:
                # La cabeza E conserva los pivotes de nuca y cráneo aprobados en su proyección tendida.
                self.bone(image, view, projected["neck"], projected["head"])
        else:
            head = original["head"]
            volume = {"image": head, "mip": head}
            self.place(image, volume, (head.width*.5, head.height*.5), projected["head"], (14+12*pitch)/max(head.size))
        return image

    def partial_uniform(self, canvas, piece, role, points):
        first, second = BONES[role]
        proximal = (piece.width * (.6 if role.endswith("boot") else .5), piece.height * (.14 if role.endswith("boot") else .12))
        distal = (piece.width * (.4 if role.endswith("boot") else .5), piece.height * (.8 if role.endswith("boot") else .88))
        view = {"image": piece, "mip": piece, "proximalPx": proximal, "distalPx": distal}
        view["diameterSource"] = transverse_diameter(piece, proximal, distal)
        # Una vista corta conserva el grosor del miembro; igualar sólo su eje volvería a inflar la extremidad.
        self.volume_bone(canvas, view, points[first], points[second], DIAMETERS[role])
