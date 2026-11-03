#!/bin/sh
# BUG-RUNTIME-012 Batch 1 T2 RED pin (GR-08 T1.1): the 3 generic callers
# resolve without the FlappyContent module — no direct content-module calls.
# Interface: sh engine/runtime/tests/generic_caller_seam_check.sh <code-root>
# Exit 0 iff none of gallery_present.cpp, jni_bridge.cpp, input_chain.cpp
# references TryPresentContentScene | QueryPresentedSceneTaps |
# FormatContentDiagLine | FlushFlappyFrameToSwapchain | flappy_content.h |
# Flappy; exit 1 naming offending references otherwise.
# Hermetic: source grep only. No device, no network. No production files touched.
set -u

usage() {
  echo "Usage: sh engine/runtime/tests/generic_caller_seam_check.sh <code-root>" >&2
}

if [ "$#" -lt 1 ] || [ -z "${1:-}" ]; then
  usage
  exit 2
fi

ROOT="$1"
RUNTIME_DIR="$ROOT/engine/runtime"
CALLERS="$RUNTIME_DIR/src/gallery_present.cpp $RUNTIME_DIR/src/jni_bridge.cpp $RUNTIME_DIR/src/input_chain.cpp"
PATTERN='TryPresentContentScene|QueryPresentedSceneTaps|FormatContentDiagLine|FlushFlappyFrameToSwapchain|flappy_content\.h|Flappy'

for f in $CALLERS; do
  if [ ! -f "$f" ]; then
    echo "generic_caller_seam_check: FAIL — caller not found: $f" >&2
    exit 1
  fi
done

HITS="$(grep -nE -- "$PATTERN" $CALLERS 2>/dev/null)"
if [ -n "$HITS" ]; then
  COUNT="$(printf '%s\n' "$HITS" | wc -l | tr -d ' ')"
  echo "generic_caller_seam_check: FAIL — $COUNT direct content-module reference(s) in generic callers:" >&2
  printf '%s\n' "$HITS" >&2
  exit 1
fi
echo "generic_caller_seam_check: PASS — generic callers resolve without the FlappyContent module"
exit 0
