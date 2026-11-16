#!/usr/bin/env python3
"""The archive run's report (design.md §15.4), from coco-corpus's lines.

    archive-summary.py CORPUS archive.tsv

Outcomes by kind; the titles that reached an undocumented opcode or
postbyte, and which, for D12; the titles whose fields changed the video
bits during the active lines, how often and which bits, for §9.4; the
largest file and tape, which size the buffers; and the images refused.
"""

import collections
import os
import sys

FIELDS = ["name", "kind", "outcome", "blocks", "largest", "undoc", "ops", "split",
          "changed", "lines", "mode", "note"]

# A title whose fields change mode mid-picture this often in a minute
# does it as a matter of course, not once at a change of screen.
OFTEN = 60


def load(path):
    rows = []
    with open(path, encoding="utf-8", errors="surrogateescape") as f:
        for line in f:
            rows.append(dict(zip(FIELDS, line.rstrip("\n").split("\t"))))
    return rows


def ops(r):
    """{opcode: (count, first pc)} from "$AB:3@$1234,..."."""
    out = {}
    if r["ops"] != "-":
        for o in r["ops"].split(","):
            op, rest = o.split(":")
            n, pc = rest.split("@")
            out[op] = (int(n), pc)
    return out


def main():
    sys.stdout.reconfigure(errors="surrogateescape")
    corpus, tsv = sys.argv[1:3]
    rows = load(tsv)
    print(f"{len(rows)} images from {corpus}\n")

    print("Outcome by kind")
    for kind in ("ccc", "cas", "cas.gz"):
        c = collections.Counter(r["outcome"] for r in rows if r["kind"] == kind)
        if c:
            print(f"  {kind:7s} {sum(c.values()):5d}: " +
                  ", ".join(f"{k} {v}" for k, v in sorted(c.items(), key=lambda kv: -kv[1])))

    bad = [r for r in rows if r["outcome"] in ("no-load", "io-error", "gave-up", "refused")]
    print(f"\nNot loaded ({len(bad)})")
    for r in bad:
        print(f"  {r['name']} [{r['kind']}]: {r['outcome']}, {r['blocks']} blocks, {r['note']}")

    undoc = [r for r in rows if r["undoc"] != "0"]
    by_op = collections.defaultdict(list)
    for r in undoc:
        for op, (n, pc) in ops(r).items():
            by_op[op].append((r["name"], n, pc))
    print(f"\nUndocumented opcodes and postbytes (D12): {len(undoc)} titles")
    for op in sorted(by_op):
        titles = by_op[op]
        print(f"  {op}: {len(titles)} titles, {sum(n for _, n, _ in titles)} times")
        for name, n, pc in sorted(titles):
            print(f"      {name}: {n} from {pc}")

    split = [r for r in rows if int(r["split"]) > 0]
    often = [r for r in split if int(r["split"]) >= OFTEN]
    bits = collections.Counter(b for r in split for b in r["changed"].split(",") if b != "-")
    print(f"\nFields with a change on an active line (§9.4): {len(split)} titles, "
          f"{len(often)} of them in {OFTEN} fields or more")
    print("  bits changed, by titles: " + ", ".join(f"{k} {v}" for k, v in bits.most_common()))
    for r in sorted(often, key=lambda r: -int(r["split"])):
        print(f"  {r['name']} [{r['kind']}]: {r['split']} fields, {r['changed']}, lines {r['lines']}")

    tapes = [r for r in rows if r["kind"] == "cas"]
    if tapes:
        lf = max(tapes, key=lambda r: int(r["largest"]))
        print(f"\nLargest file on a tape: {lf['largest']} bytes ({lf['name']})")
        sizes = []
        for r in tapes:
            p = os.path.join(corpus, "cas", r["name"] + ".cas")
            if os.path.isfile(p):
                sizes.append((os.path.getsize(p), r["name"]))
            elif os.path.isdir(os.path.join(corpus, "cas", r["name"])):
                d = os.path.join(corpus, "cas", r["name"])
                sizes.append((sum(os.path.getsize(os.path.join(d, f)) for f in os.listdir(d)), r["name"]))
        if sizes:
            n, name = max(sizes)
            over = sum(1 for s, _ in sizes if s > 65536)
            print(f"Largest tape: {n} bytes ({name}); {over} tapes over 64 KB")


if __name__ == "__main__":
    main()
