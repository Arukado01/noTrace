"""Monta grabaciones existentes para escucha humana; no genera sustitutos sonoros."""

import hashlib
import json
from pathlib import Path
import struct
import wave

ROOT = Path(__file__).resolve().parents[3]
OUTPUT = Path(__file__).resolve().parent
DIRECTORY = ROOT / "Content/Audio/NoTrace"
manifest = json.loads((DIRECTORY / "manifest.json").read_text(encoding="utf-8"))
by_kind = {}
for asset in manifest["assets"]:
    by_kind.setdefault(asset["kind"], []).append(asset)

sequences = {
    "01-sigilo-materiales": ["AmbientRoom", "StepHard", "StepMetal", "StepStair", "StepSoft", "StepCrouchHard", "StepCrouchMetal", "StepCrouchStair", "StepCrouchSoft", "ProneCrawl", "CoverEnter", "Hideout", "Drag", "CoverExit"],
    "02-armas-contacto": ["WeaponReady", "WeaponSwitch", "DryFire", "Reload", "ReloadFinish", "Pistol", "Suppressed", "Rifle", "NonLethal", "SuppressedNonLethal", "SuppressorAttach", "SuppressorDetach", "SuppressorBreak", "ImpactBody", "Punch", "Kick", "Grab", "Push", "Fall"],
    "03-verticalidad": ["GripAttach", "GripMove", "GripRelease", "StepStair", "StepRunStair", "StepCrouchStair", "ElevatorClose", "ElevatorStart", "ElevatorMove", "ElevatorStop", "ElevatorOpen", "Land", "GetUp", "Dive", "Roll"],
    "04-dispositivos-alerta": ["AmbientElectrical", "DoorOpen", "DoorClose", "CabinetOpen", "CabinetClose", "TableFlip", "Lock", "ComputerKey", "WireConnect", "Lever", "PuzzleSolved", "TrapArm", "TrapDisarm", "DetectionConfirmed", "SearchStart", "VigilanceStart", "AlertEnd", "Wake", "Death"],
}

rate = 48000
gap_frames = int(rate * .25)
results = []
for name, kinds in sequences.items():
    pcm = bytearray()
    timeline = []
    for kind in kinds:
        variants = by_kind[kind]
        for asset in variants[:2] if kind == "DryFire" else variants[:1]:
            path = DIRECTORY / asset["file"]
            assert hashlib.file_digest(path.open("rb"), "sha256").hexdigest() == asset["sha256"]
            with wave.open(str(path)) as audio:
                assert audio.getframerate() == rate and audio.getsampwidth() == 2
                channels = audio.getnchannels()
                limit = min(audio.getnframes(), int(4 * rate)) if asset["loop"] else audio.getnframes()
                data = audio.readframes(limit)
            if channels == 2:
                values = struct.unpack("<" + "h" * (len(data) // 2), data)
                data = b"".join(struct.pack("<h", round((left + right) / 2)) for left, right in zip(values[::2], values[1::2]))
            start = len(pcm) / (rate * 2)
            pcm.extend(data)
            timeline.append({"kind": kind, "file": asset["file"], "startSeconds": start, "durationSeconds": len(data) / (rate * 2), "originalSha256": asset["sha256"], "source": asset["source"], "excerpt": asset["loop"]})
            pcm.extend(bytes(gap_frames * 2))
    duration = len(pcm) / (rate * 2)
    if duration < 15:
        pcm.extend(bytes(round((15 - duration) * rate) * 2))
    assert 15 <= len(pcm) / (rate * 2) <= 30, name
    destination = OUTPUT / (name + ".wav")
    with wave.open(str(destination), "wb") as audio:
        audio.setnchannels(1)
        audio.setsampwidth(2)
        audio.setframerate(rate)
        audio.writeframes(pcm)
    results.append({"file": destination.name, "durationSeconds": len(pcm) / (rate * 2), "bytes": destination.stat().st_size, "sha256": hashlib.file_digest(destination.open("rb"), "sha256").hexdigest(), "timeline": timeline})

(OUTPUT / "manifest.json").write_text(json.dumps({"purpose": "Secuencias de grabaciones reales para revisión humana; no mezcla ni grabación de gameplay", "processing": "Concatenación PCM, pausas de 250 ms y reducción de estéreo a mono sólo en estos montajes; ambientes limitados a cuatro segundos; sin tonos ni ruido sintetizados", "auditoryAcceptance": False, "reviewer": None, "montages": results}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print(json.dumps({"montages": len(results), "seconds": sum(item["durationSeconds"] for item in results), "auditoryAcceptance": False}))
