#!/bin/sh
# Fetch XRoar at the pinned release and build it headless: the reference
# emulator and CPU oracle (design.md §15.2, D8). Nothing of XRoar is
# copied into this tree. The checkout lives in reference/ and the build in
# out/, both gitignored.
#
#   tools/trace/build-xroar.sh                  # checkout, then a headless build
#   tools/trace/build-xroar.sh --checkout-only  # what the CPU oracle needs (CI)
#   tools/trace/build-xroar.sh [--checkout-only] [CHECKOUT [BUILD]]
#
# The oracle (test/host/test_mc6809_oracle) compiles XRoar's mc6809.c from
# the checkout itself, so CMake needs only the checkout: re-run cmake
# after this script to turn its skip into a test. The headless build is
# for M3's trace diff and M4's pictures, got building in M1 so it is ready
# (EL §11.4). It needs autoconf, automake and pkg-config.
#
# The upstream server speaks only git's dumb HTTP protocol, so the clone
# is whole, not shallow (about 15 s).
set -eu

UPSTREAM=https://www.6809.org.uk/git/xroar.git
PINNED_TAG=1.13
PINNED=af501b71ed4959fb1968250d6a6fa7778347955a   # 1.13, M1, 2026-11-14

checkout_only=0
if [ "${1:-}" = "--checkout-only" ]; then checkout_only=1; shift; fi

here="$(cd "$(dirname "$0")/../.." && pwd)"
src="${1:-$here/reference/xroar}"
build="${2:-$here/out/xroar}"

if [ ! -d "$src/.git" ]; then
    mkdir -p "$(dirname "$src")"
    git clone -q "$UPSTREAM" "$src"
fi
if [ "$(git -C "$src" rev-parse HEAD)" != "$PINNED" ]; then
    git -C "$src" fetch -q --tags origin || true
    git -C "$src" checkout -q "$PINNED"
fi
rev="$(git -C "$src" rev-parse HEAD)"
[ "$rev" = "$PINNED" ] || { echo "$src is at $rev, not $PINNED_TAG ($PINNED)" >&2; exit 1; }
echo "xroar: $src at $PINNED_TAG ($rev)"

[ "$checkout_only" = 1 ] && exit 0

# Headless: no GUI toolkit, no audio, no OpenGL; trace on, for M3.
if [ ! -x "$src/configure" ]; then
    (cd "$src" && ./autogen.sh >/dev/null)
fi
mkdir -p "$build"
cd "$build"
"$src/configure" \
    --without-gtk3 --without-gtk2 --without-gtkgl \
    --without-sdl3 --without-sdl2 --without-cocoa --without-opengl \
    --without-coreaudio --without-jack --without-pulse --without-alsa --without-oss \
    --without-sndfile --without-libpng --without-zlib \
    --enable-trace > configure.log 2>&1 || { tail -20 configure.log >&2; exit 1; }
make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" > make.log 2>&1 || { tail -20 make.log >&2; exit 1; }
"$build/src/xroar" -help > /dev/null
echo "xroar: built $build/src/xroar"
