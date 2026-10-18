"""Verifica hashes, procedencia, PCM y cobertura del paquete sin acreditar escucha."""

import hashlib
import json
from pathlib import Path
import re
import struct
import wave

ROOT = Path(__file__).resolve().parents[2]
directory = ROOT / "Content/Audio/NoTrace"
manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
recipes = json.loads((ROOT / "Tools/Audio/Library.json").read_text(encoding="utf-8"))["assets"]
sources = {item["id"]: item for item in json.loads((ROOT / "Tools/Audio/Sources.json").read_text(encoding="utf-8"))["sources"]}
rejected = ("Kenney", "HaelDB", "/72030/", "/316919/", "/316922/", "/788342/")
assert all(not any(value.lower() in (source["sourceUrl"] + source["author"]).lower() for value in rejected) for source in sources.values()), "La biblioteca incluye una fuente rechazada"
assert {item["file"] for item in recipes} == {item["file"] for item in manifest["assets"]}
catalog = (ROOT / "Games/Include/Config/NoTraceAudioCatalog.h").read_text(encoding="utf-8")
assert set(re.findall(r'"([^"\n]+\.wav)"', catalog)) == {item["file"] for item in recipes}, "El catálogo C++ y las recetas divergen"
assert {path.relative_to(directory).as_posix() for path in directory.rglob("*.wav")} == {item["file"] for item in recipes}, "WAV fuera del catálogo activo"
fingerprints = set()
for asset in manifest["assets"]:
    path = (directory / asset["file"]).resolve()
    assert path.is_relative_to(directory) and path.is_file()
    assert hashlib.file_digest(path.open("rb"), "sha256").hexdigest() == asset["sha256"]
    assert asset["sourceInfo"] == sources[asset["source"]] and asset["sourceInfo"]["license"] == "CC0-1.0"
    with wave.open(str(path)) as audio:
        assert audio.getsampwidth() == 2 and audio.getframerate() == 48000
        assert audio.getnchannels() == (2 if asset["loop"] else 1)
        pcm = audio.readframes(audio.getnframes())
    fingerprint = hashlib.sha256(pcm).hexdigest()
    assert fingerprint == asset["pcmSha256"] and fingerprint not in fingerprints
    fingerprints.add(fingerprint)
    peak = max(abs(sample[0]) for sample in struct.iter_unpack("<h", pcm))
    assert 0 < peak < 32440 and asset["clippedSamples"] == 0
print(json.dumps({"result": "PASS", "assets": len(fingerprints), "sourceRecordings": len(sources), "sampleRate": 48000, "auditoryAcceptance": False}))
