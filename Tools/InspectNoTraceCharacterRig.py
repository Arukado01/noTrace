"""Inspecciona el rig de autoría real sin modificar el atlas ni el juego."""

import argparse
import hashlib
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

import GenerateNoTraceCharactersActions as actions
import GenerateNoTraceCharactersRig as rig
from GenerateNoTraceCharactersProne import ProneBindings


ROOT = Path(__file__).resolve().parents[1]
COLOURS = [(255, 191, 74), (71, 221, 228), (244, 119, 184), (153, 225, 118), (180, 166, 255)]


def affine(matrix, point):
    return [matrix[0] * point[0] + matrix[1] * point[1] + matrix[2], matrix[3] * point[0] + matrix[4] * point[1] + matrix[5]]


def invert(matrix):
    a, b, c, d, e, f = matrix
    determinant = a * e - b * d
    if abs(determinant) < 1e-12:
        raise ValueError('Matriz de diagnóstico singular')
    return [e / determinant, -b / determinant, (b * f - e * c) / determinant,
            -d / determinant, a / determinant, (d * c - a * f) / determinant]


def image_bindings(bindings):
    result = {}
    for direction, bank in bindings.banks.items():
        for role, view in bank.items():
            result[id(view['image'])] = {'role': role, 'view': view['id'], 'sourceRect': view['sourceRect'], 'bank': direction}
    for view in bindings.auxiliary:
        result[id(view['image'])] = {'role': view['role'], 'view': view.get('id', view['role']), 'sourceRect': view['sourceRect'], 'bank': 'auxiliary'}
    for direction, parts in bindings.original.items():
        for role, image in parts.items():
            result[id(image)] = {'role': role, 'view': direction + '/' + role, 'sourceRect': [0, 0, *image.size], 'bank': 'original_mip64'}
    return result


def trace_render(bindings, points, direction, clip, phase, stance):
    identities = image_bindings(bindings)
    records = []
    functions = {rig.uniform_piece.__code__: 'uniform', rig.bone.__code__: 'original', rig.object_piece.__code__: 'original_object'}

    def profile(frame, event, returned):
        if event != 'return' or frame.f_code not in functions:
            return
        values = frame.f_locals
        piece = values.get('piece')
        if piece is None or min(piece.size) <= 1:
            return
        kind = functions[frame.f_code]
        identity = identities.get(id(piece), {'role': 'unregistered', 'view': 'unregistered', 'sourceRect': [0, 0, *piece.size]})
        if kind == 'uniform':
            transform = dict(returned)
            transform['axisScale'] = [values['factor'], values['factor']]
        elif kind == 'original':
            if 'matrix' not in values:
                return
            transform = {'globalToLocal': list(values['matrix']), 'localToGlobal': invert(values['matrix']),
                         'pivot': values['source_a'], 'target': values['first'], 'axisScale': [values['along'], values['across']]}
        else:
            factor = values['factor']
            resized = values['resized']
            centre = values['center']
            x, y = round(centre[0] - resized.width * .5), round(centre[1] - resized.height * .5)
            transform = {'localToGlobal': [resized.width / piece.width, 0, x, 0, resized.height / piece.height, y],
                         'pivot': [piece.width * .5, piece.height * .5], 'target': centre,
                         'axisScale': [resized.width / piece.width, resized.height / piece.height], 'nominalScale': factor}
            transform['globalToLocal'] = invert(transform['localToGlobal'])
        transform.update(identity)
        transform['kind'] = kind
        transform['sampleSize'] = list(piece.size)
        transform['jointNames'] = list(rig.PART_JOINTS.get(identity['role'].replace('_idle', ''), ()))
        transform['transformedRect'] = [affine(transform['localToGlobal'], point) for point in [(0, 0), (piece.width, 0), (piece.width, piece.height), (0, piece.height)]]
        records.append(transform)

    previous = sys.getprofile()
    try:
        sys.setprofile(profile)
        rendered = bindings.render(points, direction, clip, phase, stance)
    finally:
        sys.setprofile(previous)
    return rendered, records


