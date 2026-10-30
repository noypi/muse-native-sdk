#!/bin/sh
# run-eprobe.sh — T2 egl-probe runner (FR-GRAPHICS-011, AC-031/032).
# Implements the `muse-native egl-probe` dispatch surface as a thin wrapper:
# builds (if needed) then execs the per-ABI probe binary matching the target,
# forwards stdout/stderr unchanged, exits with the probe's exit code.
# `--expect-exit`/`--expect-log` assert and exit nonzero on mismatch.
# No EGL here and no EGL in packages (FR-PKG-007): the probe binary owns EGL.
#
# Usage:
#   run-eprobe.sh [--device SERIAL]... [--report drivers]
#                 [--force-fail STAGE] [--expect-exit CODE]
#                 [--expect-log SUBSTRING] [--help|-h]
#   muse-native egl-probe <same flags>  (thin dispatch; see note below)
#
# ABI selection: `adb shell getprop ro.product.cpu.abi` per target device;
# the matching `build/egl-probe-<abi>` artifact (see Makefile) is pushed to
# /data/local/tmp/egl-probe and run. With no --device (or no adb/device),
# the hermetic host binary ./egl-probe-host runs in-process.
set -u

HERE=$(dirname "$0")

print_help() {
  cat <<'EOF'
Usage: muse-native egl-probe [--device SERIAL]... [--report drivers] [--force-fail STAGE] [--expect-exit CODE] [--expect-log SUBSTRING]

Builds (if needed) then execs the per-ABI probe binary matching the target,
forwards stdout/stderr unchanged, exits with the probe's exit code.

Options:
  --device SERIAL     target device serial (repeatable; one EGL-PROBE-VERDICT line per target)
  --report drivers    print EGL version / client APIs / fence-sync presence
  --force-fail STAGE  force a failure at stage STAGE (init|config|context|surface)
  --expect-exit CODE  assert exit code (number, or `nonzero` for any nonzero); exit 2 on mismatch
  --expect-log TEXT   assert stdout/stderr contains TEXT; exit 2 on mismatch
  -h, --help          show this help
EOF
}

# --- parse args (thin dispatch; probe binary owns --force-fail/--report) ---
DEVICES=""
REPORT=""
FORCE_FAIL=""
EXPECT_EXIT=""
EXPECT_LOG=""
SHOW_HELP=0
while [ $# -gt 0 ]; do
  case "$1" in
    --device)
      [ $# -lt 2 ] && { echo "error: --device needs a SERIAL value" >&2; exit 2; }
      DEVICES="$DEVICES $2"; shift 2;;
    --report)
      [ $# -lt 2 ] && { echo "error: --report needs a value (drivers)" >&2; exit 2; }
      [ "$2" = "drivers" ] || { echo "error: --report: Expected drivers, got '$2'" >&2; exit 2; }
      REPORT="drivers"; shift 2;;
    --force-fail)
      [ $# -lt 2 ] && { echo "error: --force-fail needs a STAGE value" >&2; exit 2; }
      case "$2" in init|config|context|surface) ;; *) echo "error: --force-fail: Expected one of: init, config, context, surface" >&2; exit 2;; esac
      FORCE_FAIL="$2"; shift 2;;
    --expect-exit)
      [ $# -lt 2 ] && { echo "error: --expect-exit needs a CODE value" >&2; exit 2; }
      EXPECT_EXIT="$2"; shift 2;;
    --expect-log)
      [ $# -lt 2 ] && { echo "error: --expect-log needs a SUBSTRING value" >&2; exit 2; }
      EXPECT_LOG="$2"; shift 2;;
    -h|--help)
      SHOW_HELP=1; shift;;
    --)
      shift; break;;
    -*)
      echo "error: unknown option: $1" >&2; exit 2;;
    *)
      echo "error: unexpected argument: $1" >&2; exit 2;;
  esac
done

if [ "$SHOW_HELP" = "1" ]; then
  print_help
  exit 0
fi

# --- build (if needed): host binary + four ABI artifacts ---
if [ ! -x "$HERE/egl-probe-host" ]; then
  if ! make -C "$HERE" host; then
    echo "error: egl-probe host build failed" >&2
    exit 1
  fi
fi

