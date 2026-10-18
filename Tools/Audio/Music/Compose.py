import argparse
import hashlib
import html
import json
import math
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description="Compone desde cero las cuatro capas originales de NO TRACE.")
parser.add_argument("--python-deps", type=Path, default=Path("build/qa-cierre-eventos-20261017/audio-python"))
parser.add_argument("--output", type=Path, default=Path("Content/Musics/NoTrace"))
args = parser.parse_args()
sys.path.insert(0, str(args.python_deps.resolve()))
import numpy as np
import soundfile as sf
from scipy.fft import rfft, irfft
from scipy.signal import butter, sosfilt

RATE = 48000
SECONDS = 192
FRAMES = RATE * SECONDS
TAU = 2 * np.pi
DIRECTORY = Path(__file__).resolve().parent
OUTPUT = args.output.resolve()
OUTPUT.mkdir(parents=True, exist_ok=True)
EVENTS = []


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def hz(note):
    return 440 * 2 ** ((note - 69) / 12)


def envelope(time, duration, attack, release, curve=1.5):
    return (np.minimum(time / attack, 1) * np.minimum((duration - time) / release, 1)).clip(0, 1) ** curve


def place(track, signal, start, gain, pan, kind, notes=None):
    index = int(round(start * RATE)) % FRAMES
    size = len(signal)
    angle = (pan + 1) * np.pi / 4
    stereo = np.column_stack((signal * math.cos(angle), signal * math.sin(angle))).astype(np.float32)
    stereo *= gain
    first = min(size, FRAMES - index)
    track[index:index + first] += stereo[:first]
    if size > first:
        track[:size - first] += stereo[first:]
    EVENTS.append({"kind": kind, "startSeconds": round(start % SECONDS, 6), "durationSeconds": round(size / RATE, 6), "notesMidi": notes or [], "gain": round(gain, 5), "pan": round(pan, 3)})


def bowed(note, duration, rng, brightness=1):
    time = np.arange(round(duration * RATE), dtype=np.float32) / RATE
    fundamental = hz(note)
    result = np.zeros(len(time), dtype=np.float32)
    phases = rng.uniform(0, TAU, 7)
    for partial in range(1, 8):
        slow = 0.0025 * np.sin(TAU * (0.041 + partial * 0.008) * time + phases[partial - 1])
        drift = 0.0018 * np.sin(TAU * 0.173 * time + phases[partial - 1])
        weight = math.exp(-partial / (1.4 + brightness)) / partial ** 0.75
        for spread in (-1, 1):
            phase = TAU * fundamental * partial * (time * (1 + spread * 0.0007) + slow + drift / partial)
            result += (weight * 0.5 * np.sin(phase + phases[partial - 1])).astype(np.float32)
    result *= envelope(time, duration, 3.8, 5.5)
    result *= 0.9 + 0.1 * np.sin(TAU * 0.13 * time + phases[0])
    return result


def felt(note, duration, rng, tension=False):
    time = np.arange(round(duration * RATE), dtype=np.float32) / RATE
    fundamental = hz(note)
    result = np.zeros(len(time), dtype=np.float32)
    partials = [(1, 1, 0.58), (2.001, 0.27, 0.42), (3.006, 0.09, 0.24), (4.012, 0.025, 0.11)]
    for ratio, weight, decay in partials:
        phase = rng.uniform(0, TAU)
        result += (weight * np.sin(TAU * fundamental * ratio * time + phase) * np.exp(-time / (duration * decay))).astype(np.float32)
    result *= envelope(time, duration, 0.095 if tension else 0.19, 0.8, 1)
    return result


def bass(note, duration, rng):
    time = np.arange(round(duration * RATE), dtype=np.float32) / RATE
    phase = rng.uniform(0, TAU)
    result = np.sin(TAU * hz(note) * time + phase) + 0.13 * np.sin(TAU * hz(note) * 2 * time + phase / 2)
    result *= envelope(time, duration, 1.9, 3.3)
    return result.astype(np.float32)


