#!/bin/sh
# TC-RUNTIME-044 (FR-RUNTIME-013 AC-RUNTIME-038): no `Flappy` per-app symbol
# in the generic dispatch file engine/runtime/src/renderer.cpp.
# Interface: sh engine/runtime/tests/no_flappy_renderer_check.sh <code-root>
# Exit 0 iff the file exists and `grep -Eq -- Flappy` finds zero matches
# (negated-grep gate); exit nonzero + naming the file otherwise. Hermetic:
# grep over one source file. No device, no network, no env/config reads.
set -u

if [ "$#" -lt 1 ] || [ -z "${1:-}" ]; then
  echo "Usage: sh engine/runtime/tests/no_flappy_renderer_check.sh <code-root>" >&2
  exit 2
fi

ROOT="$1"
FILE="$ROOT/engine/runtime/src/renderer.cpp"

if [ ! -f "$FILE" ]; then
  echo "no_flappy_renderer_check: FAIL — dispatch file missing (absence would false-pass a bare ! grep gate): $FILE" >&2
  exit 1
fi

if grep -Eq -- Flappy "$FILE" 2>/dev/null; then
  echo "no_flappy_renderer_check: FAIL — Flappy symbol present in $FILE" >&2
  grep -En -- Flappy "$FILE" >&2
  exit 1
fi

echo "no_flappy_renderer_check: PASS — zero Flappy symbols in $FILE"
exit 0
