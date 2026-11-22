#!/bin/sh
# Fetch M13's disk archive: the Color Computer Archive's disk images
# (colorcomputerarchive.com/repo/Disks/), as fetch-archive.sh fetches its
# cartridges and cassettes (design.md §15.4). Not committed: the titles
# are not ours to distribute, and out/ is ignored.
#
#   tools/fetch-disks.sh [-all] [dir]       # default: a sample, into out/disks
#
# By default a sample: from each folder's titles, CoCo 3 ones left out,
# an even spread by name of the number below (the owner's plan of
# 2026-11-21: a few hundred first, the whole archive once the sample's
# failures are understood). The spread depends only on the listing, so
# a later fetch takes the same titles unless the archive has changed.
# -all takes every title.
#
# As fetch-archive.sh: one request at a time with a pause between, files
# already fetched kept, each file's SHA-1 in DIR/manifest.sha1. Leaves
# the downloads in DIR/dl/<folder>/ and every title's disks in DIR/dsk/:
# a title's one disk as <title>.dsk, a title of several as a directory
# DIR/dsk/<title>/ with its disks under their own names; a bare disk in
# a listing as <name>.dsk. A zip with no
# .dsk is listed in DIR/no-dsk.txt.
set -eu

all=0
if [ "${1:-}" = "-all" ]; then all=1; shift; fi
dir="${1:-out/disks}"
base="https://colorcomputerarchive.com/repo/Disks"
# folder:sample. -all adds the folders the sample leaves out; Blank disks,
# Coco SDC, DrivePak and Drivewire are left out of both: they are empty
# disks or need hardware this emulator does not have.
folders="Games:150 Applications:50 Utilities:50 Educational:30 Operating%20Systems:40"
if [ "$all" = 1 ]; then
    folders="$folders Demos:0 Music:0 Pictures:0 Programming:0 Public%20Domain:0 Magazines:0 Books:0 Hardware:0"
fi

# The listing's zips, and bare disks (Public Domain has only those), one
# a line, still URL-encoded, CoCo 3 left out.
links() {
    curl -sSLf "$base/$1/" | grep -io 'href="[^"/?#]*\.\(zip\|dsk\)"' |
        sed 's/^href="//; s/"$//; s/&amp;/\&/g; s/&#0*39;/%27/g' |
        grep -v '%20(Coco%203)' | LC_ALL=C sort -u
}

# Every line if all, else `want` of them spread evenly.
spread() {
    awk -v want="$1" -v all="$all" '{ l[NR] = $0 }
        END {
            if (all || NR <= want) { for (i = 1; i <= NR; i++) print l[i]; exit }
            for (k = 0; k < want; k++) print l[int(k * NR / want) + 1]
        }'
}

for fw in $folders; do
    f="${fw%%:*}"; want="${fw##*:}"
    fname="$(printf '%b' "$(printf '%s' "$f" | sed 's/%/\\x/g')")"
    d="$dir/dl/$fname"
    mkdir -p "$d"
    n=0; got=0
    for l in $(links "$f" | spread "$want"); do
        n=$((n + 1))
        name="$(printf '%b' "$(printf '%s' "$l" | sed 's/%/\\x/g')")"
        [ -s "$d/$name" ] && continue
        if curl -sSLf -o "$d/$name.part" "$base/$f/$l"; then
            mv "$d/$name.part" "$d/$name"
            got=$((got + 1))
        else
            rm -f "$d/$name.part"
            echo "$fname/$name: not fetched" >&2
        fi
        sleep 0.3
    done
    echo "$fname: $n titles, $got fetched"
done

(cd "$dir/dl" && find . -type f ! -name '*.part' -print0 | LC_ALL=C sort -z |
    xargs -0 shasum) >"$dir/manifest.sha1"

# The extraction is Python's zipfile, run isolated (-I) from a temporary
# script outside the downloaded tree, and it writes only under DIR.
py="$(mktemp "${TMPDIR:-/tmp}/fetch-disks.XXXXXX")"
trap 'rm -f "$py"' EXIT
cat >"$py" <<'PY'
import os, shutil, sys, zipfile
root = sys.argv[1]
dl, dsk = os.path.join(root, "dl"), os.path.join(root, "dsk")
shutil.rmtree(dsk, ignore_errors=True)
os.makedirs(dsk)
counts = {"dsk": 0, "parts": 0, "nodsk": 0}
nodsk = []
for sub in sorted(os.listdir(dl)):
    for name in sorted(os.listdir(os.path.join(dl, sub))):
        if name.lower().endswith(".dsk"):
            # A bare disk is a title of its own.
            if not os.path.exists(os.path.join(dsk, name[:-4] + ".dsk")):
                shutil.copy(os.path.join(dl, sub, name), os.path.join(dsk, name[:-4] + ".dsk"))
                counts["dsk"] += 1
            continue
        if not name.lower().endswith(".zip"):
            continue
        title = name[:-4].replace("/", "_")
        try:
            z = zipfile.ZipFile(os.path.join(dl, sub, name))
        except zipfile.BadZipFile:
            nodsk.append(f"{sub}/{name}: not a zip")
            continue
        with z:
            parts = sorted(n for n in z.namelist()
                           if n.lower().endswith(".dsk") and not n.endswith("/"))
            if not parts:
                nodsk.append(f"{sub}/{name}")
                counts["nodsk"] += 1
                continue
            if os.path.exists(os.path.join(dsk, title + ".dsk")) or os.path.isdir(os.path.join(dsk, title)):
                continue
            if len(parts) == 1:
                with open(os.path.join(dsk, title + ".dsk"), "wb") as f:
                    f.write(z.read(parts[0]))
            else:
                d = os.path.join(dsk, title)
                os.makedirs(d)
                seen = set()
                for p in parts:
                    b = os.path.basename(p)
                    while b.lower() in seen:
                        b = "_" + b
                    seen.add(b.lower())
                    with open(os.path.join(d, b), "wb") as f:
                        f.write(z.read(p))
                counts["parts"] += 1
            counts["dsk"] += 1
with open(os.path.join(root, "no-dsk.txt"), "w") as f:
    f.write("".join(n + "\n" for n in nodsk))
print(f"{counts['dsk']:5d} titles -> {dsk} ({counts['parts']} of several disks, a directory each)")
print(f"{counts['nodsk']:5d} zips with no .dsk (no-dsk.txt)")
PY
python3 -I "$py" "$dir"