# Forwarded probe args (subset the probe binary understands).
PROBE_ARGS=""
[ -n "$REPORT" ] && PROBE_ARGS="$PROBE_ARGS --report drivers"
[ -n "$FORCE_FAIL" ] && PROBE_ARGS="$PROBE_ARGS --force-fail $FORCE_FAIL"

# --- run one leg (host or single device), forwarding output unchanged ---
# $1 = serial (empty = host). Prints probe output verbatim; echoes exit code.
run_leg() {
  _serial="$1"
  _out=/tmp/nfr_eprobe.leg.$$.log
  if [ -z "$_serial" ]; then
    # shellcheck disable=SC2086
    "$HERE/egl-probe-host" $PROBE_ARGS >"$_out" 2>&1
    _code=$?
  else
    if ! command -v adb >/dev/null 2>&1; then
      echo "error: adb not found; cannot reach device '$_serial'" >&2
      rm -f "$_out"
      return 1
    fi
    _abi=$(adb -s "$_serial" shell getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r\n ')
    case "$_abi" in
      armeabi-v7a|arm64-v8a|x86|x86_64) ;;
      *)
        echo "error: device '$_serial': unknown ABI '$_abi' (ro.product.cpu.abi)" >&2
        rm -f "$_out"
        return 1;;
    esac
    _bin="$HERE/build/egl-probe-$_abi"
    if [ ! -f "$_bin" ]; then
      if ! make -C "$HERE" android; then
        echo "error: egl-probe android build failed (need ABI $_abi)" >&2
        rm -f "$_out"
        return 1
      fi
    fi
    if ! adb -s "$_serial" push "$_bin" /data/local/tmp/egl-probe >/dev/null 2>&1; then
      echo "error: device '$_serial': adb push failed" >&2
      rm -f "$_out"
      return 1
    fi
    # shellcheck disable=SC2086
    adb -s "$_serial" shell /data/local/tmp/egl-probe $PROBE_ARGS >"$_out" 2>&1
    _code=$?
  fi
  cat "$_out"
  rm -f "$_out"
  return $_code
}

OVERALL=0
if [ -z "$DEVICES" ]; then
  LEG_OUT=/tmp/nfr_eprobe.all.$$.log
  : >"$LEG_OUT"
  run_leg "" >"$LEG_OUT" 2>&1
  OVERALL=$?
  cat "$LEG_OUT"
else
  LEG_OUT=/tmp/nfr_eprobe.all.$$.log
  : >"$LEG_OUT"
  for d in $DEVICES; do
    run_leg "$d" >>"$LEG_OUT" 2>&1
    _c=$?
    if [ $_c -ne 0 ]; then OVERALL=$_c; fi
  done
  cat "$LEG_OUT"
fi

# --- --expect-exit / --expect-log assertions (exit nonzero on mismatch) ---
if [ -n "$EXPECT_EXIT" ]; then
  if [ "$EXPECT_EXIT" = "nonzero" ]; then
    if [ "$OVERALL" -eq 0 ]; then
      echo "error: --expect-exit nonzero: probe exited 0" >&2
      exit 2
    fi
  elif [ "$EXPECT_EXIT" = "zero" ]; then
    if [ "$OVERALL" -ne 0 ]; then
      echo "error: --expect-exit zero: probe exited $OVERALL" >&2
      exit 2
    fi
  else
    case "$EXPECT_EXIT" in
      ''|*[!0-9]*) echo "error: --expect-exit: Expected a number, zero, or nonzero" >&2; exit 2;;
    esac
    if [ "$OVERALL" -ne "$EXPECT_EXIT" ]; then
      echo "error: --expect-exit $EXPECT_EXIT: probe exited $OVERALL" >&2
      exit 2
    fi
  fi
fi
if [ -n "$EXPECT_LOG" ]; then
  if ! grep -qF -- "$EXPECT_LOG" "$LEG_OUT"; then
    echo "error: --expect-log '$EXPECT_LOG': substring not found in probe output" >&2
    rm -f "$LEG_OUT"
    exit 2
  fi
fi
rm -f "$LEG_OUT"

if [ -n "$EXPECT_EXIT$EXPECT_LOG" ]; then
  exit 0
fi
exit "$OVERALL"
