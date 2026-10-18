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
JOINT_PARENTS = {"pelvis": None, "waist": "pelvis", "neck": "pelvis", "head": "neck", "backpack": "neck", "weapon_grip": "right_wrist", "weapon_muzzle": "weapon_grip"}
JOINT_PARENTS.update({"rig_frame_origin": "pelvis", **{"rig_frame_"+name: "rig_frame_origin" for name in ["spine", "left", "dorsal"]}})
PART_JOINTS = {"head": ("neck", "head"), "torso": ("pelvis", "neck"), "pelvis": ("pelvis",), "backpack": ("backpack",)}
for side in ["left", "right"]:
    JOINT_PARENTS.update({side + "_shoulder": "neck", side + "_elbow": side + "_shoulder", side + "_wrist": side + "_elbow", side + "_hip": "pelvis",
                          side + "_knee": side + "_hip", side + "_ankle": side + "_knee", side + "_toe": side + "_ankle"})
    PART_JOINTS.update({side + "_upper_arm": (side + "_shoulder", side + "_elbow"), side + "_forearm": (side + "_elbow", side + "_wrist"), side + "_hand": (side + "_wrist",),
                       side + "_thigh": (side + "_hip", side + "_knee"), side + "_shin": (side + "_knee", side + "_ankle"), side + "_boot": (side + "_ankle", side + "_toe")})


def local(point, direction="S"):
    forward, side, height = point
    angle = math.pi / 4.0 - DIRECTIONS.index(direction) * math.pi / 4.0
    return (forward * math.cos(angle) + side * math.sin(angle), forward * math.sin(angle) - side * math.cos(angle), height)


def project(point, direction="S"):
    x, y, height = local(point, direction)
    return (ANCHOR[0] + (x - y) * 1.35, ANCHOR[1] + (x + y) * 0.675 - height)


def retarget_joint(points, joint, target):
    displacement = tuple(target[index] - points[joint][index] for index in range(3))
    descendants = {joint}
    for _ in range(len(JOINT_PARENTS)):
        descendants.update(name for name, parent in JOINT_PARENTS.items() if parent in descendants)
    # Las coordenadas resueltas son globales; trasladar un padre conserva los offsets locales de su cadena.
    for name in descendants.intersection(points):
        points[name] = tuple(points[name][index] + displacement[index] for index in range(3))
    return points


def body_frame(points):
    if all("rig_frame_"+name in points for name in ["origin", "spine", "left", "dorsal"]):
        origin = points["rig_frame_origin"]
        return {name: unit(add(points["rig_frame_"+name], scale(origin, -1))) for name in ["spine", "left", "dorsal"]}
    spine = unit(add(points["neck"], scale(points["pelvis"], -1.0)))
    left = unit(add(points["left_shoulder"], scale(points["right_shoulder"], -1.0)))
    dorsal = unit((spine[1]*left[2]-spine[2]*left[1], spine[2]*left[0]-spine[0]*left[2], spine[0]*left[1]-spine[1]*left[0]))
    if length(dorsal) < .5:
        raise ValueError("El torso y los hombros no definen una orientación anatómica válida")
    return {"spine": spine, "left": left, "dorsal": dorsal}


def twisted_body_frame(pelvis, neck, roll):
    spine = unit(add(neck, scale(pelvis, -1.0)))
    horizontal_left = unit((-spine[1], spine[0], 0.0))
    upright_dorsal = (spine[1]*horizontal_left[2]-spine[2]*horizontal_left[1], spine[2]*horizontal_left[0]-spine[0]*horizontal_left[2], spine[0]*horizontal_left[1]-spine[1]*horizontal_left[0])
    left = add(scale(horizontal_left, math.cos(roll)), scale(upright_dorsal, math.sin(roll)))
    dorsal = add(scale(upright_dorsal, math.cos(roll)), scale(horizontal_left, -math.sin(roll)))
    return {"spine": spine, "left": left, "dorsal": dorsal}


def frame_coordinates(vector, frame):
    return tuple(dot(vector, frame[name]) for name in ["spine", "left", "dorsal"])


def frame_vector(coordinates, frame):
    return tuple(sum(coordinates[index]*frame[name][axis] for index, name in enumerate(["spine", "left", "dorsal"])) for axis in range(3))


