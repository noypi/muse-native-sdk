#!/bin/sh
# BUG-RUNTIME-012 Batch 1 T1 RED pin (GR-08 T1.1): no FlappyContent remains
# under engine/.
# Interface: sh engine/runtime/tests/flappy_content_removal_check.sh <code-root>
# Exit 0 iff `grep -rE -- FlappyContent|flappy_content engine/` finds nothing
# under <code-root>/engine except this gate's own wiring (its script/exclude
# names and the CTest NAME/COMMAND lines that invoke it); exit 1 naming
# offending files otherwise.
# Hermetic: source grep only. No device, no network. No production files touched.
set -u

usage() {
  echo "Usage: sh engine/runtime/tests/flappy_content_removal_check.sh <code-root>" >&2
}

if [ "$#" -lt 1 ] || [ -z "${1:-}" ]; then
  usage
  exit 2
fi

ROOT="$1"
if [ ! -d "$ROOT/engine" ]; then
  echo "flappy_content_removal_check: FAIL — engine/ not found under: $ROOT" >&2
  usage >&2
  exit 1
fi

# A gate that trips on its own registration can never go green: filter the
# gate's own wiring lines (script/exclude names, CTest NAME/COMMAND). Any
# other occurrence — a symbol, include, or build-graph entry — still fails.
HITS="$(grep -rE -n --exclude='flappy_content_removal_check.sh' --exclude='generic_caller_seam_check.sh' -- 'FlappyContent|flappy_content' "$ROOT/engine" 2>/dev/null | grep -v 'flappy_content_removal_check\|generic_caller_seam_check')"
if [ -n "$HITS" ]; then
  COUNT="$(printf '%s\n' "$HITS" | wc -l | tr -d ' ')"
  echo "flappy_content_removal_check: FAIL — FlappyContent/flappy_content present in $COUNT line(s) under engine/:" >&2
  printf '%s\n' "$HITS" >&2
  exit 1
fi
echo "flappy_content_removal_check: PASS — no FlappyContent/flappy_content under engine/"
exit 0
