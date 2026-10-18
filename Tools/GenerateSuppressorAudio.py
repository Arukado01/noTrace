import math
import pathlib
import random
import struct
import wave


def render(path, attach):
    rate = 22050
    duration = 0.42 if attach else 0.36
    rng = random.Random(8821 if attach else 9907)
    samples = []
    filtered = 0.0
    clicks = (0.012, 0.085, 0.16, 0.24, 0.35) if attach else (0.015, 0.05, 0.13, 0.25)
    for index in range(int(rate * duration)):
        time = index / rate
        white = rng.uniform(-1.0, 1.0)
        filtered = filtered * 0.78 + white * 0.22
        rotation = math.sin(math.pi * min(1.0, time / duration)) ** 1.4
        scratch = filtered * rotation * (0.12 if attach else 0.16)
        metal = 0.0
        for click_index, onset in enumerate(clicks):
            local = time - onset
            if local >= 0.0:
                frequency = (2300 if attach else 1700) + click_index * 95
                metal += math.exp(-local * 165) * (0.18 * white + 0.12 * math.sin(local * frequency * math.tau))
                metal += math.exp(-local * 65) * 0.045 * math.sin(local * 390 * math.tau)
        envelope = min(1.0, time / 0.004, (duration - time) / 0.018)
        samples.append(int(max(-0.98, min(0.98, (scratch + metal) * envelope)) * 32767))
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(rate)
        output.writeframes(struct.pack(f"<{len(samples)}h", *samples))


if __name__ == "__main__":
    directory = pathlib.Path(__file__).resolve().parents[1] / "Content" / "Sounds" / "NoTrace"
    render(directory / "SuppressorAttach.wav", True)
    render(directory / "SuppressorDetach.wav", False)