def joint_support_basis(first, last):
    axis = unit(add(last, scale(first, -1.0)))
    upward = unit(add((0.0, 0.0, 1.0), scale(axis, -axis[2])))
    sideways = (axis[1]*upward[2]-axis[2]*upward[1], axis[2]*upward[0]-axis[0]*upward[2], axis[0]*upward[1]-axis[1]*upward[0])
    return upward, sideways


def joint_support_angle(first, last, middle):
    upward, sideways = joint_support_basis(first, last)
    normal = add(middle, scale(add(first, last), -.5))
    return math.atan2(dot(normal, sideways), dot(normal, upward))


def joint_from_support_angle(first, last, angle, bone, ground=0.0):
    upward, sideways = joint_support_basis(first, last)
    midpoint = scale(add(first, last), .5)
    radius = math.sqrt(bone*bone-math.dist(first, last)**2*.25)
    lower_cosine = (ground-midpoint[2])/(radius*upward[2])
    if lower_cosine > 1:
        raise ValueError("La cadena IK no alcanza un apoyo sobre el suelo")
    maximum_angle = math.acos(max(-1.0, lower_cosine))
    angle = max(-maximum_angle, min(maximum_angle, angle))
    return add(midpoint, scale(add(scale(upward, math.cos(angle)), scale(sideways, math.sin(angle))), radius))


def grounded_joint(first, last, bend, bone, ground=0.0):
    candidate = elbow(first, last, bend, bone)
    if candidate[2] >= ground-1e-8:
        return candidate
    upward, sideways = joint_support_basis(first, last)
    midpoint = scale(add(first, last), .5)
    radius = math.sqrt(bone*bone-math.dist(first, last)**2*.25)
    cosine = (ground-midpoint[2])/(radius*upward[2])
    if not -1.0 <= cosine <= 1.0:
        raise ValueError("La cadena IK no alcanza un apoyo sobre el suelo")
    preferred = add(candidate, scale(midpoint, -1.0))
    sine = math.copysign(math.sqrt(max(0.0, 1.0-cosine*cosine)), dot(preferred, sideways))
    # La articulación conserva sus dos longitudes al alcanzar el límite físico del suelo.
    return add(midpoint, scale(add(scale(upward, cosine), scale(sideways, sine)), radius))


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


def load_canonical_registration(root):
    return json.loads((Path(root) / "Manifests/rig-pieces.canonical.json").read_text(encoding="utf-8"))


def read_registered_parts(root, direction):
    root = Path(root)
    registration = load_canonical_registration(root)
    entries = registration["directions"][direction]
    result = {}
    for role in NAMES:
        view = dict(entries[role])
        path = root / view["file"]
        if hashlib.sha256(path.read_bytes()).hexdigest() != view["sourceSha256"]:
            raise ValueError("Cambió una pieza canónica original: " + str(path))
        image = Image.open(path).convert("RGBA")
        factor = view["bindScale"]
        if list(image.size) != view["sourceSize"] or not math.isfinite(factor) or factor <= 0:
            raise ValueError("Registro canónico original no válido: " + str(path))
        view["image"] = image
        view["mip"] = image
        view["uniformScale"] = view["bindScale"] = factor
        view["role"], view["sourceDirection"] = role, direction
        result[role] = view
    return result


def render_registered_piece(canvas, view, target, angle=0.0, anchor="proximalPx"):
    # La escala uniforme de reposo queda bloqueada por recurso; la pose sólo traslada y gira la pieza completa.
    factor = view["bindScale"]
    if view.get("uniformScale", factor) != factor:
        raise ValueError("Una pose intenta cambiar el bind de reposo")
    return uniform_piece(canvas, view["image"], view[anchor], target, factor, angle, view["image"])


def registered_length(view):
    return math.dist(view["proximalPx"], view["distalPx"]) * view["bindScale"]


def _registered_rotate(point, angle):
    return (point[0]*math.cos(angle)-point[1]*math.sin(angle), point[0]*math.sin(angle)+point[1]*math.cos(angle))


def _registered_blend_axis(first, last, amount, outward=0.0):
    if amount <= 0:
        return first
    if amount >= 1:
        return last
    angle = math.atan2(last[1], last[0])-math.atan2(first[1], first[0])
    angle = math.atan2(math.sin(angle), math.cos(angle))
    # La tibia se pliega por su lado del cuerpo en vez de atravesar la pierna contraria.
    if abs(angle) > math.pi/2 and _registered_rotate(first, angle*.5)[0]*outward < -1e-8:
        angle -= math.copysign(math.tau, angle)
    return _registered_rotate(first, angle*amount)


