#!/bin/sh
# JNI contract regression test — FR-RUNTIME-009 / AC-RUNTIME-027.
# Interface: jni_contract_test(libRuntime.so: path, MainActivity.java: path) -> exit_code: int
# Exit 0 iff every `native*` declared in MainActivity.java has a matching
# `Java_com_muse_host_MainActivity_*` export; otherwise exit 1 naming the
# missing symbol and citing AC-RUNTIME-027. Hermetic: no device, no network.
#
# Usage:
#   jni_contract_test.sh [libRuntime.so] [MainActivity.java] [jni_bridge.cpp]
# Defaults resolve relative to this script's directory inside the checkout, so
# `ctest -R jni_contract` runs hermetically on the host lane (no .so needed).
# Binary-first gate (BUG-TEXT-004): when libRuntime.so exists, every declared
# symbol must be a ` T ` dynamic export in the .so — a source match in
# jni_bridge.cpp never excuses a missing binary export (source-present/
# binary-absent = stale .so = UnsatisfiedLinkError). If the .so exists but
# `nm` is unavailable, FAIL hard naming the symbol (never fall back to
# source-only PASS). Only when no .so path exists (host lane) does the
# jni_bridge.cpp source fallback apply (same mangled names the .so must carry).
set -u

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" >/dev/null 2>&1 && pwd)"
RUNTIME_DIR="$(CDPATH= cd -- "$SCRIPT_DIR/.." >/dev/null 2>&1 && pwd)"

SO="${1:-$RUNTIME_DIR/../../build/host/libRuntime.so}"
JAVA="${2:-$RUNTIME_DIR/../../examples/m3-gallery/src/MainActivity.java}"
BRIDGE="${3:-$RUNTIME_DIR/src/jni_bridge.cpp}"

fail=0

if [ ! -f "$JAVA" ]; then
  echo "jni_contract_test: FAIL — MainActivity.java not found: $JAVA (AC-RUNTIME-027)"
  exit 1
fi

# Every `private ... native <type> <name>(...)` declaration in MainActivity.java.
NAMES="$(sed -n 's/.*private[[:space:]].*native[[:space:]][^[:space:]()]*[[:space:]]\([A-Za-z_][A-Za-z0-9_]*\)(.*/\1/p' "$JAVA" | sort -u)"
if [ -z "$NAMES" ]; then
  echo "jni_contract_test: FAIL — no native declarations parsed from $JAVA (AC-RUNTIME-027)"
  exit 1
fi
echo "jni_contract_test: native declarations: $(echo "$NAMES" | tr '\n' ' ')"

missing=""
for name in $NAMES; do
  sym="Java_com_muse_host_MainActivity_${name}"
  # Binary-first (BUG-TEXT-004): an existing .so decides. Source presence in
  # jni_bridge.cpp never overrides a missing `T` export while the .so exists.
  # The source fallback applies strictly when no .so path exists (host lane).
  if [ -f "$SO" ]; then
    if ! command -v nm >/dev/null 2>&1; then
      echo "jni_contract_test: FAIL — cannot inspect $SO: nm unavailable for $sym (AC-RUNTIME-027)"
      missing="$missing $sym"
      fail=1
    elif nm -D "$SO" 2>/dev/null | grep -q " T .*${sym}\$"; then
      echo "jni_contract_test: PASS — $name -> $sym (libRuntime.so dynamic symbols)"
    else
      echo "jni_contract_test: FAIL — missing JNI export $sym for native $name in $SO (AC-RUNTIME-027)"
      missing="$missing $sym"
      fail=1
    fi
  else
    # Source-only fallback for the host lane (no .so to inspect).
    if [ -f "$BRIDGE" ] && grep -q "$sym" "$BRIDGE"; then
      echo "jni_contract_test: PASS — $name -> $sym (jni_bridge.cpp; no .so inspected)"
    else
      echo "jni_contract_test: FAIL — missing JNI export $sym for native $name (AC-RUNTIME-027)"
      missing="$missing $sym"
      fail=1
    fi
  fi
done

# M3-006 (#313) delivery contract, updated by the M3-006 re-completion
# (coder 1d205741, code READY 0eed8fe4): `nativeDispatchBridgeTap` stays
# REMOVED (replaced by consume-on-delivery via `dispatchTapToOwner`) and must
# NOT be declared or expected anymore — a stale declaration here is a
# contract-vs-engine gap (UnsatisfiedLinkError on every ABI). Fail closed if
# it resurfaces. `nativeLastBridgeTapId` was likewise removed by M3-006 but
# has been RESTORED as the last-minted bridge tapId getter (it observes the
# shared per-process counter, never mints/mounts/dispatches), so it is
# blessed below — NOT stale.
for stale in nativeDispatchBridgeTap; do
  if echo "$NAMES" | grep -qx "$stale"; then
    echo "jni_contract_test: FAIL — stale M3-006 symbol $stale still declared in $JAVA (removed; delivery is via dispatchTapToOwner) (AC-RUNTIME-027)"
    fail=1
    missing="$missing Java_com_muse_host_MainActivity_${stale}(stale)"
  fi
  if [ -f "$BRIDGE" ] && grep -q "Java_com_muse_host_MainActivity_${stale}" "$BRIDGE"; then
    echo "jni_contract_test: FAIL — stale M3-006 export Java_com_muse_host_MainActivity_${stale} still present in $BRIDGE (removed; delivery is via dispatchTapToOwner) (AC-RUNTIME-027)"
    fail=1
    missing="$missing Java_com_muse_host_MainActivity_${stale}(stale-export)"
  fi