def percussion(kind, rng, length=0.7):
    time = np.arange(round(length * RATE), dtype=np.float32) / RATE
    noise = rng.standard_normal(len(time)).astype(np.float32)
    if kind == "soft-low":
        phase = TAU * (47 * time + 8 * (1 - np.exp(-time * 17)) / 17)
        signal = np.sin(phase) * np.exp(-time * 9)
        signal += sosfilt(butter(2, 240, fs=RATE, output="sos"), noise) * np.exp(-time * 22) * 0.1
    elif kind == "muted-metal":
        signal = (0.5 * np.sin(TAU * 173 * time) + 0.3 * np.sin(TAU * 281.1 * time) + 0.18 * np.sin(TAU * 443.7 * time)) * np.exp(-time * 14)
        signal += sosfilt(butter(2, (700, 1800), btype="bandpass", fs=RATE, output="sos"), noise) * np.exp(-time * 35) * 0.2
    else:
        signal = sosfilt(butter(2, (400, 2500), btype="bandpass", fs=RATE, output="sos"), noise) * np.exp(-time * 18)
    return (signal * envelope(time, length, 0.012, 0.06, 1)).astype(np.float32)


def atmosphere(track, rng, gain):
    # El espectro y las modulaciones periódicas conservan la continuidad al cerrar el bucle.
    size = FRAMES // 12
    frequency = np.fft.rfftfreq(size, 12 / RATE)
    shape = np.exp(-((frequency / 850) ** 1.4)) / np.maximum(frequency, 60) ** 0.8
    shape[frequency < 36] = 0
    for channel in range(2):
        spectrum = (rng.standard_normal(len(frequency)) + 1j * rng.standard_normal(len(frequency))) * shape
        wave = irfft(spectrum, size).astype(np.float32)
        from scipy.signal import resample
        wave = resample(wave, FRAMES).astype(np.float32)
        wave /= max(float(np.std(wave)), 1e-10)
        time = np.arange(FRAMES, dtype=np.float32) / RATE
        breath = 0.65 + 0.18 * np.cos(TAU * 5 * time / SECONDS + channel) + 0.12 * np.cos(TAU * 11 * time / SECONDS + 1.7)
        track[:, channel] += wave * gain * breath


def circular_filter(signal, cutoff, high=35):
    frequencies = np.fft.rfftfreq(len(signal), 1 / RATE)
    response = (1 - np.exp(-(frequencies / high) ** 4)) * np.exp(-(frequencies / cutoff) ** 4)
    return irfft(rfft(signal) * response.astype(np.float32), len(signal)).astype(np.float32)


def mix_room(track, rng, amount):
    # La convolución circular incluye antes de la muestra cero las colas del final.
    impulse_size = int(RATE * 8)
    time = np.arange(impulse_size, dtype=np.float32) / RATE
    ir = np.zeros((impulse_size, 2), dtype=np.float32)
    for channel in range(2):
        noise = rng.standard_normal(impulse_size).astype(np.float32)
        noise = sosfilt(butter(2, 1700, fs=RATE, output="sos"), noise)
        noise *= np.exp(-time / 1.35) * np.minimum(time / 0.13, 1)
        ir[:, channel] = noise * 0.012
        for delay, level in ((0.079, 0.11), (0.149, -0.09), (0.263, 0.07), (0.431, 0.055), (0.719, -0.04)):
            ir[int((delay + channel * 0.011) * RATE), channel] += level
        ir[:, channel] /= max(np.sqrt(np.sum(ir[:, channel] ** 2)), 1e-10)
    mono = track.mean(axis=1)
    spectrum = rfft(mono)
    for channel in range(2):
        tail = irfft(spectrum * rfft(ir[:, channel], n=FRAMES), FRAMES)
        track[:, channel] += tail * amount
        track[:, channel] = circular_filter(track[:, channel], 5800)
    return track