def registered_source_point(view, point, target, angle=0.0, anchor="anchorPx"):
    return add(target, _registered_rotate(scale(add(point, scale(view[anchor], -1)), view["bindScale"]), angle))


def registered_head_anchor(view, points):
    spine = body_frame(points)["spine"]
    pitch = math.hypot(*spine[:2])/max(1e-9, length(spine))
    amount = max(0.0, min(1.0, (pitch-.45)/.5))
    amount = amount*amount*(3-2*amount)
    first = view.get("uprightCervicalPx", view["anchorPx"])
    last = view.get("pronePosteriorCervicalPx", first)
    return tuple(first[i]+(last[i]-first[i])*amount for i in range(2))


def _registered_chain(first, target, upper, lower, hint):
    delta = add(target, scale(first, -1))
    distance = length(delta)
    axis = unit(delta) if distance > 1e-9 else (0.0, 1.0)
    reachable = max(abs(upper-lower)+1e-6, min(upper+lower-1e-6, distance))
    target = add(first, scale(axis, reachable))
    along = (upper*upper-lower*lower+reachable*reachable)/(2*reachable)
    radius = math.sqrt(max(0.0, upper*upper-along*along))
    centre = add(first, scale(axis, along))
    normal = (-axis[1], axis[0])
    options = [add(centre, scale(normal, sign*radius)) for sign in [-1, 1]]
    return min(options, key=lambda point: math.dist(point, hint)), target


def _registered_lift(point, reference, direction):
    height = reference[2]
    horizontal = (point[0]-ANCHOR[0])/1.35
    depth = (point[1]-ANCHOR[1]+height)/.675
    x, y = (horizontal+depth)*.5, (depth-horizontal)*.5
    angle = math.pi/4-DIRECTIONS.index(direction)*math.pi/4
    return (x*math.cos(angle)+y*math.sin(angle), x*math.sin(angle)-y*math.cos(angle), height)


