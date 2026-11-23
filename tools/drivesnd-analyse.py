#!/usr/bin/env python3
"""drivesnd-analyse.py — a drive's sound measured, to set the
synthesised one beside recordings (design.md §10).

For each group of WAV files (16-bit mono at 36,621 Hz; tools/
drive-sounds.py's conversion, or test_drivesnd --calib's output):
the noise between steps, its RMS and its spectrum in bands relative to
its 1 kHz band; and the steps, their RMS over the noise a millisecond
at a time, and their excess over the noise by band. Python alone, no
NumPy: slow, but a few seconds a file.

  tools/drivesnd-analyse.py out/audio/wav/disc*.wav -- out/calib/*.wav
"""

import array
import cmath
import math
import sys
import wave

R = 36621
EDGES = [200, 300, 400, 600, 800, 1000, 1300, 1600, 2000, 2500, 3200, 4000, 5000, 6300, 8000, 10000]


def load(f):
    with wave.open(f) as w:
        assert w.getframerate() == R and w.getnchannels() == 1 and w.getsampwidth() == 2, f
        return [float(v) for v in array.array("h", w.readframes(w.getnframes()))]


def fft(x):
    n = len(x)
    if n == 1:
        return [complex(x[0])]
    e, o = fft(x[0::2]), fft(x[1::2])
    t = [cmath.exp(-2j * math.pi * k / n) * o[k] for k in range(n // 2)]
    return [e[k] + t[k] for k in range(n // 2)] + [e[k] - t[k] for k in range(n // 2)]


def psd(seg):
    n = len(seg)
    w = [0.5 - 0.5 * math.cos(2 * math.pi * i / (n - 1)) for i in range(n)]
    X = fft([s * wi for s, wi in zip(seg, w)])
    return [abs(X[k]) ** 2 / n for k in range(n // 2)]


def bands(p, n):
    out = []
    for lo, hi in zip(EDGES, EDGES[1:] + [12000]):
        a = int(lo * n / R)
        b = max(a + 1, int(hi * n / R))
        out.append(10 * math.log10(sum(p[a:b]) / (b - a) + 1e-9))
    return out


def ms_peaks(x):
    return [max(abs(v) for v in x[i:i + 37]) for i in range(0, len(x) - 37, 37)]


def onsets(e):
    out, i = [], 10
    while i < len(e):
        q = max(e[i - 10:i])
        if e[i] > max(1500, 2.5 * q):
            out.append(i)
            i += 30
        else:
            i += 1
    return out


def measure(files):
    N, Ns = 2048, 512
    bg, nb, sp, env, segs, sq, nsq = [0.0] * (N // 2), 0, [0.0] * (Ns // 2), [0.0] * 50, 0, 0.0, 0
    for f in files:
        x = load(f)
        e = ms_peaks(x)
        on = onsets(e)
        busy = set()
        for t in on:
            busy.update(range(t - 20, t + 80))
        for s in range(0, len(x) - N, N):
            if any((m // 37) in busy for m in range(s, s + N, 37)):
                continue
            seg = x[s:s + N]
            bg = [a + b for a, b in zip(bg, psd(seg))]
            sq += sum(v * v for v in seg)
            nsq += N
            nb += 1
        for t in on:
            s0 = int((t - 1) * 37)
            seg = x[s0:s0 + int(0.06 * R)]
            if len(seg) < int(0.06 * R):
                continue
            sp = [a + b for a, b in zip(sp, psd(seg[:Ns]))]
            for m in range(50):
                c = seg[m * 37:(m + 1) * 37]
                env[m] += math.sqrt(sum(v * v for v in c) / 37)
            segs += 1
    bg = [v / max(nb, 1) for v in bg]
    rms = math.sqrt(sq / max(nsq, 1))
    out = {"rms": rms, "steps": segs, "bg": bands(bg, N)}
    if segs:
        sp = [v / segs for v in sp]
        bgs = [sum(bg[k * 4:k * 4 + 4]) / 4 for k in range(Ns // 2)]
        out["excess"] = [a - b for a, b in zip(bands(sp, Ns), bands(bgs, Ns))]
        out["env"] = [max(0.0, v / segs - rms) for v in env]
    return out


def main():
    args = sys.argv[1:]
    groups = [[]]
    for a in args:
        if a == "--":
            groups.append([])
        else:
            groups[-1].append(a)
    ms = [measure(g) for g in groups if g]
    print("noise RMS:   " + "  ".join("%8.0f" % m["rms"] for m in ms))
    print("steps:       " + "  ".join("%8d" % m["steps"] for m in ms))
    print("noise by band, dB from its 1 kHz band; step's excess over it, dB")
    k1 = EDGES.index(1000)
    for i, lo in enumerate(EDGES):
        row = "  %5d Hz " % lo
        for m in ms:
            row += "  %6.1f" % (m["bg"][i] - m["bg"][k1])
            row += " %5.1f" % m["excess"][i] if "excess" in m else "      "
        print(row)
    print("a step's RMS over the noise, by ms (each group):")
    for m in ms:
        if "env" in m:
            print("  " + " ".join("%d" % v for v in m["env"]))


if __name__ == "__main__":
    main()