def weighted_loudness(audio):
    from scipy.signal import lfilter
    filtered = lfilter([1.53512485958697, -2.69169618940638, 1.19839281085285], [1, -1.69065929318241, 0.73248077421585], audio, axis=0)
    filtered = lfilter([1, -2, 1], [1, -1.99004745483398, 0.99007225036621], filtered, axis=0)
    power = np.sum(filtered ** 2, axis=1)
    cumulative = np.r_[0, np.cumsum(power)]
    starts = np.arange(0, len(audio) - 19200 + 1, 4800)
    blocks = (cumulative[starts + 19200] - cumulative[starts]) / 19200
    levels = -0.691 + 10 * np.log10(np.maximum(blocks, 1e-20))
    selected = blocks[levels > -70]
    relative = -0.691 + 10 * np.log10(max(float(np.mean(selected)), 1e-20)) - 10
    selected = blocks[(levels > -70) & (levels > relative)]
    return float(-0.691 + 10 * np.log10(max(float(np.mean(selected)), 1e-20)))


HARMONIES = {
    "menu": [[38, 57, 60, 64], [38, 53, 57, 62], [34, 53, 57, 64], [34, 57, 60, 65], [43, 53, 58, 62], [43, 57, 62, 65], [36, 55, 58, 62], [36, 57, 62, 64], [34, 53, 57, 60], [43, 53, 57, 62], [45, 55, 58, 63], [38, 57, 60, 64]],
    "lobby": [[38, 53, 57, 64], [38, 57, 60, 65], [43, 53, 58, 62], [43, 57, 62, 65], [34, 53, 57, 60], [34, 57, 60, 64], [36, 55, 58, 62], [36, 58, 62, 65], [43, 53, 57, 62], [45, 55, 58, 62], [45, 55, 58, 63], [38, 53, 57, 64]],
    "mission": [[38, 57, 60, 64], [38, 53, 57, 63], [34, 53, 57, 64], [34, 57, 60, 64], [43, 53, 57, 62], [43, 53, 58, 62], [38, 57, 60, 63], [38, 53, 57, 64], [36, 55, 58, 62], [34, 53, 57, 60], [45, 55, 58, 63], [38, 57, 60, 64]],
}


TRACKS = [
    {"id": "menu", "file": "OperacionEncubierta.ogg", "title": "Operación encubierta", "role": "Menú principal", "bpm": 60, "bars": 48, "seed": 26061801, "loudness": -23, "motif": [62, 69, 65, 64, 60, 62], "sectionNames": ["Dossier", "Clave", "Vigilancia", "Credenciales", "Pasillo vacío", "Señal", "Coordenadas", "Intercepción", "Ventana", "Orden", "Umbral", "Retorno al dossier"]},
    {"id": "lobby", "file": "PreparacionTactica.ogg", "title": "Preparación táctica", "role": "Lobby y sala de espera", "bpm": 75, "bars": 60, "seed": 26061802, "loudness": -24, "motif": [50, 57, 60, 63, 58, 57], "sectionNames": ["Mesa táctica", "Coordinación", "Ruta norte", "Ruta sur", "Equipo", "Punto de encuentro", "Comprobación", "Cobertura", "Ventana de acceso", "Confirmación", "En espera", "Mesa táctica"]},
    {"id": "mission", "file": "InfiltracionActiva.ogg", "title": "Infiltración activa", "role": "Misión, base de sigilo", "bpm": 90, "bars": 72, "seed": 26061803, "loudness": -27, "motif": [50, 51, 45, 48, 53, 52], "sectionNames": ["Perímetro", "Ángulo muerto", "Acceso", "Cámara", "Conducto", "Patrulla", "Archivo", "Cruce", "Ocultación", "Ruta alterna", "Salida vigilada", "Perímetro"]},
    {"id": "tension", "file": "InfiltracionTension.ogg", "title": "Infiltración activa — tensión", "role": "Capa adaptativa de misión", "bpm": 90, "bars": 72, "seed": 26061804, "loudness": -29, "motif": [50, 57, 51, 48], "sectionNames": ["Alerta latente"] * 12},
]


