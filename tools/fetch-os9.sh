#!/bin/sh
# Fetch the OS-9 disks test_os9 boots (design.md §12.4): Tandy's OS-9
# Level One 02.00.00 system disk and 02.01.00's two disks from the Color
# Computer Archive, and NitrOS-9 3.3.0's Level 1 disks for the CoCo 1 from
# its SourceForge release; then make 2.01's system disk with clock_tc3,
# the SmartWatch's clock, by tools/os9-clock.py (§12.7). Not committed:
# Tandy's are not ours to distribute, and the tests find each by SHA-1,
# as they find the ROMs.
#
#   tools/fetch-os9.sh [dir]        # default: roms/, where test_os9 looks
#
# The downloads are kept in out/os9-dl/ and not asked for again. Each
# disk is checked against the SHA-1 test_os9 knows before it is copied.
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
dir="${1:-$root/roms}"
dl="$root/out/os9-dl"
mkdir -p "$dir" "$dl"

tandy="OS-9%20Level%201%20v02.00.00%20(Tandy)%20(OS-9)%20(6847T1).zip"
tandy201="OS-9%20Level%201%20v02.01.00%20(Tandy)%20(OS-9)%20(6847T1).zip"
nitros9="nos96809l1v030300coco1.zip"

get() {
    [ -s "$dl/$2" ] && return 0
    curl -sSLf -o "$dl/$2.part" "$1" && mv "$dl/$2.part" "$dl/$2"
    sleep 1
}
get "https://colorcomputerarchive.com/repo/Disks/Operating%20Systems/$tandy" tandy-os9-l1-v0200.zip
get "https://colorcomputerarchive.com/repo/Disks/Operating%20Systems/$tandy201" tandy-os9-l1-v0201.zip
get "https://downloads.sourceforge.net/project/nitros9/releases/v3.3.0/$nitros9" "$nitros9"

# Python's zipfile, run isolated (-I) from a temporary script outside the
# downloaded tree, writing only the three named members into DIR.
py="$(mktemp "${TMPDIR:-/tmp}/fetch-os9.XXXXXX")"
trap 'rm -f "$py"' EXIT
cat >"$py" <<'EOF'
import hashlib, os, sys, zipfile
dl, out = sys.argv[1], sys.argv[2]
want = {
    ("tandy-os9-l1-v0200.zip", "OS9L1V2M.DSK"): "521740e7a00eefb4b92a6c803391df31c004fdf9",
    ("tandy-os9-l1-v0201.zip", "OS9-L1V201M.DSK"): "7019d3b476a3f944a6dab77c7c4c8e1d67b18863",
    ("tandy-os9-l1-v0201.zip", "OS9-L1V201B.DSK"): "0ade5dec3cdc0f34654e5ebfc779fb41c2a92737",
    ("nos96809l1v030300coco1.zip", "nos96809l1v030300coco1_40d_1.dsk"): "c417fad58ea8eca331e173922748e9903bc20fcd",
    ("nos96809l1v030300coco1.zip", "nos96809l1v030300coco1_80d.dsk"): "8695113e8a9e7d616e7db055af2b1f4a00989808",
}
bad = 0
for (zname, member), sha in want.items():
    z = zipfile.ZipFile(os.path.join(dl, zname))
    names = [n for n in z.namelist() if os.path.basename(n) == member]
    if not names:
        print(f"{zname}: no {member}", file=sys.stderr); bad += 1; continue
    data = z.read(names[0])
    got = hashlib.sha1(data).hexdigest()
    if got != sha:
        print(f"{member}: sha1 {got}, want {sha}", file=sys.stderr); bad += 1; continue
    with open(os.path.join(out, member), "wb") as f:
        f.write(data)
    print(f"{member}: ok")
sys.exit(1 if bad else 0)
EOF
python3 -I "$py" "$dl" "$dir"

# 2.01's system disk with clock_tc3 in OS9Boot, and the serial port's
# descriptor and driver left out to make room (tools/os9-clock.py).
rtc="$dir/OS9-L1V201M-RTC.DSK"
want=76fd68df3fc0527d1ceb49bc6772a7a7311d7636
if [ ! -e "$rtc" ]; then
    python3 -I "$root/tools/os9-clock.py" --drop T1,RS232 \
        "$dir/OS9-L1V201M.DSK" "$dir/OS9-L1V201B.DSK" "$rtc"
fi
got="$(shasum "$rtc" | cut -d' ' -f1)"
[ "$got" = "$want" ] && echo "OS9-L1V201M-RTC.DSK: ok" || {
    echo "OS9-L1V201M-RTC.DSK: sha1 $got, want $want" >&2; exit 1; }
