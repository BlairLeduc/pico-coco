#!/usr/bin/env python3
"""The disk archive run's report (design.md §15.4), from coco-corpus's
lines and the two pictures of each title.

    disk-compare.py disks.tsv SHOTS

For each title started, ours (SHOTS/<title>.ppm) against XRoar's
(SHOTS/<title>.xroar.ppm), taken at the same E cycle
(tools/archive-disks.sh). A text screen must match exactly; the
character cells that differ are counted, since a cursor caught on in
one and off in the other is one cell. A graphics screen may differ by
up to CLOSE of its pixels and still be called close. A title whose two
pictures are in different modes differs whatever its pixels.

The two disk models time a load differently, by seconds either way, so
coco-corpus -ref also compares ours with XRoar's picture at every field
within 10 s of the capture: a title whose picture matches XRoar's
exactly at some field there is the same picture at another time, and is
grouped so, with the distance; the others are judged at the capture,
with the nearest match beside them.

Then the titles grouped: started and matching, close, differing, no
XRoar snapshot, our own errors, no entry found, refused.
"""

import collections
import os
import re
import sys

FIELDS = ["name", "kind", "outcome", "drives", "bytes", "undoc", "ops", "split",
          "changed", "lines", "mode", "note", "regions", "dropped", "joyr", "joyl", "cols", "cmd",
          "at_px", "best_px", "best_off"]

CLOSE = 0.01       # of the picture's pixels, for a graphics screen
W, H = 256, 192    # the active picture (design.md §9)


def load(path):
    rows = []
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        for line in f:
            rows.append(dict(zip(FIELDS, line.rstrip("\n").split("\t"))))
    return rows


def ppm(path):
    """(width, height, rgb bytes) of a binary PPM, or None."""
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    toks, i = [], 0
    while len(toks) < 4:
        while data[i:i + 1].isspace():
            i += 1
        if data[i:i + 1] == b"#":
            while data[i:i + 1] not in (b"\n", b""):
                i += 1
            continue
        j = i
        while not data[j:j + 1].isspace():
            j += 1
        toks.append(data[i:j])
        i = j
    w, h = int(toks[1]), int(toks[2])
    return w, h, data[i + 1:]


def vdg_mode(txt):
    try:
        with open(txt, encoding="utf-8", errors="replace") as f:
            m = re.search(r"VDG mode \$([0-9A-F]{2})", f.read())
    except OSError:
        return None
    return int(m.group(1), 16) if m else None


