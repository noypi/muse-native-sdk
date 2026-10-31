#!/bin/bash
# T4 — Fail closed on null/empty tree asset with diagnostic, never crash (RED-state test).
# Hermetic: greps MainActivity.java only. No device, no build, no network.
# Spec refs: FR-STARTUP-002, AC-STARTUP-004, Errors table (null/empty tree asset).
# Method: executable grep form of the TC-STARTUP-004 diagnostic half
# (missing-asset run + logcat check themselves stay manual per the spec).
# Interface: none — no new callable surface; the behavior is the T1 completion
#   branch: null/empty bytes or nativeLoadTree == 0 → logcat
#   `tree-load failed-closed: <null|empty|native-0>` + treeReady stays false,
#   no render, no retry storm, no throw.
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
echo "== T4 fail-closed asset checks =="
# 1-3. Exact fail-closed diagnostics (one line per failure cause).
check "null diagnostic: tree-load failed-closed: null" 'tree-load failed-closed: null'
check "empty diagnostic: tree-load failed-closed: empty" 'tree-load failed-closed: empty'
check "native-0 diagnostic: tree-load failed-closed: native-0" 'tree-load failed-closed: native-0'
# Completion body for order-sensitive checks.
HOOK_BODY=$(sed -n '/void handleTreeBytes/,/^    }/p' "$SRC")
# 4. treeReady stays false on the failure paths: treeReady = true is installed
#    only after the handle != 0 success check (all three failure branches return early).
GOT_ZERO_LINE=$(echo "$HOOK_BODY" | grep -n 'handle *== *0' | head -1 | cut -d: -f1)
READY_LINE=$(echo "$HOOK_BODY" | grep -n 'treeReady *= *true' | head -1 | cut -d: -f1)
if [ -n "$GOT_ZERO_LINE" ] && [ -n "$READY_LINE" ] && [ "$GOT_ZERO_LINE" -lt "$READY_LINE" ] \
   && echo "$HOOK_BODY" | grep -Eq 'treeBytes *== *null' \
   && echo "$HOOK_BODY" | grep -Eq 'length *== *0'; then
  echo "PASS: treeReady stays false on null/empty/native-0 (early return before install)"
else
  echo "FAIL: treeReady stays false on null/empty/native-0 (early return before install)"
  FAIL=$((FAIL+1))
fi
# 5. No render on the failure paths: the hook never renders directly (T2 gate only).
if ! echo "$HOOK_BODY" | grep -Eq 'nativeRenderFrame'; then
  echo "PASS: hook never calls nativeRenderFrame directly (gate only)"
else
  echo "FAIL: hook never calls nativeRenderFrame directly (gate only)"
  FAIL=$((FAIL+1))
fi
# 6. Never throws: the hook catches everything at the boundary.
if echo "$HOOK_BODY" | grep -Eq 'catch *\(.*(Throwable|Exception)'; then
  echo "PASS: hook catches Throwable/Exception (never throws)"
else
  echo "FAIL: hook catches Throwable/Exception (never throws)"
  FAIL=$((FAIL+1))
fi
# 7. No retry storm: exactly one diagnostic per failure, no loop or reschedule.
if ! echo "$HOOK_BODY" | grep -Eq 'for *\(|while *\(|schedule|retry|postDelayed'; then
  echo "PASS: hook has no retry loop or reschedule (no retry storm)"
else
  echo "FAIL: hook has no retry loop or reschedule (no retry storm)"
  FAIL=$((FAIL+1))
fi
# 8. Success path still reaches the T2 gate (fail-closed must not swallow success).
if echo "$HOOK_BODY" | grep -Eq 'maybeRenderFirstFrame'; then
  echo "PASS: hook calls the T2 gate on success"
else
  echo "FAIL: hook calls the T2 gate on success"
  FAIL=$((FAIL+1))
fi
echo "== $FAIL failure(s) =="
exit "$FAIL"
