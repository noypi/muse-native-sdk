#!/bin/bash
# T3 — Clean teardown: no leak, no use-after-destroy, no stranded load (RED-state test).
# Hermetic: greps MainActivity.java only. No device, no build, no network.
# Spec refs: FR-STARTUP-002, AC-STARTUP-004, RULE-RUNTIME-004 (ordering constraint).
# Method: executable grep form of the TC-STARTUP-004 procedure teardown half
# (the rotate/bg-fg/force-stop steps themselves stay manual per the spec).
# Interface (verbatim): void onDestroy() / void surfaceDestroyed(SurfaceHolder) —
#   existing signatures; error shape: fail-closed no-op when engineHandle == 0
#   or generation stale; never dereferences destroyed handle.
#   Ordering: surfaceDestroyed→nativeDetachSurface,
#   onDestroy→cancel load + detach + destroy + handle=0 per RULE-RUNTIME-004.
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
echo "== T3 clean-teardown checks =="
# 1. Existing signature: onDestroy()
check "onDestroy() declared (existing signature)" 'void onDestroy\(\)'
# 2. Existing signature: surfaceDestroyed(SurfaceHolder)
check "surfaceDestroyed(SurfaceHolder) declared (existing signature)" 'void surfaceDestroyed\(SurfaceHolder'
# Method bodies for order-sensitive checks.
DESTROY_BODY=$(sed -n '/void onDestroy/,/^    }/p' "$SRC")
SD_BODY=$(sed -n '/void surfaceDestroyed/,/^    }/p' "$SRC")
# 3. onDestroy cancels the T1 background load (no stranded load).
if echo "$DESTROY_BODY" | grep -Eq 'shutdownNow|shutdown\(\)|\.cancel\(|cancelLoad|treeLoader.*cancel'; then
  echo "PASS: onDestroy cancels the background load"
else
  echo "FAIL: onDestroy cancels the background load"
  FAIL=$((FAIL+1))
fi
# 4. onDestroy bumps the load generation so a late post-back is dropped.
if echo "$DESTROY_BODY" | grep -Eq 'treeLoadGeneration *(\+\+|\+=|=[^;]*\+ *1)'; then
  echo "PASS: onDestroy bumps treeLoadGeneration (stale post-back dropped)"
else
  echo "FAIL: onDestroy bumps treeLoadGeneration (stale post-back dropped)"
  FAIL=$((FAIL+1))
fi
# 5. onDestroy keeps detach-then-destroy order per RULE-RUNTIME-004.
DETACH_LINE=$(echo "$DESTROY_BODY" | grep -n 'nativeDetachSurface' | head -1 | cut -d: -f1)
DESTROY_LINE=$(echo "$DESTROY_BODY" | grep -n 'nativeDestroy' | head -1 | cut -d: -f1)
if [ -n "$DETACH_LINE" ] && [ -n "$DESTROY_LINE" ] && [ "$DETACH_LINE" -lt "$DESTROY_LINE" ]; then
  echo "PASS: onDestroy detaches before destroying (RULE-RUNTIME-004)"
else
  echo "FAIL: onDestroy detaches before destroying (RULE-RUNTIME-004)"
  FAIL=$((FAIL+1))
fi
# 6. onDestroy releases the handle (engineHandle = 0, no dangling handle).
if echo "$DESTROY_BODY" | grep -Eq 'engineHandle *= *0'; then
  echo "PASS: onDestroy resets engineHandle = 0"
else
  echo "FAIL: onDestroy resets engineHandle = 0"
  FAIL=$((FAIL+1))
fi
# 7. surfaceDestroyed detaches the surface (host owns surface per RULE-RUNTIME-004).
if echo "$SD_BODY" | grep -Eq 'nativeDetachSurface'; then
  echo "PASS: surfaceDestroyed calls nativeDetachSurface"
