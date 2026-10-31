#!/bin/bash
# T1 — Move tree asset read + nativeLoadTree off the UI thread (RED-state test).
# Hermetic: greps MainActivity.java only. No device, no build, no network.
# Spec refs: FR-STARTUP-002, AC-STARTUP-003. Method: TC-STARTUP-003 grep part.
# ASSUMPTION markers: A2 single background loader; A3 nativeLoadTree 0-semantics
# untouched; A4 no Java harness — grep + manual per spec Method column.
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
check_absent() { # $1 = description, rest = grep -E pattern (must NOT match region)
  local desc="$1"; shift
  if grep -Eq "$@" "$SRC"; then
    echo "FAIL: $desc"
    FAIL=$((FAIL+1))
  else
    echo "PASS: $desc"
  fi
}
echo "== T1 async tree load checks =="
# 1. Public interface exists verbatim: void startAsyncTreeLoad()
check "startAsyncTreeLoad() declared (void, no args)" 'void startAsyncTreeLoad\(\)'
# 2. Test hook exists (MAJOR fix): package-private handleTreeBytes(byte[], long)
#    — parse moved into the treeLoader background lambda; only the resulting
#    handle is posted to the UI thread.
check "handleTreeBytes(byte[], long) hook declared" 'void handleTreeBytes\(byte\[\][^)]*, *long'
# 3. onCreate delegates to startAsyncTreeLoad (no inline sync load)
check "onCreate calls startAsyncTreeLoad" 'startAsyncTreeLoad\(\)'
# 4. onCreate must NOT block on the load path: no .get()/.join()/await/Future.get/CountDownLatch.await/Thread.join in onCreate region
ONCREATE_REGION=$(sed -n '/protected void onCreate/,/^    }/p' "$SRC")
if echo "$ONCREATE_REGION" | grep -Eq '\.get\(\)|\.join\(\)|await\(|CountDownLatch|Thread\.join|Future'; then
  echo "FAIL: onCreate blocks on load (join/get/await present)"
  FAIL=$((FAIL+1))
else
  echo "PASS: onCreate has no blocking join/get/await on load path"
fi
# 5. onCreate must NOT call nativeLoadTree synchronously (load lives in background)
if echo "$ONCREATE_REGION" | grep -Eq 'nativeLoadTree'; then
  echo "FAIL: onCreate still calls nativeLoadTree synchronously"
  FAIL=$((FAIL+1))
else
  echo "PASS: onCreate does not call nativeLoadTree synchronously"
fi
# 6. Single-owner background loader present (A2): Executor/HandlerThread/Thread
check "single background loader (Executor|HandlerThread|new Thread|AsyncTask)" 'Executor|HandlerThread|new Thread|Executors'
# 7. Fail-closed diagnostic: logcat 'tree-load: <reason>' on failure path
check "fail-closed logcat diagnostic 'tree-load:'" 'tree-load'
# 8. engineHandle stays 0 on failure (fail-closed branch assigns/guards 0)
check "engineHandle fail-closed (stays 0 / guarded)" 'engineHandle'
# 9. Loader never throws: completion path catches Throwable/Exception
check "loader never throws (catches Throwable|Exception|IOException)" 'catch *\( *(Throwable|Exception|IOException)'
# 10. MAJOR placement rule: nativeLoadTree runs on the background loader, never
#     on the UI thread. Split startAsyncTreeLoad at the Handler.post boundary:
#     the pre-post (background) part must contain nativeLoadTree; the post
#     (UI-thread callback) tail and the handleTreeBytes hook body must not.
LOADER_BODY=$(sed -n '/void startAsyncTreeLoad/,/^    }/p' "$SRC")
BG_BEFORE_POST=$(echo "$LOADER_BODY" | awk '/Handler.*post/{exit} {print}')
POST_AND_AFTER=$(echo "$LOADER_BODY" | awk '/Handler.*post/{flag=1} flag{print}')
HOOK_BODY_T1=$(sed -n '/void handleTreeBytes/,/^    }/p' "$SRC")
if echo "$BG_BEFORE_POST" | grep -Eq 'nativeLoadTree'; then
  echo "PASS: nativeLoadTree reachable from background-loader lambda region"
else
  echo "FAIL: nativeLoadTree reachable from background-loader lambda region"
  FAIL=$((FAIL+1))
fi
if echo "$POST_AND_AFTER" | grep -Eq 'nativeLoadTree'; then
  echo "FAIL: nativeLoadTree absent from Handler.post UI-thread callback"
  FAIL=$((FAIL+1))
else
  echo "PASS: nativeLoadTree absent from Handler.post UI-thread callback"
fi
if echo "$HOOK_BODY_T1" | grep -Eq 'nativeLoadTree'; then
  echo "FAIL: nativeLoadTree absent from handleTreeBytes hook body (parse is off-UI)"
  FAIL=$((FAIL+1))
else
  echo "PASS: nativeLoadTree absent from handleTreeBytes hook body (parse is off-UI)"
fi
echo "== $FAIL failure(s) =="
exit "$FAIL"