done

# M3-006 re-completion pin (coder 1d205741, code READY 0eed8fe4): the
# restored `nativeLastBridgeTapId` getter MUST be exported from jni_bridge.cpp
# with fail-closed -1 semantics (null env / pending exception / nothing minted
# yet / internal failure -> -1, which never aliases a live lease id). A
# missing export is a contract-vs-engine gap (UnsatisfiedLinkError at the
# host call site).
if [ -f "$BRIDGE" ] && grep -q "Java_com_muse_host_MainActivity_nativeLastBridgeTapId" "$BRIDGE"; then
  echo "jni_contract_test: PASS — nativeLastBridgeTapId export present in jni_bridge.cpp (M3-006 re-completion)"
  if grep -q "return -1" "$BRIDGE"; then
    echo "jni_contract_test: PASS — nativeLastBridgeTapId fail-closed -1 semantics present in jni_bridge.cpp"
  else
    echo "jni_contract_test: FAIL — nativeLastBridgeTapId fail-closed -1 semantics missing in $BRIDGE (M3-006 re-completion) (AC-RUNTIME-027)"
    fail=1
    missing="$missing Java_com_muse_host_MainActivity_nativeLastBridgeTapId(fail-closed)"
  fi
else
  echo "jni_contract_test: FAIL — missing JNI export Java_com_muse_host_MainActivity_nativeLastBridgeTapId in $BRIDGE (M3-006 re-completion restores this getter) (AC-RUNTIME-027)"
  fail=1
  missing="$missing Java_com_muse_host_MainActivity_nativeLastBridgeTapId"
fi

# Landed delivery pin 1/2: the UP-fire path must deliver via
# `dispatchTapToOwner` (consume-on-delivery). Internal C++ symbol, so this is
# a source pin on jni_bridge.cpp regardless of the .so lane.
if [ -f "$BRIDGE" ]; then
  if grep -q "dispatchTapToOwner(" "$BRIDGE"; then
    echo "jni_contract_test: PASS — dispatchTapToOwner delivery present in jni_bridge.cpp (M3-006 landed contract)"
  else
    echo "jni_contract_test: FAIL — dispatchTapToOwner delivery missing in $BRIDGE (M3-006 landed contract) (AC-RUNTIME-027)"
    fail=1
    missing="$missing dispatchTapToOwner(delivery)"
  fi
else
  echo "jni_contract_test: FAIL — jni_bridge.cpp not found: $BRIDGE (M3-006 delivery pin) (AC-RUNTIME-027)"
  fail=1
  missing="$missing jni_bridge.cpp(missing)"
fi

# Landed delivery pin 2/2: RouterInvoke/SetRoutes entries must exist in
# jni_bridge.cpp (source pin). These two are NOT `native*`-declared in
# MainActivity.java, so AC-RUNTIME-027's binary-first rule (every *declared*
# native resolves in the .so) does not extend to them — the generic loop
# above remains the sole binary gate. NOTE (stale-.so observation, all ABIs):
# the staged libs under examples/m3-gallery/build/staging currently carry
# neither nativeRouterInvoke nor nativeRouterSetRoutes as dynamic exports
# (only nativeSetShellState); they predate the landed source. Flagged for
# build-ops; not a gate failure while Java declares no such natives.
for rname in nativeRouterInvoke nativeRouterSetRoutes; do
  rsym="Java_com_muse_host_MainActivity_${rname}"
  if [ -f "$BRIDGE" ] && grep -q "$rsym" "$BRIDGE"; then
    echo "jni_contract_test: PASS — $rname present in jni_bridge.cpp (M3-006 landed contract)"
  else
    echo "jni_contract_test: FAIL — missing $rsym in $BRIDGE (M3-006 landed contract) (AC-RUNTIME-027)"
    fail=1
    missing="$missing $rsym"
  fi
done

if [ "$fail" -ne 0 ]; then
  echo "jni_contract_test: FAIL missing:${missing} (AC-RUNTIME-027: host must pass the surface through JNI as an opaque handle; stale contract = UnsatisfiedLinkError)"
  exit 1
fi
echo "jni_contract_test: all $(echo "$NAMES" | wc -l | tr -d ' ') native methods matched (AC-RUNTIME-027)"
exit 0
