#!/usr/bin/env python3
"""An OS-9 boot disk whose clock reads the SmartWatch (design.md §12.7):
a copy of BOOT with OS9Boot's Clock module replaced by clock_tc3 from
the /MODULES directory of Tandy OS-9 Level One 2.01's second disk.

clock_tc3 reads the date from a DS1216 SmartWatch at $FF7C, which this
emulator fits with `smartwatch = on` (or the Setup page), set from the
board's DS3231. test_os9 boots a disk made so.

    python3 -I tools/os9-clock.py [--drop NAME,...] BOOT.DSK MODULES.DSK OUT.DSK

  BOOT     a Tandy OS-9 Level One 2.01 boot disk (OS9-L1V201M.DSK, or a
           copy tools/os9-lowercase.py made of it)
  MODULES  the second 2.01 disk, OS9-L1V201B.DSK, for /MODULES/clock_tc3
  OUT      the new disk; it must not exist, and BOOT is never changed
  --drop   modules left out of OS9Boot, by name, to make room

OS9Boot must be one run of sectors, as the boot track's loader reads it
by LSN0's DD.BT and DD.BSZ. clock_tc3 is 85 bytes longer than Clock, so
when the file's sectors cannot hold it, it is moved to the first free run
long enough, by the allocation map, and its old sectors freed. The 2.01
system disk has neither: its OS9Boot is 54 bytes short and only 49
sectors are free. `--drop T1,RS232` leaves out the bit-banger serial
port's descriptor and driver, which this emulator has no port for
(design.md §20), and OS9Boot then fits where it is. The
startup file's SETIME line, which would ask for the time and set the
SmartWatch to whatever is typed, becomes a comment: '* SETIME ...'.
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
        if b == 0:
            break
        out.append(b & 0x7F)
        if b & 0x80:
            break
    return out.decode("latin-1")


def module_ok(m):
    par = 0
    for b in m[:9]:
        par ^= b
    return len(m) >= 12 and m[:2] == b"\x87\xcd" and par == 0xFF and crc24(m) == 0x800FE3


class Disk:
    """An RBF file system in a JVC image: LSNs in order after the header."""

    def __init__(self, path):
        self.img = bytearray(open(path, "rb").read())
        self.head = len(self.img) % 256
        self.lsn0 = self.sec(0)

    def at(self, lsn):
        return self.head + lsn * 256

    def sec(self, lsn):
        return self.img[self.at(lsn):self.at(lsn) + 256]

    def put(self, lsn, data):
        assert len(data) <= 256
        self.img[self.at(lsn):self.at(lsn) + len(data)] = data

    def word(self, off, n):
        return int.from_bytes(self.lsn0[off:off + n], "big")

    def fd(self, lsn):
        f = self.sec(lsn)
        segs = []
        for i in range(0x10, 0x100, 5):
            a, n = int.from_bytes(f[i:i + 3], "big"), int.from_bytes(f[i + 3:i + 5], "big")
            if n == 0:
                break
            segs.append((a, n))
        return f, int.from_bytes(f[9:13], "big"), segs

    def read(self, lsn):
        f, size, segs = self.fd(lsn)
        data = b"".join(self.sec(a + k) for a, n in segs for k in range(n))
        return f[0], bytes(data[:size])

    def entries(self, lsn):
        att, data = self.read(lsn)
        if not att & 0x80:
            raise SystemExit("not a directory")
        for i in range(0, len(data), 32):
            e = data[i:i + 32]
            if e[0]:
                yield name_at(e, 0), int.from_bytes(e[29:32], "big")

    def find(self, path):
        lsn = self.word(8, 3)
        for part in path.strip("/").split("/"):
            hit = [l for n, l in self.entries(lsn) if n.upper() == part.upper()]
            if not hit:
                return None
            lsn = hit[0]
        return lsn


def main():
    args = sys.argv[1:]
    drop = set()
    if len(args) == 5 and args[0] == "--drop":
        drop = {n.upper() for n in args[1].split(",") if n}
        args = args[2:]
    if len(args) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    boot_path, mods_path, out = args
    if os.path.exists(out):
        raise SystemExit(f"{out}: exists; not written")

    # clock_tc3: one module, named Clock, whole.
    mods = Disk(mods_path)
    lsn = mods.find("/MODULES/clock_tc3")
    if lsn is None:
        raise SystemExit(f"{mods_path}: no /MODULES/clock_tc3")
    _, tc3 = mods.read(lsn)
    if not module_ok(tc3) or (tc3[2] << 8 | tc3[3]) != len(tc3) or \
            name_at(tc3, tc3[4] << 8 | tc3[5]) != "Clock":
        raise SystemExit(f"{mods_path}: /MODULES/clock_tc3 is not one whole Clock module")

    d = Disk(boot_path)
    bt, bsz, bit = d.word(0x15, 3), d.word(0x18, 2), d.word(6, 2)
    fd_lsn = d.find("/OS9Boot")
    if not bt or not bsz or fd_lsn is None:
        raise SystemExit(f"{boot_path}: no boot file")
    f, size, segs = d.fd(fd_lsn)
    if size != bsz or len(segs) != 1 or segs[0][0] != bt:
        raise SystemExit(f"{boot_path}: OS9Boot is not the one run LSN0 names")
    boot = bytes(d.img[d.at(bt):d.at(bt) + bsz])

    # Its modules, end to end; the Clock among them replaced.
    new, i, swapped, dropped = bytearray(), 0, 0, set()
    while i < len(boot):
        n = boot[i + 2] << 8 | boot[i + 3]
        m = boot[i:i + n]
        if not module_ok(m):
            raise SystemExit(f"{boot_path}: OS9Boot's module at {i} is not whole")
        name = name_at(m, m[4] << 8 | m[5])
        if name.upper() in drop:
            dropped.add(name.upper())
            print(f"OS9Boot: {name} left out, {n} bytes")
        elif name == "Clock" and m[6] == 0xC1:
            new += tc3
            swapped += 1
        else:
            new += m
        i += n
    if dropped != drop:
        raise SystemExit(f"{boot_path}: OS9Boot has no {', '.join(sorted(drop - dropped))}")
    if swapped != 1:
        raise SystemExit(f"{boot_path}: {swapped} Clock modules in OS9Boot, not one")
    print(f"OS9Boot: Clock, {len(boot)} bytes, now with clock_tc3: {len(new)} bytes")

    # Room: the run it has, or the first free run long enough.
    need = (len(new) + 255) // 256
    start, have = segs[0]
    if bit != 1:
        raise SystemExit(f"{boot_path}: {bit} sectors a cluster; only 1 is handled")
    mapn = d.word(4, 2)
    amap = bytearray(b"".join(d.sec(1 + k) for k in range((mapn + 255) // 256))[:mapn])
    used = lambda c: amap[c // 8] >> (7 - c % 8) & 1
    def mark(c, v):
        if v:
            amap[c // 8] |= 0x80 >> (c % 8)
        else:
            amap[c // 8] &= ~(0x80 >> (c % 8)) & 0xFF
    if need > have:
        for c in range(start, start + have):
            mark(c, 0)
        total = min(d.word(0, 3), mapn * 8)
        run = next((c for c in range(1, total - need + 1)
                    if all(not used(c + k) for k in range(need))), None)
        if run is None:
            raise SystemExit(f"{boot_path}: no free run of {need} sectors for OS9Boot")
        for c in range(run, run + need):
            mark(c, 1)
        for k in range(0, len(amap), 256):
            d.put(1 + k // 256, bytes(amap[k:k + 256]))
        print(f"OS9Boot: moved from LSN {start} ({have} sectors) to LSN {run} ({need})")
        start, have = run, need
    data = bytes(new) + bytes(have * 256 - len(new))
    for k in range(have):
        d.put(start + k, data[k * 256:(k + 1) * 256])
    f = bytearray(f)
    f[9:13] = len(new).to_bytes(4, "big")
    f[0x10:0x15] = start.to_bytes(3, "big") + have.to_bytes(2, "big")
    d.put(fd_lsn, bytes(f))
    l0 = bytearray(d.sec(0))
    l0[0x15:0x18] = start.to_bytes(3, "big")
    l0[0x18:0x1A] = len(new).to_bytes(2, "big")
    d.put(0, bytes(l0))

    # startup's SETIME, a comment.
    st = d.find("/startup")
    if st is not None:
        sf, ssize, ssegs = d.fd(st)
        _, text = d.read(st)
        lines = text.split(b"\r")
        hit = [k for k, l in enumerate(lines) if l.strip().upper().startswith(b"SETIME")]
        new_text = b"\r".join(b"* " + l if k in hit else l for k, l in enumerate(lines))
        if hit and len(new_text) <= sum(n for _, n in ssegs) * 256:
            blob = new_text + bytes(-len(new_text) % 256)
            off = 0
            for a, n in ssegs:
                for k in range(n):
                    if off < len(blob):
                        d.put(a + k, blob[off:off + 256])
                    off += 256
            sf = bytearray(sf)
            sf[9:13] = len(new_text).to_bytes(4, "big")
            d.put(st, bytes(sf))
            print(f"startup: {len(hit)} SETIME line made a comment")

    with open(out, "xb") as fo:
        fo.write(d.img)
    print(f"{out}: written")
    return 0


if __name__ == "__main__":
    sys.exit(main())