def compare(a, b):
    """(pixels differing, text cells differing) of the active picture, which
    the frame holds centred in its border."""
    wa, ha, da = a
    wb, hb, db = b
    if (wa, ha) != (wb, hb):
        return None, None
    x0, y0 = (wa - W) // 2, (ha - H) // 2
    px = 0
    cells = set()
    for y in range(H):
        ra = (y0 + y) * wa * 3 + x0 * 3
        la, lb = da[ra:ra + W * 3], db[ra:ra + W * 3]
        if la == lb:
            continue
        for x in range(W):
            if la[x * 3:x * 3 + 3] != lb[x * 3:x * 3 + 3]:
                px += 1
                cells.add((y // 12, x // 8))
    return px, len(cells)


def main():
    sys.stdout.reconfigure(errors="surrogateescape")
    tsv, shots = sys.argv[1:3]
    rows = load(tsv)
    groups = collections.defaultdict(list)
    for r in rows:
        name = r["name"]
        if r["outcome"] in ("refused", "no-entry"):
            groups[r["outcome"]].append((r, r["note"]))
            continue
        ours = ppm(os.path.join(shots, name + ".ppm"))
        theirs = ppm(os.path.join(shots, name + ".xroar.ppm"))
        if theirs is None:
            groups["no-snapshot"].append((r, "XRoar wrote no snapshot"))
            continue
        mo = vdg_mode(os.path.join(shots, name + ".txt"))
        mx = vdg_mode(os.path.join(shots, name + ".xroar.txt"))
        px, cells = compare(ours, theirs)
        # coco-corpus's own count, of our field's end state against
        # XRoar's, is the judge: the saved picture of a title that splits
        # its screen shows the changes along the field, which a snapshot
        # cannot (§9.4).
        if px is not None and r.get("at_px", "-") not in ("-", None):
            px = int(r["at_px"])
        text = mo is not None and not (mo & 0x80)
        what = f"ours ${mo:02X}, XRoar's ${mx:02X}" if mo is not None and mx is not None else "?"
        best = int(r["best_px"]) if r.get("best_px", "-") not in ("-", None) else None
        off = int(r["best_off"]) if best is not None else 0
        near = f"; nearest {best} pixels at {off / 60:+.1f} s" if best is not None else ""
        if px is not None and px != 0 and best == 0:
            groups["timing"].append((r, f"{'text' if text else 'graphics'}, {px} pixels at the capture, "
                                        f"the same picture {off / 60:+.1f} s from it"))
            continue
        if px is None:
            groups["differ"].append((r, "frames of different sizes"))
        elif mo != mx:
            groups["differ"].append((r, f"modes differ: {what}; {px} pixels{near}"))
        elif px == 0:
            groups["same"].append((r, "text" if text else "graphics"))
        elif text:
            groups["differ-text"].append((r, f"{cells} cell(s), {px} pixels{near}"))
        elif px <= CLOSE * W * H:
            groups["close"].append((r, f"{px} pixels ({100.0 * px / (W * H):.2f}%){near}"))
        else:
            groups["differ"].append((r, f"graphics, {px} pixels ({100.0 * px / (W * H):.1f}%), mode ${mo:02X}{near}"))

    print(f"{len(rows)} titles from {tsv}\n")
    entry = collections.Counter(r["note"].split(";")[0] for r in rows
                                if r["outcome"] not in ("refused", "no-entry"))
    print("How they were started: " + ", ".join(f"{k} {v}" for k, v in entry.most_common()))
    outc = collections.Counter(r["outcome"] for r in rows)
    print("Our outcome:           " + ", ".join(f"{k} {v}" for k, v in outc.most_common()))
    print()
    order = [("same", "The same picture as XRoar's"),
             ("timing", "The same picture as XRoar's, at another time within 10 s"),
             ("close", f"Graphics within {CLOSE:.0%} of XRoar's"),
             ("differ-text", "A text screen that differs from XRoar's"),
             ("differ", "Pictures that differ"),
             ("no-snapshot", "No picture from XRoar"),
             ("no-entry", "No entry point found: listed, not guessed at"),
             ("refused", "Refused")]
    print("Summary")
    for key, title in order:
        print(f"  {len(groups[key]):4d}  {title}")
    for key, title in order:
        g = groups[key]
        if not g or key == "same":
            continue
        print(f"\n{title} ({len(g)})")
        for r, why in sorted(g, key=lambda t: t[0]["name"].lower()):
            cmd = f" [{r['cmd']}]" if r.get("cmd") not in (None, "", "-") else ""
            extra = f", ours {r['outcome']}" if key not in ("no-entry", "refused") else ""
            print(f"  {r['name']}{cmd}: {why}{extra}")
    undoc = [r for r in rows if r.get("undoc", "0") not in ("0", "")]
    if undoc:
        print(f"\nUndocumented opcodes reached (D12) ({len(undoc)})")
        for r in undoc:
            print(f"  {r['name']}: {r['ops']}")
    split = [r for r in rows if r.get("split", "0") not in ("0", "")]
    if split:
        print(f"\nVideo bits changed on active lines (§9.4) ({len(split)})")
        for r in split:
            print(f"  {r['name']}: {r['split']} fields, {r['changed']}, lines {r['lines']}")


if __name__ == "__main__":
    main()
