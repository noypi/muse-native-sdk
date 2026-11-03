#!/bin/sh
# BUG-RUNTIME-012 Batch 1 T1 RED pin (GR-08 T1.1): no FlappyContent remains
# under engine/.
# Interface: sh engine/runtime/tests/flappy_content_removal_check.sh <code-root>
# Exit 0 iff `grep -rE -- FlappyContent|flappy_content engine/` finds nothing
# under <code-root>/engine; exit 1 naming offending files otherwise.
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

MATCHES="$(grep -rE -l --exclude='flappy_content_removal_check.sh' --exclude='generic_caller_seam_check.sh' -- 'FlappyContent|flappy_content' "$ROOT/engine" 2>/dev/null)"
if [ -n "$MATCHES" ]; then
  COUNT="$(printf '%s\n' "$MATCHES" | wc -l | tr -d ' ')"
  echo "flappy_content_removal_check: FAIL — FlappyContent/flappy_content present in $COUNT file(s) under engine/:" >&2
  printf '%s\n' "$MATCHES" >&2
  exit 1
fi
echo "flappy_content_removal_check: PASS — no FlappyContent/flappy_content under engine/"
exit 0
