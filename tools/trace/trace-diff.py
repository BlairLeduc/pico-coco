#!/usr/bin/env python3
"""trace-diff.py — hold this core's trace to XRoar's (design.md §15.2,
EL §11.4).

    tools/trace/trace-diff.py xroar.trace ours.trace

Each side is a line per instruction: its address, then every register
after it. XRoar's -trace lines carry the bytes and the mnemonic between,
which are kept for the report; its vector lines ([RESET], [IRQ] ...),
which carry no registers, and everything else are skipped. Every line
of ours is compared: the first instruction that differs, in address or
in any register, is reported with what led to it. XRoar is run for longer
than this core, so a XRoar trace shorter than ours, or either side
empty, means a run that stopped early and checked less than it should,
and fails. Exit 0 if they agree, 1 if not.
"""
import re
import sys

REGS = r"(cc=[0-9a-f]{2} a=[0-9a-f]{2} b=[0-9a-f]{2} dp=[0-9a-f]{2} " \
       r"x=[0-9a-f]{4} y=[0-9a-f]{4} u=[0-9a-f]{4} s=[0-9a-f]{4})"
LINE = re.compile(r"^([0-9a-f]{4})\|(.*?)" + REGS + r"\s*$")


def load(path):
    out = []
    with open(path, errors="replace") as f:
        for raw in f:
            m = LINE.match(raw.rstrip("\n"))
            if m:
                out.append((m.group(1), m.group(3), m.group(2).strip()))
    return out


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    xr, ours = load(sys.argv[1]), load(sys.argv[2])
    if not xr or not ours:
        print(f"no trace: XRoar {len(xr)} lines, ours {len(ours)}")
        return 1
    n = min(len(xr), len(ours))
    for i in range(n):
        if xr[i][:2] != ours[i][:2]:
            print(f"diverged at instruction {i} of {n} compared")
            for j in range(max(0, i - 12), i):
                print(f"   {xr[j][0]}  {xr[j][2]:<28} {xr[j][1]}")
            print(f"xr {xr[i][0]}  {xr[i][2]:<28} {xr[i][1]}")
            print(f"us {ours[i][0]}  {'':<28} {ours[i][1]}")
            return 1
    if len(xr) < len(ours):
        print(f"agree for {n}, but XRoar's trace stopped early "
              f"(XRoar {len(xr)}, ours {len(ours)})")
        return 1
    print(f"agree: {n} instructions (XRoar {len(xr)}, ours {len(ours)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
