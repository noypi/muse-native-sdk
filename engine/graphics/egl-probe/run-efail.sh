#!/bin/sh
# run-efail.sh — T5 EFAIL runner (FR-GRAPHICS-011, TC-GRAPHICS-032).
# Hermetic host test mirroring engine/graphics/device/run-t1.sh pattern:
# compiles T1 sources with g++ -std=c++17, output to /tmp, timeout-guarded,
# no crash/hang/skip. Prints EFAIL-VERDICT and exits accordingly.
#
# Usage: run-efail.sh [--device SERIAL]
#   Default device is 127.0.0.1:6562 (or $ANDROID_SERIAL when set).
#   Every leg executes or the script FAILs — there is no pending/skip path.
set -u

HERE=$(dirname "$0")
# Absolute probe dir so the script works from any cwd (checkout root or here).
PROBE_DIR=$(cd "$HERE" && pwd)
TIMEOUT_SECS=${EFAIL_TIMEOUT_SECS:-30}
OUT=/tmp/nfr_efail.test
STAGES="init config context surface"
UNKNOWN_STAGE="bogus-stage-efail"

DEVICE="${ANDROID_SERIAL:-127.0.0.1:6562}"
while [ $# -gt 0 ]; do
  case "$1" in
    --device)
      [ $# -lt 2 ] && { echo "EFAIL-VERDICT: FAIL --device needs a SERIAL value"; exit 1; }
      DEVICE="$2"; shift 2;;
    -h|--help)
      echo "Usage: run-efail.sh [--device SERIAL] (default: 127.0.0.1:6562)"; exit 0;;
    --)
      shift; break;;
    -*)
      echo "EFAIL-VERDICT: FAIL unknown option: $1"; exit 1;;
    *)
      echo "EFAIL-VERDICT: FAIL unexpected argument: $1"; exit 1;;
  esac
done
export ANDROID_SERIAL="$DEVICE"

fail() {
  echo "EFAIL-VERDICT: FAIL $1"
  exit 1
}

blocked() {
  echo "BLOCKED $DEVICE $1"
  echo "EFAIL-VERDICT: FAIL BLOCKED $DEVICE $1"
  exit 1
}

need_timeout() {
  command -v timeout >/dev/null 2>&1 || fail "timeout(1) missing — cannot guard legs against hangs"
}

# Run a leg under the timeout guard; echoes the exit code. Usage: run_guarded LOG -- cmd...
run_guarded() {
  _log="$1"; shift
  # shellcheck disable=SC2068
  timeout "$TIMEOUT_SECS" $@ >"$_log" 2>&1
  echo $?
}

# Assert a completed force-fail leg: nonzero, no signal/timeout, diagnostic.
# $1 = label, $2 = exit code, $3 = log file, $4 = expected stage substring.
assert_fail_leg() {
  _label="$1"; _code="$2"; _log="$3"; _stage="$4"
  [ "$_code" -eq 0 ] && fail "$_label exited 0, want nonzero (see $_log)"
  [ "$_code" -eq 124 ] && fail "$_label timed out after ${TIMEOUT_SECS}s (hang)"
  [ "$_code" -ge 128 ] && fail "$_label crashed (signal exit $_code)"
  grep -q "EGL-PROBE-DIAGNOSTIC" "$_log" || fail "$_label log missing 'EGL-PROBE-DIAGNOSTIC' (see $_log)"
  grep -q "$_stage" "$_log" || fail "$_label log missing diagnostic substring '$_stage' (see $_log)"
  echo "PASS $_label (exit $_code, diagnostic names '$_stage')"
}

need_timeout

# 1. Build the host probe binary (Makefile host target; direct g++ fallback
#    mirroring run-t1.sh). The binary MUST exist — no pending/skip path.
if [ ! -x "$PROBE_DIR/egl-probe-host" ]; then
  echo "--- building egl-probe-host (make host) ---"
  if ! make -C "$PROBE_DIR" host >/tmp/nfr_efail.host-build.log 2>&1; then
    echo "--- make host failed; fallback direct g++ line ---"
    cat /tmp/nfr_efail.host-build.log
    if ! g++ -std=c++17 -Wall -Wextra -Werror -O2 -I "$PROBE_DIR/.." \
      "$PROBE_DIR/egl_probe.cc" "$PROBE_DIR/egl_probe_main.cc" \
      -o "$PROBE_DIR/egl-probe-host" -ldl 2>/tmp/nfr_efail.host-build.log; then
      cat /tmp/nfr_efail.host-build.log
      fail "egl-probe-host build failed (make host + g++ fallback)"
    fi
  fi