def resolve_registered(views, intentPoints, cameraDirection, clip="Idle", phase=0, stance="Standing"):
    # La intención de movimiento se resuelve en una única métrica de sockets; ninguna pose modifica el bind del recurso.
    intent = {name: tuple(point) for name, point in intentPoints.items()}
    if clip == "ReviveRise" and phase == 7 and stance != "Prone":
        return resolve_registered(views, intent, cameraDirection, "CrouchIdle" if stance == "Crouched" else "Idle", 0, stance)
    posture_transition = clip in ["CrouchEnter", "CrouchExit"]
    if posture_transition and phase in [0, 7]:
        destination = "CrouchIdle" if (clip == "CrouchEnter") == (phase == 7) else "Idle"
        return resolve_registered(views, skeleton(destination, 0), cameraDirection, destination, 0, "Standing")
    intent_frame = body_frame(intent)
    project_intent = {name: project(point, cameraDirection) for name, point in intent.items()}
    crouched = stance == "Crouched" or clip.startswith("Crouch")
    crouch_amount = float(crouched)
    if posture_transition:
        progress = max(0.0, min(1.0, phase/7))
        crouch_amount = progress*progress*(3-2*progress)
        if clip == "CrouchExit":
            crouch_amount = 1-crouch_amount
    horizontal = math.hypot(*(intent["neck"][i]-intent["pelvis"][i] for i in range(2)))
    prone = stance == "Prone" or horizontal/max(1e-9, math.dist(intent["neck"], intent["pelvis"])) > .7
    standing, kneeling = skeleton("Idle", 0), skeleton("CrouchIdle", 0)
    reference = {name: add(scale(standing[name], 1-crouch_amount), scale(kneeling[name], crouch_amount)) for name in standing}
    reference_pixels = {name: project(point, cameraDirection) for name, point in reference.items()}
    heading = DIRECTIONS.index(cameraDirection)*math.pi/4
    core_angle = math.radians(10+22*crouch_amount)*math.sin(heading)
    old_axis = add(project_intent["pelvis"], scale(project_intent["neck"], -1))
    rest_axis = add(reference_pixels["pelvis"], scale(reference_pixels["neck"], -1))
    delta_angle = math.atan2(old_axis[1], old_axis[0])-math.atan2(rest_axis[1], rest_axis[0])
    core_angle += math.atan2(math.sin(delta_angle), math.cos(delta_angle))
    if clip in ["Idle", "CrouchIdle"]:
        core_angle += (intent["neck"][2]-reference["neck"][2])*.0175
    pelvis_angle = 0.0
    pelvis = views["pelvis"]
    torso = views["torso"]
    if prone:
        centre, core_angle, pelvis_angle = registered_prone_core(views, intent, cameraDirection)
    else:
        centre = add(scale(registered_rest_pelvis(views, cameraDirection, False), 1-crouch_amount),
                     scale(registered_rest_pelvis(views, cameraDirection, True), crouch_amount))
        centre = add(centre, add(project_intent["pelvis"], scale(reference_pixels["pelvis"], -1)))
    projected = {"pelvis": centre}
    projected["waist"] = registered_source_point(pelvis, pelvis["waistPx"], centre, pelvis_angle)
    torso_offset = _registered_rotate(scale(add(torso["distalPx"], scale(torso["proximalPx"], -1)), torso["bindScale"]), core_angle)
    projected["neck"] = add(projected["waist"], scale(torso_offset, -1))
    simple_motion = posture_transition or clip in ["Idle", "Walk", "Run", "CrouchIdle", "CrouchWalk", "CrouchRun"]
    for side, sign in [("left", 1), ("right", -1)]:
        hip, shoulder = side+"_hip", side+"_shoulder"
        projected[hip] = registered_source_point(pelvis, pelvis[side+"HipPx"], centre, pelvis_angle)
        projected[shoulder] = registered_source_point(torso, torso[side+"ShoulderPx"], projected["neck"], core_angle, "proximalPx")
        thigh, shin = registered_length(views[side+"_thigh"]), registered_length(views[side+"_shin"])
        standing_knee, standing_ankle = registered_rest_leg(views, cameraDirection, side, False, projected[hip])
        kneeling_knee, kneeling_ankle = registered_rest_leg(views, cameraDirection, side, True, projected[hip])
        first_axis = _registered_blend_axis(add(standing_knee, scale(projected[hip], -1)), add(kneeling_knee, scale(projected[hip], -1)), crouch_amount)
        last_axis = _registered_blend_axis(add(standing_ankle, scale(standing_knee, -1)), add(kneeling_ankle, scale(kneeling_knee, -1)),
                                           crouch_amount, projected[hip][0]-centre[0] if posture_transition else 0)
        rest_knee = add(projected[hip], first_axis)
        rest_ankle = add(rest_knee, last_axis)
        ankle = side+"_ankle"
        if simple_motion:
            target = add(rest_ankle, add(project_intent[ankle], scale(reference_pixels[ankle], -1)))
            hint = rest_knee
        else:
            target = add(projected[hip], add(project_intent[ankle], scale(project_intent[hip], -1)))
            hint = add(projected[hip], add(project_intent[side+"_knee"], scale(project_intent[hip], -1)))
        projected[side+"_knee"], projected[ankle] = _registered_chain(projected[hip], target, thigh, shin, hint)
        upper, fore = registered_length(views[side+"_upper_arm"]), registered_length(views[side+"_forearm"])
        if prone:
            target, hint = registered_prone_arm(views, intent, cameraDirection, side, projected[shoulder])
        elif simple_motion:
            elbow = add(projected[shoulder], (sign*.6, math.sqrt(max(0, upper*upper-.36))))
            wrist = add(elbow, (sign*.3, math.sqrt(max(0, fore*fore-.09))))
            forward = add(project((5, 0, 0), cameraDirection), scale(ANCHOR, -1))
            ready = add(projected["neck"], (forward[0]+sign*2, 16))
            target = add(scale(wrist, 1-crouch_amount), scale(ready, crouch_amount))
            target = add(target, add(project_intent[side+"_wrist"], scale(reference_pixels[side+"_wrist"], -1)))
            hint = add(scale(elbow, 1-crouch_amount), scale(add(projected[shoulder], (sign*3, upper)), crouch_amount))
        else:
            target = add(projected[shoulder], add(project_intent[side+"_wrist"], scale(project_intent[shoulder], -1)))
            hint = add(projected[shoulder], add(project_intent[side+"_elbow"], scale(project_intent[shoulder], -1)))
        projected[side+"_elbow"], projected[side+"_wrist"] = _registered_chain(projected[shoulder], target, upper, fore, hint)
        boot = views[side+"_boot"]
        old_toe = add(project_intent[side+"_toe"], scale(project_intent[ankle], -1))
        rest_toe = add(reference_pixels[side+"_toe"], scale(reference_pixels[ankle], -1))
        boot_angle = math.atan2(old_toe[1], old_toe[0])-math.atan2(rest_toe[1], rest_toe[0])
        projected[side+"_toe"] = registered_source_point(boot, boot["distalPx"], projected[ankle], boot_angle)
    if simple_motion and crouch_amount < 1:
        soles = [projected[s+"_ankle"][1]+(views[s+"_boot"]["image"].getbbox()[3]-views[s+"_boot"]["anchorPx"][1])*views[s+"_boot"]["bindScale"] for s in ["left", "right"]]
        ground = (144-max(soles))*(1-crouch_amount)
        projected = {name: add(point, (0, ground)) for name, point in projected.items()}
    final = {name: _registered_lift(point, intent.get(name, intent["pelvis"]), cameraDirection) for name, point in projected.items()}
    # El marco FK conserva la orientación de intención sin deducirla de sockets pintados en distintas vistas.
    final["rig_frame_origin"] = final["pelvis"]
    for name, axis in intent_frame.items():
        final["rig_frame_"+name] = add(final["rig_frame_origin"], axis)
    head = views["head"]
    anchor = registered_head_anchor(head, final)
    projected["head"] = add(projected["neck"], scale(add(head["cranialCentrePx"], scale(anchor, -1)), head["bindScale"]))
    projected["backpack"] = add(projected["neck"], add(project_intent["backpack"], scale(project_intent["neck"], -1)))
    final["head"] = _registered_lift(projected["head"], intent["head"], cameraDirection)
    final["backpack"] = _registered_lift(projected["backpack"], intent["backpack"], cameraDirection)
    if "weapon_grip" in intent:
        projected["weapon_grip"] = projected["right_wrist"]
        projected["weapon_muzzle"] = add(projected["right_wrist"], add(project_intent["weapon_muzzle"], scale(project_intent["weapon_grip"], -1)))
        for name in ["weapon_grip", "weapon_muzzle"]:
            final[name] = _registered_lift(projected[name], intent[name], cameraDirection)
    return final


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


