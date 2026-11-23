#!/usr/bin/env python3
"""drive-sounds.py — cut the drives' recorded sounds for the card
(design.md §10, drivesnd.h).

From recordings of a floppy drive at work, writes the two files the
firmware looks for in /coco/sounds/ on the card:

  step.wav    one step of the head: from just before its onset, 40 ms,
              faded out over its last 10 ms, its peak at --step-peak
  motor.wav   one revolution of the motor's noise, 200 ms at 300 rpm,
              from the steadiest stretch with no step in it, its end
              cross-faded into its start so that it loops without a
              seam, its RMS at --motor-rms

both 16-bit mono at 36,621 Hz, the PCM's rate (150 MHz / 4,096). The
recordings stay the user's: neither they nor what is cut from them go in
the tree (design.md §10). MP3 input is converted with afconvert (macOS)
or ffmpeg; WAV is read as it is, any rate, 8 or 16 bits.

  tools/drive-sounds.py out/audio/*.mp3 -o out/audio/card
  tools/drive-sounds.py a.mp3 b.mp3 --step b.mp3@10.31 --motor a.mp3@4.0

Without --step or --motor, the step is the loudest onset that stands
clear of the noise before it with no other onset within 100 ms, and the
motor the 200 ms whose loudness varies least, away from every onset.
What was chosen is printed, with its time, to choose again by.
"""

import argparse
import array
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave

RATE = 36621
MS = RATE / 1000.0


def read(path, tmp):
    """The file as a list of floats at RATE, mono."""
    if not path.lower().endswith(".wav"):
        out = os.path.join(tmp, os.path.basename(path) + ".wav")
        if shutil.which("afconvert"):
            cmd = ["afconvert", "-f", "WAVE", "-d", "LEI16@%d" % RATE, "-c", "1", path, out]
        elif shutil.which("ffmpeg"):
            cmd = ["ffmpeg", "-loglevel", "error", "-y", "-i", path, "-ac", "1", "-ar", str(RATE),
                   "-sample_fmt", "s16", out]
        else:
            sys.exit("%s: neither afconvert nor ffmpeg to convert it with" % path)
        subprocess.run(cmd, check=True)
        path = out
    with wave.open(path) as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        a = array.array("h", raw)
        if sys.byteorder == "big":
            a.byteswap()
        x = [float(v) for v in a]
    elif width == 1:
        x = [(b - 128) * 256.0 for b in raw]
    else:
        sys.exit("%s: %d-byte samples" % (path, width))
    if ch > 1:
        x = [sum(x[i:i + ch]) / ch for i in range(0, len(x) - ch + 1, ch)]
    if rate != RATE:   # linear, enough for noises
        step = rate / RATE
        x = [x[min(int(i * step), len(x) - 2)] * (1 - (i * step) % 1) +
             x[min(int(i * step) + 1, len(x) - 1)] * ((i * step) % 1)
             for i in range(int(len(x) / step))]
    return x


def peaks_ms(x):
    w = int(MS)
    return [max(abs(v) for v in x[i:i + w]) for i in range(0, len(x) - w, w)]


def onsets(e):
    """Milliseconds where a step starts: well over the 10 ms before."""
    out, i = [], 10
    while i < len(e):
        quiet = max(e[i - 10:i])
        if e[i] > max(1500.0, 2.5 * quiet):
            out.append((i, e[i] / max(quiet, 1.0), e[i]))
            i += 30
        else:
            i += 1
    return out


def at(spec, files):
    """FILE@SECONDS: the file's index and the time in ms."""
    name, _, t = spec.rpartition("@")
    for k, f in enumerate(files):
        if f == name or os.path.basename(f) == os.path.basename(name):
            return k, int(float(t) * 1000)
    sys.exit("%s: not one of the recordings" % name)