fi
[ -x "$PROBE_DIR/egl-probe-host" ] || fail "egl-probe-host binary absent after build — matrix cannot run"
echo "host binary: $PROBE_DIR/egl-probe-host"

# 2. Compile the hermetic force-fail test against the T1 sources.
if ! g++ -std=c++17 -I "$PROBE_DIR/.." \
  "$PROBE_DIR/egl_probe_force_fail.test.cpp" \
  "$PROBE_DIR/egl_probe.cc" \
  -o "$OUT" 2>/tmp/nfr_efail.build.log; then
  echo "--- build log (/tmp/nfr_efail.build.log) ---"
  cat /tmp/nfr_efail.build.log
  fail "compile-error"
fi

# 3. Run the compiled test binary under the timeout guard (no hang).
if ! timeout "$TIMEOUT_SECS" "$OUT"; then
  fail "force-fail-test exited nonzero"
fi

# 4. Host stage matrix: every stage init|config|context|surface must exit
#    nonzero with a stage-naming diagnostic. Every leg executes or FAILs.
for s in $STAGES; do
  LOG=/tmp/nfr_efail.stage.$s.log
  code=$(run_guarded "$LOG" "$PROBE_DIR/egl-probe-host" --force-fail "$s")
  assert_fail_leg "host stage $s" "$code" "$LOG" "$s"
done

# 5. Unknown-stage case: nonzero + diagnostic, never crash/hang/skip.
LOG=/tmp/nfr_efail.stage.unknown.log
code=$(run_guarded "$LOG" "$PROBE_DIR/egl-probe-host" --force-fail "$UNKNOWN_STAGE")
assert_fail_leg "host unknown-stage $UNKNOWN_STAGE" "$code" "$LOG" "$UNKNOWN_STAGE"

# 6. On-device legs against $DEVICE (default 127.0.0.1:6562):
#    muse-native egl-probe --force-fail <stage> --expect-exit nonzero
#    --expect-log EGL-PROBE-DIAGNOSTIC, all four stages, -s on every adb call.
#    Unreachable device => BLOCKED-with-reason (never a silent skip).
if ! command -v adb >/dev/null 2>&1; then
  blocked "adb not found on PATH"
fi
FINGERPRINT=""
if ! timeout "$TIMEOUT_SECS" adb -s "$DEVICE" shell getprop ro.build.fingerprint >/tmp/nfr_efail.fp.log 2>&1; then
  blocked "unreachable ($(cat /tmp/nfr_efail.fp.log | tr '\n' ' ' | cut -c1-160))"
fi
FINGERPRINT=$(tr -d '\r\n ' </tmp/nfr_efail.fp.log)
ABI=$(timeout "$TIMEOUT_SECS" adb -s "$DEVICE" shell getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r\n ')
echo "device $DEVICE reachable (fingerprint: $FINGERPRINT; abi: $ABI)"

# Dispatch: prefer `muse-native egl-probe` when it implements the subcommand
# (the T2 CLI wiring; detected by its --help advertising --force-fail —
# the bare `muse-native --help` fallback exits 0 without it), else the
# engine dispatch script (same flags, thin wrapper over the probe binary).
if muse-native egl-probe --help 2>&1 | grep -q "force-fail"; then
  EPROBE="muse-native egl-probe"
else
  EPROBE="sh $PROBE_DIR/run-eprobe.sh"
  echo "note: installed muse-native has no egl-probe subcommand; dispatching via run-eprobe.sh (same flags)"
fi
for s in $STAGES; do
  LOG=/tmp/nfr_efail.device.$s.log
  # shellcheck disable=SC2086
  code=$(run_guarded "$LOG" $EPROBE --device "$DEVICE" --force-fail "$s" --expect-exit nonzero --expect-log EGL-PROBE-DIAGNOSTIC)
  if [ "$code" -eq 124 ]; then
    blocked "leg $s timed out after ${TIMEOUT_SECS}s (hang)"
  fi
  [ "$code" -ge 128 ] && fail "device $DEVICE stage $s crashed (signal exit $code)"
  [ "$code" -eq 0 ] && fail "device $DEVICE stage $s exited 0, want nonzero (see $LOG)"
  grep -q "EGL-PROBE-DIAGNOSTIC" "$LOG" || fail "device $DEVICE stage $s log missing 'EGL-PROBE-DIAGNOSTIC' (see $LOG)"
  grep -q "$s" "$LOG" || fail "device $DEVICE stage $s log missing diagnostic substring '$s' (see $LOG)"
  echo "PASS device $DEVICE stage $s (exit $code, diagnostic names '$s')"
done

echo "EFAIL-VERDICT: PASS"
exit 0