def similarity_transform(pivot, target, factor, angle=0.0):
    if factor <= 0 or not math.isfinite(factor) or not all(math.isfinite(value) for point in [pivot, target] for value in point):
        raise ValueError("Transformación de pieza degenerada")
    cosine, sine = math.cos(angle), math.sin(angle)
    tx = target[0] - factor * (cosine * pivot[0] - sine * pivot[1])
    ty = target[1] - factor * (sine * pivot[0] + cosine * pivot[1])
    forward = (factor * cosine, -factor * sine, tx, factor * sine, factor * cosine, ty)
    inverse = (cosine / factor, sine / factor, -(cosine * tx + sine * ty) / factor,
               -sine / factor, cosine / factor, (sine * tx - cosine * ty) / factor)
    return forward, inverse


def uniform_piece(canvas, piece, pivot, target, factor, angle=0.0, mip=None):
    forward, inverse = similarity_transform(pivot, target, factor, angle)
    mip = piece if mip is None else mip
    sampled = tuple(value * (mip.width / piece.width if index < 3 else mip.height / piece.height) for index, value in enumerate(inverse))
    canvas.alpha_composite(mip.transform(canvas.size, Image.Transform.AFFINE, sampled, Image.Resampling.NEAREST))
    return {"localToGlobal": forward, "globalToLocal": inverse, "pivot": pivot, "target": target, "scale": factor, "angle": angle}


def uniform_bone(canvas, piece, source_first, source_second, target_first, target_second, mip=None, source_diameter=None, maximum_diameter=None):
    source_length = math.dist(source_first, source_second)
    if source_length < 1e-8:
        raise ValueError("El binding de hueso tiene un eje local sin longitud")
    factor = math.dist(target_first, target_second) / source_length
    angle = math.atan2(target_second[1]-target_first[1], target_second[0]-target_first[0]) - math.atan2(source_second[1]-source_first[1], source_second[0]-source_first[0])
    pivot, target = source_first, target_first
    if maximum_diameter is not None:
        if source_diameter is None or source_diameter <= 0 or maximum_diameter <= 0:
            raise ValueError("El límite de volumen necesita diámetros positivos")
        factor = min(factor, maximum_diameter / source_diameter)
        # El volumen autorado se centra en el segmento cuando su eje proyectado se acorta por perspectiva.
        pivot = tuple((source_first[index]+source_second[index])/2 for index in range(2))
        target = tuple((target_first[index]+target_second[index])/2 for index in range(2))
    return uniform_piece(canvas, piece, pivot, target, factor, angle, mip)


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


