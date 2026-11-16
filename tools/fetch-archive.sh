#!/bin/sh
# Fetch M11's archive: the Color Computer Archive's cartridges and
# cassettes (colorcomputerarchive.com/repo/), the owner's choice of
# 2026-11-15 (design.md §15.4). Not committed: the titles are not ours to
# distribute, and out/ is ignored.
#
#   tools/fetch-archive.sh [dir]        # default: out/corpus
#
# The archive is a living one, so no single SHA-1 names it: each file's
# is written to DIR/manifest.sha1, sorted, and a later fetch shows what
# changed by its diff. Files already fetched are kept and not asked for
# again. One request at a time, with a pause between, as a volunteer's
# site deserves.
#
# Leaves the downloads in DIR/dl/<folder>/, every cartridge in DIR/ccc/
# and every tape in DIR/cas/: a title's one .cas as <title>.cas, a title
# of several as a directory DIR/cas/<title>/ with its parts under their
# own names. Cartridges and tapes the archive marks "(Coco 3)" are kept
# out: they are another machine (design.md §20). A zip with no .cas
# (a WAV alone) is listed in DIR/no-cas.txt: WAV tapes are deferred
# (design.md §20).
set -eu

dir="${1:-out/corpus}"
base="https://colorcomputerarchive.com/repo"
folders="Cartridges Cassettes/Applications Cassettes/Demos Cassettes/Educational Cassettes/Games Cassettes/Music"

# The listing's file links, one a line, still URL-encoded.
links() {
    curl -sSLf "$base/$1/" | grep -o 'href="[^"/?#]*\.\(zip\|ccc\|rom\)"' |
        sed 's/^href="//; s/"$//; s/&amp;/\&/g; s/&#0*39;/%27/g' | sort -u
}

for f in $folders; do
    d="$dir/dl/$f"
    mkdir -p "$d"
    n=0; got=0
    for l in $(links "$f"); do
        n=$((n + 1))
        name="$(printf '%b' "$(printf '%s' "$l" | sed 's/%/\\x/g')")"
        [ -s "$d/$name" ] && continue
        if curl -sSLf -o "$d/$name.part" "$base/$f/$l"; then
            mv "$d/$name.part" "$d/$name"
            got=$((got + 1))
        else
            rm -f "$d/$name.part"
            echo "$f/$name: not fetched" >&2
        fi
        sleep 0.3
    done
    echo "$f: $n files, $got fetched"
done

(cd "$dir/dl" && find . -type f ! -name '*.part' -print0 | LC_ALL=C sort -z |
    xargs -0 shasum) >"$dir/manifest.sha1"

# The extraction is Python's zipfile, run isolated (-I) from a temporary
# script outside the downloaded tree, and it writes only under DIR.
py="$(mktemp "${TMPDIR:-/tmp}/fetch-archive.XXXXXX")"
trap 'rm -f "$py"' EXIT
cat >"$py" <<'PY'
import os, shutil, sys, zipfile
root = sys.argv[1]
dl, cas, ccc = (os.path.join(root, d) for d in ("dl", "cas", "ccc"))
for d in (cas, ccc):
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
coco3 = lambda n: "(coco 3)" in n.lower()
counts = {"ccc": 0, "cas": 0, "parts": 0, "coco3": 0, "nocas": 0}
nocas = []
for name in sorted(os.listdir(os.path.join(dl, "Cartridges"))):
    if coco3(name):
        counts["coco3"] += 1
        continue
    if name.lower().endswith((".ccc", ".rom")):
        shutil.copy(os.path.join(dl, "Cartridges", name), ccc)
        counts["ccc"] += 1
subs = sorted(os.path.relpath(d, dl) for d, _, _ in os.walk(os.path.join(dl, "Cassettes")))
for sub in subs:
    for name in sorted(os.listdir(os.path.join(dl, sub))):
        if not name.lower().endswith(".zip"):
            continue
        title = name[:-4]
        if coco3(title):
            counts["coco3"] += 1
            continue
        try:
            z = zipfile.ZipFile(os.path.join(dl, sub, name))
        except zipfile.BadZipFile:
            nocas.append(f"{sub}/{name}: not a zip")
            continue
        with z:
            parts = [n for n in z.namelist() if n.lower().endswith(".cas") and not n.endswith("/")]
            if not parts:
                nocas.append(f"{sub}/{name}")
                counts["nocas"] += 1
                continue
            # The same title in two folders is kept once, the first.
            if os.path.exists(os.path.join(cas, title + ".cas")) or os.path.isdir(os.path.join(cas, title)):
                continue
            if len(parts) == 1:
                with open(os.path.join(cas, title + ".cas"), "wb") as f:
                    f.write(z.read(parts[0]))
            else:
                d = os.path.join(cas, title)
                os.makedirs(d)
                for p in parts:
                    with open(os.path.join(d, os.path.basename(p)), "wb") as f:
                        f.write(z.read(p))
                counts["parts"] += 1
            counts["cas"] += 1
with open(os.path.join(root, "no-cas.txt"), "w") as f:
    f.write("".join(n + "\n" for n in nocas))
print(f"{counts['ccc']:5d} cartridges -> {ccc}")
print(f"{counts['cas']:5d} tapes -> {cas} ({counts['parts']} of several files, a directory each)")
print(f"{counts['coco3']:5d} CoCo 3 titles left out; {counts['nocas']} zips with no .cas (no-cas.txt)")
PY
python3 -I "$py" "$dir"
