#!/usr/bin/env python3
"""Lower case for OS-9 on the MC6847T1 (design.md §9.2): a copy of an OS-9
boot disk whose TERM descriptor has bit 0 of PAR set.

Tandy's OS-9 Level One (CCIO) and NitrOS-9's Level 1 (VTIO) set GM0, the
T1's lower case, in the alphanumeric mode when that bit is set as the
console starts; neither's TMODE reaches it. test_os9 boots both so, with
the same change made in memory. Select the T1 on the Setup page, or with
`vdg = 6847t1` in /coco/coco.ini, to see it.

    python3 -I tools/os9-lowercase.py IN.DSK OUT.DSK

Only the boot file LSN0 names (DD.BT, DD.BSZ) is searched, and only a
device descriptor named TERM whose driver is CCIO or VTIO is changed:
the descriptors under /MODULES are not booted, and a serial port's PAR
is its parity. A module is known by its sync bytes, header parity and
CRC. With none found, nothing is written. IN is never changed; OUT must
not exist.
"""
import os
import sys


def crc24(data):
    """OS-9's module CRC (F$CRC): a whole module comes to $800FE3."""
    c = 0xFFFFFF
    for b in data:
        c ^= b << 16
        for _ in range(8):
            c <<= 1
            if c & 0x1000000:
                c ^= 0x800063
        c &= 0xFFFFFF
    return c


def name_at(m, off):
    out = bytearray()
    for b in m[off:]:
        out.append(b & 0x7F)
        if b & 0x80:
            break
    return out.decode("latin-1")


def modules(img, lo, hi):
    """(offset, size, name, type) of every whole, valid module in [lo, hi)."""
    i = img.find(b"\x87\xcd", lo, hi)
    while i >= 0:
        h = img[i:i + 9]
        if len(h) == 9:
            par = 0
            for b in h:
                par ^= b
            size = h[2] << 8 | h[3]
            noff = h[4] << 8 | h[5]
            if par == 0xFF and 9 < noff < size and i + size <= hi:
                m = img[i:i + size]
                if crc24(m) == 0x800FE3:
                    yield i, size, name_at(m, noff), m[6]
        i = img.find(b"\x87\xcd", i + 1, hi)


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    src, dst = sys.argv[1], sys.argv[2]
    if os.path.exists(dst):
        print(f"{dst}: exists; not written", file=sys.stderr)
        return 1
    img = bytearray(open(src, "rb").read())
    # A JVC header is the size modulo 256; OS-9's LSNs follow it in order,
    # the sides of a track together, as JVC keeps them.
    head = len(img) % 256
    lsn0 = img[head:head + 256]
    bt = lsn0[0x15] << 16 | lsn0[0x16] << 8 | lsn0[0x17]
    bsz = lsn0[0x18] << 8 | lsn0[0x19]
    lo, hi = head + bt * 256, head + bt * 256 + bsz
    if bt == 0 or bsz == 0 or hi > len(img):
        print(f"{src}: LSN0 names no boot file; not written", file=sys.stderr)
        return 1
    done = 0
    for at, size, name, kind in list(modules(img, lo, hi)):
        # A device descriptor (type $F) named TERM for the console's
        # driver, PAR at $26 as SCF's descriptors have it.
        if name.upper() != "TERM" or kind & 0xF0 != 0xF0 or size < 0x2A:
            continue
        m = img[at:at + size]
        driver = name_at(m, m[0x0B] << 8 | m[0x0C]).upper()
        if driver not in ("CCIO", "VTIO"):
            print(f"{name} at ${at:06X}: driver {driver}, left as it is")
            continue
        was = m[0x26]
        m[0x26] |= 1
        c = crc24(m[:-3]) ^ 0xFFFFFF
        m[-3:] = bytes((c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF))
        assert crc24(m) == 0x800FE3
        img[at:at + size] = m
        print(f"{name} at ${at:06X}: PAR ${was:02X} -> ${m[0x26]:02X}")
        done += 1
    if done == 0:
        print(f"{src}: no console TERM in the boot file; not written", file=sys.stderr)
        return 1
    with open(dst, "xb") as f:
        f.write(img)
    print(f"{dst}: written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