def compose(spec):
    print("Renderizando " + spec["file"], flush=True)
    rng = np.random.default_rng(spec["seed"])
    track = np.zeros((FRAMES, 2), dtype=np.float32)
    identity = spec["id"]
    harmony = HARMONIES["mission" if identity == "tension" else identity]
    events_start = len(EVENTS)
    sections = []
    atmosphere(track, rng, 0.00075 if identity == "mission" else 0.0004)
    for section in range(12):
        chord = harmony[section]
        start = section * 16
        density = [0.7, 0.82, 0.9, 0.76, 0.63, 0.84, 0.72, 0.66, 0.86, 0.93, 0.8, 0.7][section]
        sections.append({"startSeconds": start, "durationSeconds": 16, "name": spec["sectionNames"][section], "harmonyMidi": chord, "density": density})
        if identity != "tension":
            for voice, note in enumerate(chord[1:]):
                duration = 22 + voice * 0.63
                pad = bowed(note, duration, rng, 0.55 if identity == "mission" else 1)
                gain = 0.027 * density * (0.73 if identity == "mission" else 1)
                place(track, pad, start - 3 + voice * 0.21, gain, [-0.64, 0.08, 0.61][voice], "bowed-pad", [note])
            place(track, bass(chord[0], 20.5, rng), start - 2.2, 0.021 * density, 0, "slow-bass", [chord[0]])
        else:
            for voice, note in enumerate((chord[1] - 12, chord[-1] - 12)):
                place(track, bowed(note, 22, rng, 0.3), start - 3, 0.012, -0.4 + voice * 0.8, "shadow-pad", [note])
        motif = spec["motif"]
        beat = 60 / spec["bpm"]
        if identity == "menu":
            if section not in (4, 7):
                shift = -12 if section in (2, 6, 10) else 0
                offset = (1.3, 4.6, 7.8, 11.4)[section % 4]
                count = 3 if section % 3 else 4
                for step in range(count):
                    note = motif[(section * 2 + step) % len(motif)] + shift
                    place(track, felt(note, 5.6 + step * 0.3, rng), start + offset + step * 2.5, 0.013 * density, (-1) ** step * 0.26, "cipher-motif", [note])
            if section in (1, 3, 5, 8, 10, 11):
                place(track, percussion("soft-low", rng), start + 8, 0.018, -0.08, "soft-low")
                place(track, percussion("brush", rng), start + 11.5, 0.008, 0.3, "brush")
        elif identity == "lobby":
            for bar in range(5):
                bar_start = start + bar * 4 * beat
                if not (section in (4, 7) and bar in (1, 2)):
                    place(track, percussion("soft-low", rng), bar_start, 0.015 * density, 0, "soft-low")
                    place(track, felt(chord[0] + 12, 1.35, rng), bar_start + 1.5 * beat, 0.012, -0.26, "tactical-pulse", [chord[0] + 12])
                    if bar % 2 == 0:
                        place(track, percussion("brush", rng), bar_start + 2.75 * beat, 0.011, 0.32, "brush")
            if section not in (3, 7):
                for step in range(4):
                    note = motif[(section + step) % len(motif)]
                    place(track, felt(note, 3.8, rng), start + 2.4 + step * 2.8, 0.014 * density, 0.22 * (-1) ** step, "coordination-motif", [note])
        elif identity == "mission":
            for bar in range(6):
                bar_start = start + bar * 4 * beat
                if bar not in (2, 5):
                    place(track, felt(chord[0] + 12, 1.8, rng), bar_start + 0.1, 0.014 * density, -0.16, "stealth-pulse", [chord[0] + 12])
                if bar in (1, 4) and section not in (3, 7):
                    place(track, percussion("muted-metal", rng), bar_start + 2.5 * beat, 0.006, 0.4 * (-1) ** section, "muted-metal")
                if bar == 0 and section % 2:
                    place(track, percussion("soft-low", rng), bar_start, 0.01, 0, "soft-low")
            if section not in (2, 5, 8):
                for step in range(3):
                    note = motif[(section + step) % len(motif)]
                    place(track, felt(note, 5.2, rng), start + 2.1 + step * 3.5, 0.010 * density, 0.24 * (-1) ** step, "surveillance-motif", [note])
        else:
            for bar in range(6):
                bar_start = start + bar * 4 * beat
                for offset in (0, 1.5, 2.5):
                    place(track, percussion("soft-low", rng), bar_start + offset * beat, 0.013 * density * (1 if offset == 0 else 0.48), 0, "tension-low")
                for step in range(4):
                    note = chord[1 + (step + bar) % 3] - 12
                    place(track, felt(note, 1.3, rng, True), bar_start + (step + 0.5) * beat, 0.011 * density, 0.3 * (-1) ** step, "tension-ostinato", [note])
                if bar % 2 == 1:
                    place(track, percussion("muted-metal", rng), bar_start + 3.25 * beat, 0.006, 0.45 * (-1) ** bar, "tension-metal")
    track = mix_room(track, rng, 0.28 if identity == "menu" else 0.22)
    print("Mezcla circular preparada: " + spec["id"], flush=True)
    level = weighted_loudness(track)
    track *= 10 ** ((spec["loudness"] - level) / 20)
    peak = float(np.max(np.abs(track)))
    if peak > 0.75:
        raise ValueError("La mezcla requiere revisar su dinámica: " + identity)
    destination = OUTPUT / spec["file"]
    # La escritura por bloques evita que el codificador acumule una pista completa internamente.
    with sf.SoundFile(destination, mode="w", samplerate=RATE, channels=2, format="OGG", subtype="VORBIS", compression_level=0.8) as stream:
        for start in range(0, FRAMES, 16384):
            stream.write(track[start:start + 16384])
    decoded, decoded_rate = sf.read(destination, dtype="float32", always_2d=True)
    if decoded.shape != track.shape or decoded_rate != RATE:
        raise ValueError("El códec modificó la cuadrícula de bucle")
    score = {key: value for key, value in spec.items() if key not in ("seed", "loudness", "sectionNames")}
    score.update({"seed": spec["seed"], "sections": sections, "events": EVENTS[events_start:]})
    asset = {"file": spec["file"], "title": spec["title"], "role": spec["role"], "sampleRate": RATE, "channels": 2, "frames": FRAMES, "durationSeconds": SECONDS, "format": "Ogg Vorbis", "loop": True, "bpm": spec["bpm"], "bars": spec["bars"], "seed": spec["seed"], "source": "Síntesis local original de osciladores aditivos y ruido nuevo; sin muestras ni grabaciones externas", "license": "NO TRACE — todos los derechos reservados; LICENSE.md del proyecto", "sha256": digest(destination), "bytes": destination.stat().st_size, "sourcePcmSha256": hashlib.sha256(track.tobytes()).hexdigest(), "decodedPcm16Sha256": hashlib.sha256(np.rint(decoded.clip(-1, 1) * 32767).astype("<i2").tobytes()).hexdigest(), "estimatedIntegratedLufs": round(weighted_loudness(decoded), 3), "peakDbfs": round(20 * np.log10(float(np.max(np.abs(decoded)))), 3)}
    print(json.dumps({key: asset[key] for key in ("file", "durationSeconds", "estimatedIntegratedLufs", "peakDbfs", "bytes")}), flush=True)
    return asset, score


