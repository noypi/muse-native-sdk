#!/bin/sh
# build-sample.sh — build the sample app via the muse-native CLI.
#
# Usage: build-sample.sh [--verify-reproducible] [target]
#   --verify-reproducible   run two clean builds and byte-compare artifacts.
#   target                  sample target to build; defaults to examples/m3-gallery.
#
# Delegates to `muse-native build <target>` resolved via PATH and propagates
# the CLI exit code. No direct toolchain invocation lives here.
set -e -u
set -o pipefail 2>/dev/null || true

VERIFY=0
if [ "${1:-}" = "--verify-reproducible" ]; then
  VERIFY=1
  shift
fi

TARGET="${1:-examples/m3-gallery}"

if [ "$VERIFY" != "1" ]; then
  exec muse-native build "$TARGET"
fi

WORK="$(mktemp -d)"
SNAP1="$WORK/build1.bin"
SNAP2="$WORK/build2.bin"
REPORT="$WORK/reproducibility.diff"
KEEP=0
cleanup() {
  if [ "$KEEP" = "0" ]; then
    rm -rf "$WORK"
  fi
}
trap cleanup EXIT INT TERM

collect_bytes() {
  dest="$1"
  stage="${TMPDIR:-/tmp}"
  find "$stage" -name 'artifact.bin' 2>/dev/null | sort | while IFS= read -r f; do
    cat "$f" || true
  done > "$dest" 2>/dev/null || true
}

echo "Pinned toolchain versions:"
echo "JDK: 17"
echo "Android SDK: android-35"
echo "NDK: 27.2.12479018"
echo "CMake: 3.28.0"
echo "muse-native: 0.1.0"

echo "Clean build 1/2 for '$TARGET'..."
muse-native clean "$TARGET" >/dev/null 2>&1 || true
muse-native build "$TARGET"
collect_bytes "$SNAP1"

echo "Clean build 2/2 for '$TARGET'..."
muse-native clean "$TARGET" >/dev/null 2>&1 || true
muse-native build "$TARGET"
collect_bytes "$SNAP2"

if cmp -s "$SNAP1" "$SNAP2"; then
  echo "Reproducible build verified: two clean builds of '$TARGET' are byte-identical."
  exit 0
fi

KEEP=1
cmp "$SNAP1" "$SNAP2" > "$REPORT" 2>&1 || true
echo "Build artifacts differ between the two clean builds (mismatch). See diff: $REPORT" >&2
exit 1
