#!/bin/sh
# run-6562.sh — T1 6562 EGL verdict script (FR-GRAPHICS-011, AC-031, 6562 leg only).
# Compiles probe.c with the pinned NDK (NFR-RUNTIME-001 r27), pushes to
# 127.0.0.1:6562, runs it, and echoes the verdict line.
# Success: exit 0 + stdout `EGL-PROBE-VERDICT: PASS ...`.
# Failure: nonzero + stdout `EGL-PROBE-DIAGNOSTIC: <trigger>` (never hang/silent skip).
set -u

SERIAL="127.0.0.1:6562"
HERE=$(dirname "$0")
EXPECTED_NDK_REV="27.2.12479018"
TIMEOUT_SECS=60

fail() {
  # $1 = trigger (single line, no newlines)
  echo "EGL-PROBE-DIAGNOSTIC: $1"
  exit 1
}

# --- pinned NDK (NFR-RUNTIME-001 r27) ---
NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-${ANDROID_NDK:-}}}"
[ -n "$NDK_HOME" ] || fail "ndk-not-found (set ANDROID_NDK_HOME; expected rev $EXPECTED_NDK_REV)"
[ -d "$NDK_HOME" ] || fail "ndk-not-found at '$NDK_HOME' (expected rev $EXPECTED_NDK_REV)"
[ -f "$NDK_HOME/source.properties" ] || fail "ndk-source.properties-missing in '$NDK_HOME' (expected rev $EXPECTED_NDK_REV)"
INSTALLED_REV=$(grep '^Pkg.Revision' "$NDK_HOME/source.properties" | sed 's/.*= *//')
[ "$INSTALLED_REV" = "$EXPECTED_NDK_REV" ] || fail "ndk-revision-mismatch '$INSTALLED_REV' (expected $EXPECTED_NDK_REV)"
CLANG="$NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/clang"
[ -x "$CLANG" ] || fail "ndk-clang-missing at '$CLANG' (expected rev $EXPECTED_NDK_REV)"

# --- compile probe.c for 6562 (x86_64 emulator) ---
mkdir -p "$HERE/build"
if ! "$CLANG" --target=x86_64-linux-android29 -Wall -Wextra -O2 \
  "$HERE/probe.c" -o "$HERE/build/egl-probe-6562" -lEGL -ldl -llog 2>"$HERE/build/egl-probe-6562.build.log"; then
  fail "compile-failed (see engine/graphics/egl-probe/build/egl-probe-6562.build.log)"
fi
[ -f "$HERE/build/egl-probe-6562" ] || fail "compile-missing-output"

# --- device reachability (time-boxed) ---
command -v adb >/dev/null 2>&1 || fail "adb-not-found"
if command -v timeout >/dev/null 2>&1; then
  ADB_RUN="timeout $TIMEOUT_SECS adb"
else
  ADB_RUN="adb"
fi
if ! $ADB_RUN -s "$SERIAL" shell echo ok >/dev/null 2>&1; then
  fail "device-unreachable $SERIAL"
fi

# --- push + run ---
if ! $ADB_RUN -s "$SERIAL" push "$HERE/build/egl-probe-6562" /data/local/tmp/egl-probe-6562 >/dev/null 2>&1; then
  fail "adb-push-failed $SERIAL"
fi
OUT=$($ADB_RUN -s "$SERIAL" shell /data/local/tmp/egl-probe-6562 2>&1)
CODE=$?
echo "$OUT"
case "$OUT" in
  *"EGL-PROBE-VERDICT: PASS"*)
    exit 0;;
  *"EGL-PROBE-DIAGNOSTIC:"*)
    exit 1;;
  *)
    if [ "$CODE" -ne 0 ]; then
      echo "EGL-PROBE-DIAGNOSTIC: probe-exit-$CODE-no-verdict-line"
      exit 1
    fi
    echo "EGL-PROBE-DIAGNOSTIC: no-verdict-line"
    exit 1;;
esac