def _registered_render_angle(source_first, source_last, target_first, target_last):
    source = tuple(source_last[index]-source_first[index] for index in range(2))
    target = tuple(target_last[index]-target_first[index] for index in range(2))
    if math.hypot(*source) <= 1e-8 or math.hypot(*target) <= 1e-8:
        raise ValueError("El dibujo canónico recibió un eje sin orientación")
    return math.atan2(target[1], target[0])-math.atan2(source[1], source[0])


def _registered_render_dorsal_visible(points, camera_direction):
    dorsal = local(body_frame(points)["dorsal"], camera_direction)
    # La normal de la cámara se obtiene de la proyección isométrica real, incluyendo su componente vertical.
    return dorsal[0]+dorsal[1]+1.35*dorsal[2] > 0.0


def render_registered(views, final_points, cameraDirection="S", layer="full", trace=None):
    if layer not in {"full", "lower", "upper", "cqcBody", "cqcArms"}:
        raise ValueError("Capa canónica desconocida: " + layer)
    canvas = Image.new("RGBA", (CELL, CELL))
    projected = {name: project(point, cameraDirection) for name, point in final_points.items()}
    sides = sorted(["right", "left"], key=lambda side: sum(local(final_points[side+"_hip"], cameraDirection)[:2]))
    arms = layer not in {"lower", "cqcBody"}
    body = layer != "cqcArms"
    lower = layer not in {"upper", "cqcArms"}
    upper = layer != "lower"
    active = set()

    def place(role, target, angle=0.0, anchor="proximalPx", view_override=None):
        view = views[role] if view_override is None else view_override
        transform = render_registered_piece(canvas, view, target, angle, anchor)
        active.add(role)
        if trace is not None:
            trace.append({**transform, "role": role, "sourceFile": view["file"], "sourceDirection": view["sourceDirection"],
                          "sourceSha256": view["sourceSha256"], "bindScale": view["bindScale"], "active": True, "layer": layer})
        return transform

    def bone(role, first, last):
        view = views[role]
        angle = _registered_render_angle(view["proximalPx"], view["distalPx"], projected[first], projected[last])
        return place(role, projected[first], angle)

    torso = views["torso"]
    torso_angle = _registered_render_angle(torso["proximalPx"], torso["distalPx"], projected["neck"], projected["waist"])
    pelvis = views["pelvis"]
    pelvis_angle = _registered_render_angle(pelvis["rightHipPx"], pelvis["leftHipPx"], projected["right_hip"], projected["left_hip"])
    dorsal_visible = _registered_render_dorsal_visible(final_points, cameraDirection)

    def pack():
        view = views["backpack"]
        angle = _registered_render_angle(view["proximalPx"], view["distalPx"], projected["waist"], projected["neck"])
        place("backpack", projected["backpack"], angle, "anchorPx")

    if upper and body and not dorsal_visible:
        pack()
    if lower:
        for side in sides:
            for role, first, last in [("thigh", "hip", "knee"), ("shin", "knee", "ankle"), ("boot", "ankle", "toe")]:
                bone(side+"_"+role, side+"_"+first, side+"_"+last)
        place("pelvis", projected["pelvis"], pelvis_angle, "anchorPx")
    if upper:
        shoulder_depths = [sum(local(final_points[side+"_shoulder"], cameraDirection)[:2]) for side in sides]
        near = sides[-1] if abs(shoulder_depths[1]-shoulder_depths[0]) > 1e-6 else None
        if arms:
            for side in sides:
                if side != near:
                    bone(side+"_upper_arm", side+"_shoulder", side+"_elbow")
        if body:
            place("torso", projected["neck"], torso_angle)
        if arms and near is not None:
            bone(near+"_upper_arm", near+"_shoulder", near+"_elbow")
        if body and dorsal_visible:
            pack()
        if arms:
            for side in sides:
                bone(side+"_forearm", side+"_elbow", side+"_wrist")
                if not views[side+"_forearm"].get("embeddedHand"):
                    view = views[side+"_hand"]
                    wrist = view["anchorPx"]
                    fingers = view.get("fingerCentrePx", [view["image"].width*.5, view["image"].height*.5])
                    angle = _registered_render_angle(wrist, fingers, projected[side+"_elbow"], projected[side+"_wrist"])
                    place(side+"_hand", projected[side+"_wrist"], angle, "anchorPx")
        if body:
            view = dict(views["head"])
            view["_registeredCervicalPx"] = registered_head_anchor(view, final_points)
            cranial = view.get("cranialCentrePx", view["distalPx"])
            angle = _registered_render_angle(view["_registeredCervicalPx"], cranial, projected["neck"], projected["head"])
            place("head", projected["neck"], angle, "_registeredCervicalPx", view)
    if trace is not None:
        for role, view in views.items():
            if role not in active:
                trace.append({"role": role, "sourceFile": view["file"], "sourceDirection": view["sourceDirection"],
                              "sourceSha256": view["sourceSha256"], "bindScale": view["bindScale"], "active": False, "layer": layer})
    return canvas


