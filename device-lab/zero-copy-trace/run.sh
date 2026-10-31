#!/usr/bin/env bash
# zero-copy-trace suite (NFR-PVIEWS-002 / AC-PVIEWS-006, TC-PVIEWS-006).
#
# Usage: run.sh [--serial 127.0.0.1:6562]
#   No args pins ANDROID_SERIAL=127.0.0.1:6562 (env or default); exactly one
#   optional flag --serial <serial> is accepted and must equal 127.0.0.1:6562.
#   Any other arg, missing --serial value, extra args, or a non-target serial
#   exits 2 (usage/serial-mismatch, fail closed, no device touched).
#
#   Exit 0 iff the Perfetto-trace evidence JSON validates:
#     {noMemcpy: true, zOrderOk: true, handlesLeaked: 0}
#   (strict types via validate.py). Any evidence failure exits 1.
#
# Evidence: local evidence file validated by validate.py; the on-device
# Perfetto trace pull lands at EVIDENCE_PATH when set by the device run,
# otherwise ./evidence/zero-copy-trace-result.json next to this script.
# ZERO_COPY_TRACE_EVIDENCE_PATH retargets the evidence path only; the verdict
# always validates the evidence file content, never the env var.
#
# Hermetic gate runs before any device contact: validate.py --self-test plus
# bash syntax self-check must pass, else exit 1 without touching adb.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TARGET_SERIAL="127.0.0.1:6562"
RESULT_FILE="zero-copy-trace-result.json"

usage() {
  echo "usage: run.sh [--serial 127.0.0.1:6562]" >&2
  echo "serial pin: ANDROID_SERIAL=127.0.0.1:6562 (fail closed on any other serial)" >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "zero-copy-trace suite (NFR-PVIEWS-002 / AC-PVIEWS-006)"
  echo "usage: run.sh [--serial 127.0.0.1:6562]"
  echo "evidence: {noMemcpy: true, zOrderOk: true, handlesLeaked: 0} validated by validate.py;"
  echo "  ZERO_COPY_TRACE_EVIDENCE_PATH retargets the evidence path only."
  exit 0
fi

# --- arg + serial pin (fail closed, exit 2; no device touched) ---
SERIAL=""
if [[ $# -eq 0 ]]; then
  SERIAL="${ANDROID_SERIAL:-$TARGET_SERIAL}"
elif [[ $# -eq 2 && "$1" == "--serial" ]]; then
  SERIAL="$2"
  if [[ -n "${ANDROID_SERIAL:-}" && "$ANDROID_SERIAL" != "$TARGET_SERIAL" ]]; then
    echo "run.sh: serial mismatch: ANDROID_SERIAL=$ANDROID_SERIAL (want $TARGET_SERIAL)" >&2
    exit 2
  fi
else
  usage
  exit 2
fi
if [[ "$SERIAL" != "$TARGET_SERIAL" ]]; then
  echo "run.sh: serial mismatch: got '$SERIAL' (want $TARGET_SERIAL)" >&2
  exit 2
fi
export ANDROID_SERIAL="$TARGET_SERIAL"

# --- hermetic gate first: never touch a device when the host check is red ---
run_hermetic_gate() {
  bash -n "$SCRIPT_DIR/run.sh" || return 1
  python3 "$SCRIPT_DIR/validate.py" --self-test || return 1
}

if ! run_hermetic_gate; then
  echo "run.sh: hermetic gate (validate.py --self-test) failed — refusing device run" >&2
  exit 1
fi

# --- evidence path resolution (override retargets path only) ---
resolve_evidence_file() {
  local override="${ZERO_COPY_TRACE_EVIDENCE_PATH:-}"
  if [[ -n "$override" ]]; then
    if [[ "$override" == *.json ]]; then
      echo "$override"
    else
      echo "$override/$RESULT_FILE"
    fi
    return 0
  fi
  local dispatch_path="${EVIDENCE_PATH:-}"
  if [[ -n "$dispatch_path" ]]; then
    if [[ "$dispatch_path" == *.json ]]; then
      echo "$dispatch_path"
    else
      echo "$dispatch_path/$RESULT_FILE"
    fi
    return 0
  fi
  echo "$SCRIPT_DIR/evidence/$RESULT_FILE"
}

ADB="${DEVICE_LAB_ADB:-adb}"

# --- device contact (serial-pinned; fail closed) ---
if ! "$ADB" -s "$TARGET_SERIAL" get-state >/dev/null 2>&1; then
  echo "run.sh: device $TARGET_SERIAL unreachable (adb -s $TARGET_SERIAL get-state failed)" >&2
  exit 1
fi

EVIDENCE_FILE="$(resolve_evidence_file)"

# Pull the on-device Perfetto-derived evidence JSON when it lives on the
# device; a local evidence file (tmp-dir fixture or a prior pull) is used
# as-is. Missing evidence fails closed.
if [[ "$EVIDENCE_FILE" == /storage/* || "$EVIDENCE_FILE" == /sdcard/* ]]; then
  LOCAL_COPY="$(mktemp)"
  trap 'rm -f "$LOCAL_COPY"' EXIT
  if ! "$ADB" -s "$TARGET_SERIAL" pull "$EVIDENCE_FILE" "$LOCAL_COPY" >/dev/null 2>&1; then
    echo "run.sh: missing evidence at $EVIDENCE_FILE" >&2
    exit 1
  fi
  EVIDENCE_FILE="$LOCAL_COPY"
fi

if [[ ! -f "$EVIDENCE_FILE" ]]; then
  echo "run.sh: missing evidence at $EVIDENCE_FILE" >&2
  exit 1
fi

if ! python3 "$SCRIPT_DIR/validate.py" "$EVIDENCE_FILE"; then
  echo "run.sh: suite zero-copy-trace failed evidence check" >&2
  exit 1
fi
exit 0