else
  echo "FAIL: surfaceDestroyed calls nativeDetachSurface"
  FAIL=$((FAIL+1))
fi
# 8. surfaceDestroyed clears surfaceReady so the T2 gate closes (no render on dead surface).
if echo "$SD_BODY" | grep -Eq 'surfaceReady *= *false'; then
  echo "PASS: surfaceDestroyed clears surfaceReady=false"
else
  echo "FAIL: surfaceDestroyed clears surfaceReady=false"
  FAIL=$((FAIL+1))
fi
# 9. Fail-closed no-op when engineHandle == 0: both paths guard the handle.
if echo "$DESTROY_BODY" | grep -Eq 'engineHandle *(==|!=) *0' && echo "$SD_BODY" | grep -Eq 'engineHandle *(==|!=) *0'; then
  echo "PASS: onDestroy + surfaceDestroyed guard engineHandle == 0 (fail-closed no-op)"
else
  echo "FAIL: onDestroy + surfaceDestroyed guard engineHandle == 0 (fail-closed no-op)"
  FAIL=$((FAIL+1))
fi
# 10. Late completion is dropped: the T1 post-back compares the generation token.
if grep -Eq 'generation *!= *treeLoadGeneration' "$SRC"; then
  echo "PASS: stale post-back dropped via generation check"
else
  echo "FAIL: stale post-back dropped via generation check"
  FAIL=$((FAIL+1))
fi
# 11-13. Security: stale destroy-during-parse/load post-back installs nothing,
# renders nothing, and accounts for the orphaned native handle (LOW note in
# security-review.md: readyHandle would leak if the stale branch just returns).
# Extract the stale-generation branch body (the block guarded by the
# generation != treeLoadGeneration check inside the T1 post-back).
STALE_BODY=$(awk '/if \(generation != treeLoadGeneration\)/{flag=1} flag{print} flag && /^\s*\}/{exit}' "$SRC")
# 11. Stale completion installs nothing: no engineHandle install, no treeReady set,
# no delegation to the install hook.
if echo "$STALE_BODY" | grep -Eq 'engineHandle *='; then
  echo "FAIL: stale post-back installs nothing (sets engineHandle)"
  FAIL=$((FAIL+1))
elif echo "$STALE_BODY" | grep -Eq 'treeReady *= *true'; then
  echo "FAIL: stale post-back installs nothing (sets treeReady)"
  FAIL=$((FAIL+1))
elif echo "$STALE_BODY" | grep -Eq 'handleTreeBytes'; then
  echo "FAIL: stale post-back installs nothing (calls handleTreeBytes)"
  FAIL=$((FAIL+1))
else
  echo "PASS: stale post-back installs nothing (no engineHandle/treeReady/handleTreeBytes)"
fi
# 12. Stale completion renders nothing: never reaches the T2 render gate path.
if echo "$STALE_BODY" | grep -Eq 'maybeRenderFirstFrame|nativeRenderFrame|nativeAttachSurface'; then
  echo "FAIL: stale post-back renders nothing (reaches render path)"
  FAIL=$((FAIL+1))
else
  echo "PASS: stale post-back renders nothing (no render gate path)"
fi
# 13. Stale completion accounts for the orphaned native handle: either destroys
# it in the stale branch or documents the leak as accepted under A1.
if echo "$STALE_BODY" | grep -Eq 'nativeDestroy *\( *readyHandle *\)'; then
  echo "PASS: stale post-back destroys orphaned handle (nativeDestroy)"
elif grep -Eiq 'orphan|accepted leak|leak[^.]*accepted|A1[^.]*leak|leak[^.]*A1' "$SRC"; then
  echo "PASS: stale post-back orphan handle leak documented as accepted (A1)"
else
  echo "FAIL: stale post-back accounts for orphaned handle (nativeDestroy or documented accept)"
  FAIL=$((FAIL+1))
fi
echo "== $FAIL failure(s) =="
exit "$FAIL"
