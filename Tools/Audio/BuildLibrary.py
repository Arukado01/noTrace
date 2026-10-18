import argparse
import hashlib
import html
import json
import math
from pathlib import Path
import sys

parser = argparse.ArgumentParser(description="Prepara la biblioteca CC0 desde grabaciones verificadas; no sintetiza efectos.")
parser.add_argument("--source-root", type=Path, default=Path("build/qa-cierre-eventos-20261017/audio-candidates"))
parser.add_argument("--python-deps", type=Path, default=Path("build/qa-cierre-eventos-20261017/audio-python"))
parser.add_argument("--output", type=Path, default=Path("Content/Audio/NoTrace"))
parser.add_argument("--report", type=Path, default=Path("docs/audio-redesign-2026-10-18"))
parser.add_argument("--metadata-only", action="store_true", help="Verifica WAV existentes y regenera manifiesto, créditos y galería sin reescribir audio.")
args = parser.parse_args()
sys.path.insert(0, str(args.python_deps.resolve()))
import numpy as np
import soundfile as sf
from scipy.signal import butter, resample_poly, sosfilt

directory = Path(__file__).resolve().parent
spec = json.loads((directory / "Library.json").read_text(encoding="utf-8"))
sources = {source["id"]: source for source in json.loads((directory / "Sources.json").read_text(encoding="utf-8"))["sources"]}
root = args.source_root.resolve()
output = args.output.resolve()
report = args.report.resolve()
output.mkdir(parents=True, exist_ok=True)
report.mkdir(parents=True, exist_ok=True)
rate = spec["sampleRate"]
verified = set()
fingerprints = set()
results = []
existing = {}
if args.metadata_only:
    existing = {asset["file"]: asset for asset in json.loads((output / "manifest.json").read_text(encoding="utf-8"))["assets"]}