def write(path, x):
    a = array.array("h", [max(-32767, min(32767, int(round(v)))) for v in x])
    if sys.byteorder == "big":
        a.byteswap()
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(a.tobytes())


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    p.add_argument("recordings", nargs="+")
    p.add_argument("-o", "--out", default="out/drive-sounds")
    p.add_argument("--step", help="FILE@SECONDS, the step's onset")
    p.add_argument("--motor", help="FILE@SECONDS, the start of the motor's loop")
    p.add_argument("--step-peak", type=float, default=9000.0)
    p.add_argument("--motor-rms", type=float, default=700.0)
    a = p.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        xs = [read(f, tmp) for f in a.recordings]
    es = [peaks_ms(x) for x in xs]
    ons = [onsets(e) for e in es]

    # The step.
    if a.step:
        k, t = at(a.step, a.recordings)
    else:
        best = None
        for k, o in enumerate(ons):
            for j, (t, ratio, pk) in enumerate(o):
                clear = all(abs(t2 - t) >= 100 for t2, _, _ in o if t2 != t)
                if clear and (best is None or ratio * pk > best[0]):
                    best = (ratio * pk, k, t)
        if best is None:
            sys.exit("no step stands clear in the recordings: give --step")
        _, k, t = best
    x = xs[k]
    s0 = max(0, int((t - 2) * MS))
    step = x[s0:s0 + int(40 * MS)]
    mean = sum(step) / len(step)
    step = [v - mean for v in step]
    fade = int(10 * MS)
    for i in range(fade):
        step[len(step) - fade + i] *= 1.0 - (i + 1) / fade
    pk = max(abs(v) for v in step) or 1.0
    step = [v * a.step_peak / pk for v in step]
    print("step : %s@%.3f, %d samples, gain %.2f" %
          (a.recordings[k], t / 1000.0, len(step), a.step_peak / pk))

    # The motor: one revolution and the cross-fade's 20 ms.
    rev, xf = int(200 * MS), int(20 * MS)
    if a.motor:
        k, t = at(a.motor, a.recordings)
    else:
        best = None
        for k, e in enumerate(es):
            busy = set()
            for t0, _, _ in ons[k]:
                busy.update(range(t0 - 30, t0 + 60))
            med = sorted(e)[len(e) // 2]
            # Half a second in from each end, where a recording fades.
            for t in range(500, len(e) - 720, 10):
                if any(m in busy for m in range(t, t + 220)):
                    continue
                seg = e[t:t + 220]
                mu = sum(seg) / len(seg)
                var = sum((v - mu) ** 2 for v in seg) / len(seg) / (mu * mu or 1.0)
                score = var + abs(mu - med) / (med or 1.0)   # steady, and at its usual level
                if best is None or score < best[0]:
                    best = (score, k, t)
        if best is None:
            sys.exit("no 220 ms free of steps: give --motor")
        _, k, t = best
    x = xs[k]
    s0 = int(t * MS)
    seg = x[s0:s0 + rev + xf]
    if len(seg) < rev + xf:
        sys.exit("the motor's stretch runs past the end of its recording")
    mean = sum(seg) / len(seg)
    seg = [v - mean for v in seg]
    loop = seg[:rev]
    for i in range(xf):   # the tail laid over the head
        g = (i + 0.5) / xf
        loop[i] = seg[rev + i] * (1.0 - g) + seg[i] * g
    rms = math.sqrt(sum(v * v for v in loop) / len(loop)) or 1.0
    loop = [v * a.motor_rms / rms for v in loop]
    print("motor: %s@%.3f, %d samples, gain %.2f" %
          (a.recordings[k], t / 1000.0, len(loop), a.motor_rms / rms))

    os.makedirs(a.out, exist_ok=True)
    write(os.path.join(a.out, "step.wav"), step)
    write(os.path.join(a.out, "motor.wav"), loop)
    print("wrote %s/step.wav and motor.wav: copy them to /coco/sounds/ on the card" % a.out)


if __name__ == "__main__":
    main()