def joint_records(points, direction):
    rows = []
    for name, point in points.items():
        parent = rig.JOINT_PARENTS.get(name)
        relative = [point[index] - points[parent][index] for index in range(3)] if parent in points else list(point)
        rows.append({'name': name, 'parent': parent, 'modelGlobal': list(point), 'parentRelative': relative,
                     'orientedWorldRelative': list(rig.local(point, direction)), 'spritePosition': list(rig.project(point, direction))})
    return rows


def diagnostic_image(rendered, rows, transforms, label, scale):
    margin = 48
    extent = rig.CELL * scale
    sheet = Image.new('RGB', (extent * 2 + 680, extent + margin + 48), (23, 31, 40))
    font = ImageFont.truetype('C:/Windows/Fonts/consola.ttf', 15)
    draw = ImageDraw.Draw(sheet)
    draw.text((12, 8), label, font=font, fill=(235, 240, 245))
    background = Image.new('RGBA', rendered.size, (23, 31, 40, 255))
    background.alpha_composite(rendered)
    enlarged = background.convert('RGB').resize((extent, extent), Image.Resampling.NEAREST)
    sheet.paste(enlarged, (0, margin))
    sheet.paste(enlarged, (extent, margin))
    draw.text((12, margin + extent + 10), 'Rig real: imagen sin guias', font=font, fill=(220, 230, 235))
    draw.text((extent + 12, margin + extent + 10), 'Joints / pivotes / rectangulos transformados', font=font, fill=(220, 230, 235))

    def display(point):
        return (extent + point[0] * scale, margin + point[1] * scale)

    positions = {row['name']: row['spritePosition'] for row in rows}
    for index, record in enumerate(transforms):
        colour = COLOURS[index % len(COLOURS)]
        polygon = [display(point) for point in record['transformedRect']]
        draw.line(polygon + [polygon[0]], fill=colour, width=1)
        pivot = display(affine(record['localToGlobal'], record['pivot']))
        draw.rectangle((pivot[0] - 3, pivot[1] - 3, pivot[0] + 3, pivot[1] + 3), outline=colour, width=1)
    for index, row in enumerate(rows):
        point = display(row['spritePosition'])
        if row['parent'] in positions:
            draw.line((display(positions[row['parent']]), point), fill=(90, 235, 230), width=2)
        draw.ellipse((point[0] - 3, point[1] - 3, point[0] + 3, point[1] + 3), fill=(250, 225, 90))
        draw.text((point[0] + 4, point[1] - 12), str(index), font=font, fill=(255, 245, 205))
    anchor = display(rig.ANCHOR)
    draw.line((anchor[0] - 10, anchor[1], anchor[0] + 10, anchor[1]), fill=(255, 100, 100), width=2)
    draw.line((anchor[0], anchor[1] - 10, anchor[0], anchor[1] + 10), fill=(255, 100, 100), width=2)
    text_x = extent * 2 + 14
    draw.text((text_x, margin), 'JERARQUIA REAL / OFFSET AL PADRE (actor XYZ)', font=font, fill=(110, 225, 225))
    for index, row in enumerate(rows):
        value = ','.join(f'{number:.2f}' for number in row['parentRelative'])
        draw.text((text_x, margin + 25 + index * 19), f'{index:02} {row["name"]} <- {row["parent"] or "root"}: {value}', font=font, fill=(220, 225, 230))
    draw.text((text_x, margin + 25 + len(rows) * 19), 'JSON: matrices exactas, vistas, recortes y posiciones globales.', font=font, fill=(175, 195, 210))
    return sheet