for asset in spec["assets"]:
    source = sources[asset["source"]]
    if args.metadata_only:
        previous = existing[asset["file"]]
        destination = (output / asset["file"]).resolve()
        if not destination.is_relative_to(output) or any(previous.get(key) != value for key, value in asset.items()) or previous["sourceInfo"] != source or source["license"] != "CC0-1.0":
            raise ValueError("La receta o procedencia cambió; reconstruye el audio: " + asset["file"])
        if hashlib.file_digest(destination.open("rb"), "sha256").hexdigest() != previous["sha256"] or destination.stat().st_size != previous["bytes"]:
            raise ValueError("El WAV existente cambió: " + asset["file"])
        encoded, decoded_rate = sf.read(destination, dtype="int16", always_2d=True)
        pcm_hash = hashlib.sha256(encoded.tobytes()).hexdigest()
        if pcm_hash != previous["pcmSha256"] or pcm_hash in fingerprints or decoded_rate != rate or encoded.shape[1] != (2 if asset["loop"] else 1):
            raise ValueError("PCM existente inválido o duplicado: " + asset["file"])
        if np.max(np.abs(encoded.astype("int32"))) >= 32440 or previous["clippedSamples"] != 0:
            raise ValueError("El WAV existente satura: " + asset["file"])
        fingerprints.add(pcm_hash); verified.add(source["id"]); results.append(previous)
        continue
    path = (root / source["file"]).resolve()
    if not path.is_relative_to(root) or source["license"] != "CC0-1.0":
        raise ValueError("Fuente o ruta no autorizada: " + str(path))
    if source["id"] not in verified:
        if hashlib.file_digest(path.open("rb"), "sha256").hexdigest() != source["sourceFileSha256"]:
            raise ValueError("La fuente no coincide con la grabación auditada: " + str(path))
        verified.add(source["id"])
    info = sf.info(path)
    start = int(round(asset["start"] * info.samplerate))
    end = min(info.frames, int(round(asset["end"] * info.samplerate))) if asset["end"] is not None else info.frames
    if not 0 <= start < end <= info.frames:
        raise ValueError("Recorte fuera de la grabación: " + asset["file"])
    audio, original_rate = sf.read(path, start=start, stop=end, dtype="float64", always_2d=True)
    if not asset["loop"]:
        audio = audio.mean(axis=1, keepdims=True)
    elif audio.shape[1] == 1:
        audio = np.repeat(audio, 2, axis=1)
    audio -= audio.mean(axis=0)
    audio = sosfilt(butter(2, 28, btype="highpass", fs=original_rate, output="sos"), audio, axis=0)
    if original_rate != rate:
        factor = math.gcd(original_rate, rate)
        audio = resample_poly(audio, rate // factor, original_rate // factor, axis=0)
    if not np.all(np.isfinite(audio)) or np.max(np.abs(audio)) < 1e-5:
        raise ValueError("Recorte vacío o inválido: " + asset["file"])
    if asset["loop"]:
        overlap = min(int(rate * 2), len(audio) // 8)
        blend = np.linspace(0, 1, overlap, endpoint=False)[:, None]
        seam = audio[-overlap:] * (1 - blend) + audio[:overlap] * blend
        audio = np.concatenate([seam, audio[overlap:-overlap]], axis=0)
    else:
        envelope = np.max(np.abs(audio), axis=1)
        active = np.flatnonzero(envelope > np.max(envelope) * .003)
        first = max(0, int(active[0]) - int(rate * .008))
        last = min(len(audio), int(active[-1]) + int(rate * .025))
        audio = audio[first:last]
        attack = min(int(rate * .002), len(audio) // 8)
        release = min(int(rate * .025), len(audio) // 6)
        audio[:attack] *= np.linspace(0, 1, attack)[:, None]
        audio[-release:] *= np.linspace(1, 0, release)[:, None]
    peak = float(np.max(np.abs(audio)))
    rms = float(np.sqrt(np.mean(audio ** 2)))
    gain = min(asset["peak"] / peak, 10 ** (asset["rmsDb"] / 20) / max(rms, 1e-9))
    audio *= gain
    destination = (output / asset["file"]).resolve()
    if not destination.is_relative_to(output): raise ValueError("Destino fuera de Content/Audio")
    destination.parent.mkdir(parents=True, exist_ok=True)
    sf.write(destination, audio, rate, subtype=spec["subtype"])
    encoded, decoded_rate = sf.read(destination, dtype="int16", always_2d=True)
    pcm_hash = hashlib.sha256(encoded.tobytes()).hexdigest()
    if pcm_hash in fingerprints: raise ValueError("Dos recursos duplican el mismo PCM: " + asset["file"])
    fingerprints.add(pcm_hash)
    measured = encoded.astype("float64") / 32768
    peak = float(np.max(np.abs(measured)))
    rms = float(np.sqrt(np.mean(measured ** 2)))
    if peak >= .99 or rms <= .00001: raise ValueError("Recurso saturado o silencioso: " + asset["file"])
    results.append({**asset, "sourceInfo": source, "sourceFrames": [start, end], "sourceSampleRate": info.samplerate,
        "duration": len(encoded) / decoded_rate, "sampleRate": decoded_rate, "channels": encoded.shape[1], "bytes": destination.stat().st_size,
        "sha256": hashlib.file_digest(destination.open("rb"), "sha256").hexdigest(), "pcmSha256": pcm_hash,
        "peakDbFS": 20 * math.log10(max(peak, 1e-12)), "rmsDbFS": 20 * math.log10(max(rms, 1e-12)),
        "clippedSamples": int(np.count_nonzero(np.abs(measured) >= .99)), "processingGainDb": 20 * math.log10(gain),
        "processing": "Recorte de toma física, eliminación DC, pasaaltos 28 Hz, conversión polifase 48 kHz, fundidos de borde y ajuste de nivel; sin síntesis ni cambio de tono.",
        "auditoryReview": "Pendiente de escucha humana individual y en partida"})

manifest = {"schema": 1, "license": "CC0-1.0", "recordings": len(verified), "inactiveKinds": spec.get("inactiveKinds", []), "assets": results,
    "technicalValidation": {"decodable": len(results), "distinctPcmHashes": len(fingerprints), "clippedSamples": 0,
        "sampleRate": rate, "oldSourcesUsed": False, "auditoryAcceptance": False}}
(output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
(output / "LICENSE.txt").write_text("NO TRACE — biblioteca de grabaciones CC0 1.0.\nLos autores y las páginas primarias constan por recurso en manifest.json.\nhttps://creativecommons.org/publicdomain/zero/1.0/\nLas conversiones y los recortes no convierten estas grabaciones en autoría propia.\nNo se incluyen recursos de los lotes rechazados ni grabaciones de juegos propietarios.\n", encoding="utf-8")
rows = ["# Biblioteca nueva de NO TRACE", "", "Estado: integración técnica en curso; escucha humana y aceptación artística pendientes.", "",
    f"{len(results)} recursos, {len(verified)} grabaciones fuente verificadas CC0; {sum(asset['bytes'] for asset in results):,} bytes PCM. Ningún hash PCM duplicado ni muestras con clipping en los recursos exportados.", "",
    "Las variantes son contactos o tomas diferentes; no son copias transpuestas. Algunas fuentes de Freesound sólo ofrecen una vista previa HQ pública: el manifiesto identifica expresamente esos MP3, cuya conversión no recupera calidad del original. Los archivos de armas, puertas, teclado y Foley obtenidos de OpenGameArt son descargas originales de sus páginas primarias.", "",
    "| Acción | Recurso | Fuente y autor | Intervalo de fuente | PCM SHA256 | Escucha |", "|---|---|---|---|---|---|"]
for asset in results:
    source = asset["sourceInfo"]
    rows.append(f"| {asset['kind']} | {asset['file']} | [{source['title']} — {source['author']}]({source['sourceUrl']}) · CC0 | {asset['sourceFrames'][0]}–{asset['sourceFrames'][1]} a {asset['sourceSampleRate']} Hz | {asset['pcmSha256']} | Pendiente |")
rows += ["", "La comprobación de PCM, picos, duración, carga y hashes no acredita escucha, realismo ni coherencia perceptiva de mezcla. La galería permite revisar cada recurso y un grupo; la validación definitiva requiere también la reproducción real dentro del juego y LAN.", "", "Escaleras metálicas, agarres y ascensor emplean grabaciones físicas diferentes de los pasos de suelo y las puertas normales. El bus de música se valida en el motor; no se presenta una pista musical inexistente como composición nueva."]
(report / "REPORT.md").write_text("\n".join(rows) + "\n", encoding="utf-8")
cards = []
for asset in results:
    relative = Path(__import__('os').path.relpath(output / asset["file"], report)).as_posix()
    cards.append({"kind": asset["kind"], "variant": asset["variant"], "category": asset["category"], "url": relative, "author": asset["sourceInfo"]["author"], "source": asset["sourceInfo"]["sourceUrl"], "duration": round(asset["duration"], 3), "peak": round(asset["peakDbFS"], 1), "hash": asset["pcmSha256"], "notes": asset["notes"]})
payload = json.dumps(cards, ensure_ascii=False).replace("</", "<\\/")
gallery = """<!doctype html><meta charset="utf-8"><title>NO TRACE · escucha de biblioteca nueva</title>
<style>body{background:#07131c;color:#dce8ee;font:16px system-ui;margin:32px;max-width:1100px}button,select,input{font:inherit}button{background:#244638;color:#e7f2eb;border:1px solid #5d957d;border-radius:4px;padding:8px;margin:6px}article{border-top:1px solid #30414a;padding:14px 0}audio{display:block;width:100%;max-width:650px;margin:8px 0}small{color:#a4b6c2}a{color:#8ccebd}.pending{color:#e9b069}header{position:sticky;top:0;background:#07131cf5;padding:12px 0}code{font-size:12px;overflow-wrap:anywhere}</style>
<h1>NO TRACE · grabaciones CC0 nuevas</h1><p class="pending">Escucha humana pendiente. Esta galería no acredita aprobación artística ni sustituye la mezcla en partida.</p>
<header><label>Grupo <select id="group"><option value="">Todos</option></select></label><button id="sequence">Escuchar grupo en secuencia</button><button id="stop">Detener</button><label>Volumen de revisión <input id="volume" type="range" min="0" max="100" value="55"></label><div id="flows"></div><small>Secuencias de revisión de tomas; la mezcla espacial y el ritmo se validan dentro de la partida.</small><p id="now"></p></header><main></main>
<script>const clips=PAYLOAD;const main=document.querySelector('main'),group=document.querySelector('#group'),volume=document.querySelector('#volume');let queue=[],current=null;
for(const category of [...new Set(clips.map(c=>c.category))]){const option=document.createElement('option');option.value=category;option.textContent=category;group.append(option)}
function stop(){queue=[];for(const audio of document.querySelectorAll('audio')){audio.pause();audio.onended=null}current=null;document.querySelector('#now').textContent=''}
function show(){stop();main.replaceChildren();for(const clip of clips.filter(c=>!group.value||c.category===group.value)){const article=document.createElement('article'),title=document.createElement('strong'),audio=document.createElement('audio'),source=document.createElement('a'),details=document.createElement('small');title.textContent=clip.kind+' · toma '+clip.variant;audio.controls=true;audio.preload='none';audio.src=clip.url;audio.dataset.kind=clip.kind;audio.volume=volume.value/100;source.href=clip.source;source.textContent=clip.author+' · fuente CC0';details.textContent=' · '+clip.duration+' s · pico '+clip.peak+' dBFS. '+clip.notes;article.append(title,audio,source,details);main.append(article)}}
group.onchange=show;volume.oninput=()=>{for(const audio of document.querySelectorAll('audio'))audio.volume=volume.value/100};document.querySelector('#stop').onclick=stop;
function next(){if(!queue.length){stop();return}current=queue.shift();document.querySelector('#now').textContent=current.parentNode.querySelector('strong').textContent;current.currentTime=0;current.onended=next;current.play().catch(error=>document.querySelector('#now').textContent=error.message)}
const flows={'Sigilo':['StepHard','StepHard','StepCrouchHard','CoverEnter','Knock','CoverExit','ProneCrawl','GetUp','DoorOpen','DoorClose'],'Combate':['WeaponSwitch','WeaponReady','Pistol','ImpactBody','Reload','ReloadFinish','NonLethal','SuppressedNonLethal','Punch','Kick','Push','Grab','Choke','Fall'],'Verticalidad':['StepStair','StepStair','GripAttach','GripMove','GripMove','GripRelease','Land','Button','ElevatorClose','ElevatorStart','ElevatorStop','ElevatorOpen'],'Seguridad y dispositivos':['DetectionConfirmed','SearchStart','VigilanceStart','AlertEnd','ComputerKey','Lever','WireConnect','PuzzleFailed','PuzzleSolved','TrapArm','TrapTick','TrapDisarm']};
for(const [title,sequence] of Object.entries(flows)){const button=document.createElement('button');button.textContent=title;button.onclick=()=>{group.value='';show();const used={};queue=sequence.map(kind=>{const bank=[...main.querySelectorAll('audio')].filter(audio=>audio.dataset.kind===kind);const index=used[kind]||0;used[kind]=index+1;return bank[index%bank.length]}).filter(Boolean);next()};document.querySelector('#flows').append(button)}
document.querySelector('#sequence').onclick=()=>{stop();queue=[...document.querySelectorAll('audio')];next()};show();</script>""".replace("PAYLOAD", payload)
(report / "gallery.html").write_text(gallery, encoding="utf-8")
portable = [{**clip, "url": asset["file"]} for clip, asset in zip(cards, results)]
(output / "gallery.html").write_text(gallery.replace(payload, json.dumps(portable, ensure_ascii=False).replace("</", "<\\/")), encoding="utf-8")
(output / "CREDITS.md").write_text("\n".join(rows) + "\n", encoding="utf-8")
print(json.dumps({"assets": len(results), "sources": len(verified), "kinds": len(set(asset["kind"] for asset in results)), "bytes": sum(asset["bytes"] for asset in results), "clippedSamples": 0, "auditoryReview": "Pendiente"}, ensure_ascii=False))