results = []
scores = []
for spec in TRACKS:
    asset, score = compose(spec)
    results.append(asset)
    scores.append(score)
score_path = DIRECTORY / "Score.json"
score_path.write_text(json.dumps({"version": 1, "sampleRate": RATE, "durationSeconds": SECONDS, "identity": "Re menor, novenas suspendidas, semitono frigio puntual, bajo lento, cuerdas sintéticas oscuras y percusión amortiguada", "tracks": scores}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
manifest = {"version": 1, "createdLocalDate": "2026-10-18", "generator": "Tools/Audio/Music/Compose.py", "generatorSha256": digest(Path(__file__)), "score": "Tools/Audio/Music/Score.json", "scoreSha256": digest(score_path), "compositionProcess": "Partituras y síntesis procedimental nuevas, escritas para NO TRACE; semillas independientes por pista. No se abrió ningún archivo de audio previo para componer.", "license": "El material creado se distribuye bajo la licencia propietaria del proyecto en LICENSE.md; no incorpora componentes de audio de terceros.", "loopMethod": "Eventos añadidos sobre una línea temporal circular de 9.216.000 muestras; reverberación y filtrado espectral circulares. Vorbis conserva el número exacto de frames.", "adaptation": {"base": "InfiltracionActiva.ogg", "layer": "InfiltracionTension.ogg", "sharedFrames": FRAMES, "sharedBpm": 90, "sharedHarmony": True, "startTogether": True, "normalLayerGain": 0, "note": "La tensión comparte la cuadrícula y los acordes; cambiar ganancia suavemente conserva la fase sin reiniciar."}, "auditoryReview": "La comprobación técnica y la partitura no certifican escucha humana ni aceptación artística o mezcla en partida.", "assets": results}
(OUTPUT / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
(OUTPUT / "LICENSE.txt").write_text("NO TRACE — MÚSICA ORIGINAL\nCopyright (c) 2026 Carlos Cortina\nTodos los derechos reservados conforme a LICENSE.md del proyecto.\n\nOperación encubierta; Preparación táctica; Infiltración activa; Infiltración activa — tensión.\nComposición y síntesis procedimental originales creadas para este proyecto.\nNo se han utilizado muestras, grabaciones, melodías importadas ni modelos externos de generación musical.\nNumPy, SciPy y libsndfile/soundfile son herramientas de cálculo y codificación; sus licencias no sustituyen la licencia del audio creado.\nEl manifiesto identifica los archivos y sus hashes; Tools/Audio/Music contiene la partitura y el proceso reproducible.\n", encoding="utf-8")
cards = "\n".join(f'<article><h2>{html.escape(asset["title"])}</h2><p>{html.escape(asset["role"])} · 3:12 · estéreo 48 kHz · Ogg Vorbis</p><audio controls loop preload="metadata" src="{asset["file"]}"></audio><p>{asset["estimatedIntegratedLufs"]} LUFS estimados; pico {asset["peakDbfs"]} dBFS.</p></article>' for asset in results)
(OUTPUT / "gallery.html").write_text('<!doctype html><html lang="es"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>NO TRACE — música original</title><style>body{background:#111817;color:#d9e1d7;font:16px system-ui;max-width:900px;margin:40px auto;padding:0 24px}h1{color:#b3c77e}article{padding:20px;margin:20px 0;background:#1d2924;border-left:3px solid #738e65}audio{width:100%}a{color:#b3c77e}p{line-height:1.6}</style><h1>NO TRACE — música original</h1><p>Espionaje contenido: re menor, armonías suspendidas, sintetizadores de arco oscuro, graves lentos y percusión amortiguada. Las tres composiciones duran 3:12 y se repiten en bucle. La cuarta es una capa de tensión sincronizada con la misión, diseñada para mezclarse con su base.</p>' + cards + '<p>Audio sintetizado desde cero para NO TRACE. No contiene samples externos ni audio anterior del proyecto. Copyright © 2026 Carlos Cortina; todos los derechos reservados conforme a <a href="LICENSE.txt">la licencia del proyecto</a>. <a href="manifest.json">Procedencia, hashes y estructura técnica</a>. La reproducción de esta galería no prueba la integración en el motor. El análisis numérico no equivale a una escucha humana.</p></html>\n', encoding="utf-8")
