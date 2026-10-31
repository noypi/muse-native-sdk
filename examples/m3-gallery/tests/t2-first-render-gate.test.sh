#!/bin/bash
# T2 — Gate first render on treeReady && surfaceReady (RED-state test).
# Hermetic: greps MainActivity.java only. No device, no build, no network.
# Spec refs: FR-STARTUP-002, AC-STARTUP-003. Method: TC-STARTUP-003 gate part.
# Interface (verbatim): void maybeRenderFirstFrame() — posts
#   nativeAttachSurface + nativeRenderFrame only when
#   treeReady && surfaceReady && engineHandle != 0; else no-op.
# Error shape: none — silent no-op (not a failure) when the gate is closed.
set -u
SRC="examples/m3-gallery/src/MainActivity.java"
FAIL=0
check() { # $1 = description, rest = grep -E pattern (must match SRC)
  local desc="$1"; shift
  if grep -Eq "$@" "$SRC"; then
    echo "PASS: $desc"
  else
    echo "FAIL: $desc"
    FAIL=$((FAIL+1))
  fi
}
echo "== T2 first-render gate checks =="
# 1. Public interface exists verbatim: void maybeRenderFirstFrame()
check "maybeRenderFirstFrame() declared (void, no args)" 'void maybeRenderFirstFrame\(\)'
# 2. Cold-readiness state: treeReady flag declared
check "treeReady flag declared" 'boolean treeReady'
# 3. Cold-readiness state: surfaceReady flag declared
check "surfaceReady flag declared" 'boolean surfaceReady'
# 4. Single gate check: treeReady && surfaceReady before first render
check "gate checks treeReady && surfaceReady" 'treeReady *&& *surfaceReady|surfaceReady *&& *treeReady'
# 5. Gate checks engineHandle != 0
check "gate checks engineHandle != 0" 'engineHandle *!= *0'
# 6. Gate posts attach + render (both calls reachable from gate body)
GATE_BODY=$(sed -n '/void maybeRenderFirstFrame/,/^    }/p' "$SRC")
if echo "$GATE_BODY" | grep -Eq 'nativeAttachSurface' && echo "$GATE_BODY" | grep -Eq 'nativeRenderFrame'; then
  echo "PASS: gate posts nativeAttachSurface + nativeRenderFrame"
else
  echo "FAIL: gate posts nativeAttachSurface + nativeRenderFrame"
  FAIL=$((FAIL+1))
fi
# 7. surfaceCreated routes through the gate (not direct render)
CREATED_BODY=$(sed -n '/void surfaceCreated/,/^    }/p' "$SRC")
if echo "$CREATED_BODY" | grep -Eq 'maybeRenderFirstFrame' && ! echo "$CREATED_BODY" | grep -Eq 'nativeRenderFrame'; then
  echo "PASS: surfaceCreated calls gate instead of rendering directly"
else
  echo "FAIL: surfaceCreated calls gate instead of rendering directly"
  FAIL=$((FAIL+1))
fi
# 8. surfaceChanged routes through the gate (not direct render)
CHANGED_BODY=$(sed -n '/void surfaceChanged/,/^    }/p' "$SRC")
if echo "$CHANGED_BODY" | grep -Eq 'maybeRenderFirstFrame' && ! echo "$CHANGED_BODY" | grep -Eq 'nativeRenderFrame'; then
  echo "PASS: surfaceChanged calls gate instead of rendering directly"
else
  echo "FAIL: surfaceChanged calls gate instead of rendering directly"
  FAIL=$((FAIL+1))
fi
# 9. Both completions call the gate: tree post-back (handleTreeBytes) calls it too
if grep -Eq 'maybeRenderFirstFrame' "$SRC" && sed -n '/void handleTreeBytes/,/^    }/p' "$SRC" | grep -Eq 'maybeRenderFirstFrame'; then
  echo "PASS: tree post-back (handleTreeBytes) calls the gate"
else
  echo "FAIL: tree post-back (handleTreeBytes) calls the gate"
  FAIL=$((FAIL+1))
fi
# 10. Silent no-op when closed: gate has early return / no throw on closed path
if echo "$GATE_BODY" | grep -Eq 'return'; then
  echo "PASS: gate is silent no-op when closed (early return, no throw)"
else
  echo "FAIL: gate is silent no-op when closed (early return, no throw)"
  FAIL=$((FAIL+1))
fi
echo "== $FAIL failure(s) =="
exit "$FAIL"
