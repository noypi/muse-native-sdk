#!/bin/sh
# TC-SAMPLE-046 shape (FR-SAMPLE-016 AC-SAMPLE-045): no `FlappyContent`
# symbol anywhere under engine/runtime/src — the per-game C++ content module
# is deleted by the flappy re-author, not fenced.
# Interface: sh engine/runtime/tests/no_flappycontent_src_check.sh <code-root>
# Exit 0 iff `grep -rq -e FlappyContent -- engine/runtime/src` finds zero
# matches (negated-grep gate); exit nonzero + naming the hit files
# otherwise. Hermetic: grep over one source tree. No device, no network,
# no env/config reads.
set -u

if [ "$#" -lt 1 ] || [ -z "${1:-}" ]; then
  echo "Usage: sh engine/runtime/tests/no_flappycontent_src_check.sh <code-root>" >&2
  exit 2
fi

ROOT="$1"
DIR="$ROOT/engine/runtime/src"

if [ ! -d "$DIR" ]; then
  echo "no_flappycontent_src_check: FAIL — source tree missing (absence would false-pass a bare ! grep gate): $DIR" >&2
  exit 1
fi

if grep -rq -e FlappyContent -- "$DIR" 2>/dev/null; then
  echo "no_flappycontent_src_check: FAIL — FlappyContent present under $DIR" >&2
  grep -rln -e FlappyContent -- "$DIR" >&2
  exit 1
fi

echo "no_flappycontent_src_check: PASS — zero FlappyContent symbols under $DIR"
exit 0
