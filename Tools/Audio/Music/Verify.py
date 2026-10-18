import argparse
import hashlib
import json
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description="Comprueba archivos, sincronía, nivel, diversidad y cierre circular de la música.")
parser.add_argument("--python-deps", type=Path, default=Path("build/qa-cierre-eventos-20261017/audio-python"))
parser.add_argument("--root", type=Path, default=Path("Content/Musics/NoTrace"))
args = parser.parse_args()
sys.path.insert(0, str(args.python_deps.resolve()))
import numpy as np
import soundfile as sf
from scipy.signal import lfilter, resample_poly, welch


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def loudness(audio):
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


root = args.root.resolve()
manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
failures = []
reports = []
fingerprints = set()
signatures = []
source_root = Path(__file__).resolve().parents[3]
for key in ("generator", "score"):
    if digest(source_root / manifest[key]) != manifest[key + "Sha256"]:
        failures.append("El manifiesto no corresponde al archivo " + key)
for asset in manifest["assets"]:
    file = (root / asset["file"]).resolve()
    if not file.is_relative_to(root):
        raise ValueError("La pista sale del directorio autorizado")
    audio, rate = sf.read(file, dtype="float32", always_2d=True)
    info = sf.info(file)
    fingerprint = hashlib.sha256(np.rint(audio.clip(-1, 1) * 32767).astype("<i2").tobytes()).hexdigest()
    seconds = len(audio) / rate
    actual_level = loudness(audio)
    pcm_step = float(np.max(np.abs(audio[0] - audio[-1])))
    derivative = np.abs(np.diff(audio[::8], axis=0)) / 8
    derivative_p99 = float(np.quantile(derivative, 0.99))
    edge_energy = [float(np.mean(audio[:rate // 2] ** 2)), float(np.mean(audio[-rate // 2:] ** 2))]
    edge_ratio = abs(10 * np.log10(max(edge_energy[0], 1e-20) / max(edge_energy[1], 1e-20)))
    block_rms = np.sqrt(np.mean(audio.reshape(-1, rate // 4, 2) ** 2, axis=(1, 2)))
    longest_silent = 0
    current_silent = 0
    for value in block_rms:
        current_silent = current_silent + 0.25 if value < 10 ** (-65 / 20) else 0
        longest_silent = max(longest_silent, current_silent)
    peak = float(np.max(np.abs(audio)))
    true_peak = peak
    for start in range(0, len(audio), rate * 8):
        segment = audio[max(0, start - 64):min(len(audio), start + rate * 8 + 64)]
        true_peak = max(true_peak, float(np.max(np.abs(resample_poly(segment, 4, 1, axis=0)))))
    sections = audio.reshape(12, rate * 16, 2)
    features = []
    for section in sections:
        frequency, psd = welch(section[::4].mean(axis=1), fs=rate / 4, nperseg=8192)
        bands = [float(np.sum(psd[(frequency >= low) & (frequency < high)])) for low, high in ((35, 100), (100, 220), (220, 450), (450, 900), (900, 1800), (1800, 4000))]
        features.append([float(np.sqrt(np.mean(section ** 2))), *bands])
    features = np.array(features)
    difference = np.mean(np.abs(np.diff(np.log(np.maximum(features, 1e-12)), axis=0)))
    signature = audio[::480].mean(axis=1)
    signatures.append(signature)
    report = {"file": asset["file"], "frames": len(audio), "durationSeconds": seconds, "sampleRate": rate, "channels": audio.shape[1], "format": info.format, "subtype": info.subtype, "sha256": digest(file), "decodedPcm16Sha256": fingerprint, "estimatedIntegratedLufs": round(actual_level, 3), "peakDbfs": round(20 * np.log10(max(peak, 1e-12)), 3), "estimatedTruePeak4xDbfs": round(20 * np.log10(max(true_peak, 1e-12)), 3), "clippedSamples": int(np.count_nonzero(np.abs(audio) >= 1)), "dcOffset": np.mean(audio, axis=0).tolist(), "stereoCorrelation": round(float(np.corrcoef(audio[::48].T)[0, 1]), 5), "loopSampleStep": pcm_step, "derivative99Percentile": derivative_p99, "loopEdgeRmsDifferenceDb": round(float(edge_ratio), 3), "quietestQuarterSecondDbfs": round(float(20 * np.log10(max(float(np.min(block_rms)), 1e-20))), 3), "longestSilenceBelowMinus65DbfsSeconds": longest_silent, "sectionSpectralVariation": round(float(difference), 5), "sectionRmsDbfs": [round(float(20 * np.log10(value)), 2) for value in features[:, 0]]}
    checks = {"hash": report["sha256"] == asset["sha256"] and fingerprint == asset["decodedPcm16Sha256"], "uniquePcm": fingerprint not in fingerprints, "longForm": 120 <= seconds <= 240, "exactGrid": len(audio) == 9216000 and rate == 48000 and audio.shape[1] == 2, "vorbis": info.format == "OGG" and info.subtype == "VORBIS", "noClipping": true_peak < 0.89 and report["clippedSamples"] == 0, "containedLevel": -32 <= actual_level <= -20, "noLongSilence": longest_silent < 2, "continuousSampleJoin": pcm_step < 0.006, "continuousEdgeEnergy": edge_ratio < 6, "stereo": abs(report["stereoCorrelation"]) < 0.98, "sectionVariation": difference > 0.02, "dc": float(np.max(np.abs(np.mean(audio, axis=0)))) < 0.001}
    checks = {name: bool(value) for name, value in checks.items()}
    report["checks"] = checks
    for check, passed in checks.items():
        if not passed:
            failures.append(asset["file"] + ": " + check)
    fingerprints.add(fingerprint)
    reports.append(report)
    print(json.dumps({"file": report["file"], "lufs": report["estimatedIntegratedLufs"], "truePeak": report["estimatedTruePeak4xDbfs"], "join": pcm_step, "checks": checks}), flush=True)
correlations = []
for first in range(len(signatures)):
    for second in range(first + 1, len(signatures)):
        correlation = float(np.corrcoef(signatures[first], signatures[second])[0, 1])
        correlations.append({"first": reports[first]["file"], "second": reports[second]["file"], "sampledWaveformCorrelation": round(correlation, 6)})
        if abs(correlation) > 0.85:
            failures.append("Contenido excesivamente correlacionado entre pistas")
mission = next(report for report in reports if report["file"] == "InfiltracionActiva.ogg")
tension = next(report for report in reports if report["file"] == "InfiltracionTension.ogg")
aligned = all(mission[key] == tension[key] for key in ("frames", "sampleRate", "channels", "durationSeconds"))
if not aligned:
    failures.append("La capa adaptativa perdió la cuadrícula de misión")
result = {"passed": not failures, "method": "Decodificación real Vorbis con libsndfile; loudness estimada mediante dos filtros K, ventanas de400 ms/100 ms y puertas absoluta/relativa; pico intersample aproximado4x; no es un medidor certificado.", "auditoryReview": "No se afirma escucha humana. Esta verificación detecta errores técnicos, no acepta por sí sola timbre, composición, fatiga, mezcla de efectos ni transiciones en ejecución.", "loopReview": "Un salto de muestra pequeño y energía compatible son condiciones técnicas necesarias; la naturalidad musical requiere escuchar al menos un cierre completo en el juego.", "adaptiveGridAligned": aligned, "assets": reports, "pairwiseDiversity": correlations, "failures": failures}
(root / "verification.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
if failures:
    raise SystemExit("\n".join(failures))
print("Verificación técnica de música: correcta; escucha humana pendiente.")