def inspect(root, output, clip, stance, directions, phases, scale):
    if output.exists():
        raise ValueError('La salida de diagnostico ya existe; elija otra carpeta')
    output.mkdir(parents=True)
    source_files = [Path(__file__), Path(rig.__file__), Path(actions.__file__), ROOT / 'Tools/GenerateNoTraceCharactersProne.py', root / 'Manifests/prone-rig-pieces.registration.json']
    sources = [{'file': str(path.relative_to(ROOT)), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()} for path in source_files]
    bindings = ProneBindings(root)
    records = []
    for direction in directions:
        for phase in phases:
            points = actions.body_pose(clip, phase, stance)
            untouched = json.dumps(points, sort_keys=True)
            rendered, transforms = trace_render(bindings, points, direction, clip, phase, stance)
            if json.dumps(points, sort_keys=True) != untouched:
                raise ValueError('El dibujo del rig modifica la pose fisica')
            plain = bindings.render(points, direction, clip, phase, stance)
            if rendered.tobytes() != plain.tobytes():
                raise ValueError('El modo de diagnostico altera los pixeles del rig')
            rows = joint_records(points, direction)
            stem = f'{clip}-{stance}-{direction}-{phase}'
            rendered.save(output / (stem + '-sprite.png'))
            diagnostic_image(rendered, rows, transforms, stem, scale).save(output / (stem + '.png'))
            record = {'clip': clip, 'stance': stance, 'direction': direction, 'phase': phase, 'anatomicalFrame': rig.body_frame(points),
                      'groundAnchor': list(rig.ANCHOR), 'joints': rows, 'transforms': transforms,
                      'spriteFile': stem + '-sprite.png', 'diagnosticFile': stem + '.png',
                      'rgbaSha256': hashlib.sha256(rendered.tobytes()).hexdigest()}
            (output / (stem + '.json')).write_text(json.dumps(record, indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')
            records.append(record)
    if any(hashlib.sha256(path.read_bytes()).hexdigest() != expected['sha256'] for path, expected in zip(source_files, sources)):
        raise ValueError('Una fuente cambio durante el diagnostico; no aceptar este lote')
    report = {'status': 'PASS', 'poses': len(records), 'diagnosticPixelsEqualToPlainRender': True, 'poseInputsUnmodified': True,
              'scope': 'Rig de autoria Python real antes de hornear Atlas; no es captura runtime. Jerarquia observada en coordenadas globales del autor, sin herencia de escala.',
              'coordinateSpaces': {'modelGlobal': 'Joints resueltos en el espacio del actor de autoria', 'parentRelative': 'Diferencia hijo-padre en esa misma base; no herencia de escala',
                                   'orientedWorldRelative': 'Actor orientado por Rig.local, sin traslacion del jugador en el nivel', 'spritePosition': 'Pixeles de celda192; ancla96,144',
                                   'localToGlobal': 'Pixeles de la pieza fuente a pixeles de la celda', 'globalToLocal': 'Inversa exacta usada por el muestreo'},
              'sources': sources,
              'records': records}
    (output / 'rig-diagnostics.json').write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + '\n', encoding='utf-8')
    links = '\n'.join(f'<li><a href="{row["diagnosticFile"]}">{row["clip"]} {row["stance"]} {row["direction"]} fase {row["phase"]}</a> · <a href="{row["clip"]}-{row["stance"]}-{row["direction"]}-{row["phase"]}.json">matrices/joints</a></li>' for row in records)
    (output / 'index.html').write_text('<!doctype html><meta charset="utf-8"><title>NO TRACE · diagnóstico de rig</title><body style="background:#172028;color:#dce5eb;font:16px monospace"><h1>Rig de autoría · diagnóstico</h1><p>Huesos, articulaciones, pivotes y matrices del dibujo real. Herramienta offline, sin código en producción.</p><ul>' + links + '</ul></body>', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key not in ['records', 'sources']}, ensure_ascii=False))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT / 'Art/NoTrace/Characters')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clip', choices=[entry[0] for entry in actions.CLIPS], default='Aim')
    parser.add_argument('--stance', choices=['Standing', 'Crouched', 'Prone'], default='Prone')
    parser.add_argument('--directions', choices=rig.DIRECTIONS, nargs='+', default=rig.DIRECTIONS)
    parser.add_argument('--phases', choices=range(8), type=int, nargs='+', default=list(range(8)))
    parser.add_argument('--scale', type=int, choices=range(2, 7), default=3)
    args = parser.parse_args()
    inspect(args.root.resolve(), args.output.resolve(), args.clip, args.stance, args.directions, args.phases, args.scale)
