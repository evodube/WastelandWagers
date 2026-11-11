#!/usr/bin/env python3
"""Writes fo4roulette's sounds (Data/Sound/fo4roulette/<bank>_<n>.wav, mono 16-bit 44.1 kHz).

    python tools/make_sounds.py <out Data folder>

The ball's sounds are cut from a real recording, tools/source_sounds/roulette_gfxsounds.wav (Maxwell Dufresne,
gfxsounds.com, Standard licence), into pieces the plugin plays as the ball does each thing:
  roll     its first 3.95 s, the ball running round the track, looped out to cover a long spin (the plugin shapes
           its loudness to the ball's speed and stops it as the ball reaches the rotor)
  rattle   the three hard hits in the drop (a diamond, a fret)
  tick     the small knocks after them (the ball's little bounces about the rotor and pockets)
  settle   the last rattles as it rocks into place
The rest are synthesised here: chips (caps), the rotor's whirr.
"""

import os
import sys
import wave

import numpy as np

RATE = 44100
rng = np.random.default_rng(7)


def t(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def bandpass(x, lo, hi):
    """a crude band-pass: FFT mask with soft edges"""
    n = len(x)
    f = np.fft.rfftfreq(n, 1 / RATE)
    X = np.fft.rfft(x)
    mask = 1 / (1 + ((lo / np.maximum(f, 1)) ** 4)) * 1 / (1 + ((f / hi) ** 4))
    return np.fft.irfft(X * mask, n)


def click(freq, decay, length=0.08, noise=0.5):
    tt = t(length)
    tone = np.sin(2 * np.pi * freq * tt + rng.uniform(0, 6.28)) * np.exp(-tt / decay)
    hiss = bandpass(rng.normal(0, 1, len(tt)), freq * 0.5, freq * 2.0) * np.exp(-tt / (decay * 0.6))
    return tone * (1 - noise) + hiss * noise * 0.6


def place(buf, sound, at, gain=1.0):
    i = int(at * RATE)
    n = min(len(sound), len(buf) - i)
    if n > 0:
        buf[i:i + n] += sound[:n] * gain


def write(path, x, peak=0.85):
    x = x / max(1e-9, np.max(np.abs(x))) * peak
    data = (np.clip(x, -1, 1) * 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(data.tobytes())


def chip():
    buf = np.zeros(int(0.15 * RATE))
    place(buf, click(rng.uniform(2600, 3400), 0.012, noise=0.7), 0.0)
    place(buf, click(rng.uniform(3000, 4200), 0.008, noise=0.8), rng.uniform(0.02, 0.04), 0.5)
    return buf


def rattle():
    buf = np.zeros(int(0.25 * RATE))
    at, g = 0.0, 1.0
    for _ in range(rng.integers(1, 4)):
        place(buf, click(rng.uniform(4500, 6500), 0.006, noise=0.4), at, g)
        at += rng.uniform(0.03, 0.07)
        g *= 0.5
    return buf


def settle():
    buf = np.zeros(int(0.6 * RATE))
    place(buf, click(5200, 0.01, noise=0.5), 0.0)
    at, g = 0.05, 0.6
    for _ in range(5):
        place(buf, click(rng.uniform(5000, 6200), 0.004, noise=0.5), at, g)
        at += rng.uniform(0.03, 0.06) * (1 - g * 0.3)
        g *= 0.6
    return buf


def roll(seconds=9.0):
    tt = t(seconds)
    noise = bandpass(rng.normal(0, 1, len(tt)), 400, 3200)
    # the ball passing over the track's seams: a pulse that slows as the ball does
    rate = 9.0 * np.exp(-tt / 7.0) + 2.0
    phase = np.cumsum(rate) / RATE
    pulse = 0.7 + 0.3 * np.sin(2 * np.pi * phase) ** 2
    env = np.minimum(1, tt / 0.15) * (0.6 + 0.4 * np.exp(-tt / 5.0))
    whir = np.sin(2 * np.pi * np.cumsum(180 + 120 * np.exp(-tt / 4.0)) / RATE) * 0.15
    return (noise * pulse + whir) * env


def spin():
    tt = t(1.6)
    noise = bandpass(rng.normal(0, 1, len(tt)), 150, 1200)
    env = np.minimum(1, tt / 0.08) * np.exp(-tt / 0.7)
    knock = click(900, 0.02, 0.1, noise=0.3)
    out = noise * env * 0.7
    place(out, knock, 0.0, 1.5)
    return out


def win():
    buf = np.zeros(int(1.2 * RATE))
    for _ in range(9):
        at = rng.uniform(0.0, 0.5)
        f = rng.uniform(2800, 5200)
        tt = t(0.4)
        ring = (np.sin(2 * np.pi * f * tt) + 0.4 * np.sin(2 * np.pi * f * 2.7 * tt)) * np.exp(-tt / rng.uniform(0.05, 0.15))
        place(buf, ring, at, rng.uniform(0.3, 1.0))
    return buf


def turn():
    """your turn at a card table: a soft two-note chime, low and short so it doesn't startle"""
    buf = np.zeros(int(0.9 * RATE))
    for at, f, g in ((0.0, 1046.5, 0.8), (0.13, 1568.0, 1.0)):  # C6 then G6
        tt = t(0.7)
        bell = (np.sin(2 * np.pi * f * tt) + 0.25 * np.sin(2 * np.pi * f * 2.0 * tt) + 0.08 * np.sin(2 * np.pi * f * 3.01 * tt))
        bell *= np.exp(-tt / 0.22) * np.minimum(1.0, tt / 0.004)
        place(buf, bell, at, g)
    return buf


SOURCE = os.path.join(os.path.dirname(__file__), "source_sounds", "roulette_gfxsounds.wav")
SPLIT = 3.95  # seconds: the roll ends and the drop begins


def recording():
    with wave.open(SOURCE, "rb") as w:
        x = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float64) / 32768
        x = x.reshape(-1, w.getnchannels()).mean(axis=1)
    return x


def real_roll(seconds=10.0):
    """the recorded roll, looped with crossfades to last a whole spin's time on the track (the plugin cuts it short)"""
    x = recording()[int(0.05 * RATE):int(SPLIT * RATE)]
    fade = int(0.4 * RATE)
    out = x.copy()
    ramp = np.linspace(0, 1, fade)
    while len(out) < seconds * RATE:
        out[-fade:] = out[-fade:] * (1 - ramp) + x[:fade] * ramp
        out = np.concatenate([out, x[fade:]])
    out = out[:int(seconds * RATE)]
    out[:int(0.05 * RATE)] *= np.linspace(0, 1, int(0.05 * RATE))
    return out


HITS = [(4.190, 0.120), (4.370, 0.080), (5.543, 0.088)]   # (onset, length) seconds
TICKS = [4.454, 4.571, 4.717, 4.765, 4.855, 4.987, 5.376, 5.633, 5.837, 6.085, 6.133]


def cut(x, at, length, pre=0.003):
    """one knock: from just before its onset, faded out at the end, levelled"""
    a = int((at - pre) * RATE)
    piece = x[a:a + int((length + pre) * RATE)].copy()
    piece -= np.mean(piece)
    fi = int(0.002 * RATE)
    piece[:fi] *= np.linspace(0, 1, fi)
    fo = min(len(piece) // 2, int(0.025 * RATE))
    piece[-fo:] *= np.linspace(1, 0, fo) ** 2
    return piece


def real_hits():
    x = recording()
    return [cut(x, a, n) for a, n in HITS]


def real_ticks():
    x = recording()
    out = []
    for k, a in enumerate(TICKS):
        nxt = TICKS[k + 1] if k + 1 < len(TICKS) else a + 0.1
        out.append(cut(x, a, min(0.075, nxt - a - 0.004)))
    return out


def real_settle():
    x = recording()
    piece = x[int(5.80 * RATE):int(6.50 * RATE)].copy()
    piece[:int(0.01 * RATE)] *= np.linspace(0, 1, int(0.01 * RATE))
    piece[-int(0.2 * RATE):] *= np.linspace(1, 0, int(0.2 * RATE))
    return piece


def main() -> int:
    out = os.path.join(sys.argv[1], "Sound", "fo4roulette")
    os.makedirs(out, exist_ok=True)
    for old in os.listdir(out):
        if old.endswith(".wav"):
            os.remove(os.path.join(out, old))
    banks = {"chip": (chip, 4), "spin": (spin, 2), "turn": (turn, 1)}
    if os.path.exists(SOURCE):
        banks["roll"] = (real_roll, 1)
        sets = {"rattle": real_hits(), "tick": real_ticks(), "settle": [real_settle()]}
    else:
        banks["roll"] = (roll, 1)
        banks["rattle"] = (rattle, 6)
        banks["settle"] = (settle, 2)
        sets = {}
    for name, (make, n) in banks.items():
        for i in range(1, n + 1):
            write(os.path.join(out, f"{name}_{i}.wav"), make())
    for name, pieces in sets.items():
        for i, piece in enumerate(pieces, 1):
            write(os.path.join(out, f"{name}_{i}.wav"), piece, peak=0.9)
    with open(os.path.join(out, "CREDITS.txt"), "w") as f:
        f.write("roll, rattle, tick and settle: cut from a roulette recording by Maxwell Dufresne, gfxsounds.com (https://gfxsounds.com), "
                "Standard licence.\n"
                "The other sounds are synthesised by tools/make_sounds.py.\n")
    print("wrote", len(os.listdir(out)), "files to", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