def registered_prone_core(views, intentPoints, cameraDirection):
    centre = project(intentPoints["pelvis"], cameraDirection)
    neck = project(intentPoints["neck"], cameraDirection)
    torso = views["torso"]
    core_angle = _registered_render_angle(torso["proximalPx"], torso["distalPx"], neck, centre)
    pelvis = views["pelvis"]
    pelvis_angle = _registered_render_angle(pelvis["anchorPx"], pelvis["waistPx"], centre, neck)
    return centre, core_angle, pelvis_angle


def registered_prone_arm(views, intentPoints, cameraDirection, side, shoulder2D):
    shoulder = intentPoints[side+"_shoulder"]
    elbow = intentPoints[side+"_elbow"]
    wrist = intentPoints[side+"_wrist"]
    first = add(elbow, scale(shoulder, -1))
    last = add(wrist, scale(elbow, -1))
    denominator = length(first)*length(last)
    if denominator <= 1e-9:
        raise ValueError("La intención tumbada tiene una cadena de brazo sin longitud")
    cosine = max(-1.0, min(1.0, dot(first, last)/denominator))
    upper = registered_length(views[side+"_upper_arm"])
    forearm = registered_length(views[side+"_forearm"])
    # La flexión tridimensional se conserva con las longitudes autoradas, sin estirar la imagen para alcanzar la muñeca antigua.
    reach = math.sqrt(max(0.0, upper*upper+forearm*forearm+2*upper*forearm*cosine))
    source_shoulder = project(shoulder, cameraDirection)
    screen_reach = add(project(wrist, cameraDirection), scale(source_shoulder, -1))
    screen_bend = add(project(elbow, cameraDirection), scale(source_shoulder, -1))
    direction = unit(screen_reach) if length(screen_reach) > 1e-9 else unit(screen_bend)
    if length(direction) <= 1e-9:
        raise ValueError("La intención tumbada no define una orientación visible de brazo")
    target = add(shoulder2D, scale(direction, reach))
    hint = add(shoulder2D, scale(unit(screen_bend), upper))
    return target, hint


def registered_rest_pelvis(views, cameraDirection, crouched):
    heading = DIRECTIONS.index(cameraDirection)*math.pi/4
    return (ANCHOR[0]-8*math.sin(heading) if crouched else ANCHOR[0], 134.0 if crouched else 113.5)


def registered_rest_leg(views, cameraDirection, side, crouched, hip):
    upper = registered_length(views[side+"_thigh"])
    lower = registered_length(views[side+"_shin"])
    sign = 1 if side == "left" else -1
    if crouched:
        # Las claves FK mantienen una rodilla de apoyo y otra adelantada; las piezas no cambian de dimensión.
        thigh_angles = {"left": [83, 50, 20, -50, -97, -130, 160, 125],
                        "right": [97, 110, 110, 104, -83, 70, 70, 70]}
        shin_angles = {"left": [-100, 97, 97, 95, 80, 83, 83, 83],
                       "right": [-80, 210, 210, -124, 100, -50, -30, -30]}
        index = DIRECTIONS.index(cameraDirection)
        first_angle = math.radians(thigh_angles[side][index])
        last_angle = math.radians(shin_angles[side][index])
    else:
        first_angle, last_angle = math.pi/2-sign*.035, math.pi/2-sign*.015
    knee = add(hip, (upper*math.cos(first_angle), upper*math.sin(first_angle)))
    return knee, add(knee, (lower*math.cos(last_angle), lower*math.sin(last_angle)))
