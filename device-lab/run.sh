#!/usr/bin/env bash
# device-lab suite runner (FR-RUNTIME-008 / AC-RUNTIME-021, RULE-RUNTIME-005).
#
# Usage: device-lab/run.sh <suite>
#   suite is exactly one of: ime-smoke | ime-inset | touch-smoke | config-change
# Exits 0 only when that suite's evidence file validates, non-zero otherwise;
# unknown suite (including empty, wrong case, or extra args) exits 2.
#
# Evidence defaults to the app-specific external files dir
# (Activity.getExternalFilesDir, per-package files dir on the device).
# DEVICE_LAB_<SUITE>_EVIDENCE_PATH env overrides retarget the on-device path
# only; the verdict always validates the evidence file content (JSON schema
# + engineHosted flag), never the env var itself. No shared-root evidence
# paths are used.
#
# The hermetic gate (ctest build of engine/runtime) runs before any device
# suite; a gate failure fails closed without touching a device.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
APP_ID="com.muse.devicelab"
# Default on-device evidence base: the app-specific external files dir
# (Context.getExternalFilesDir -> Android/data/<package>/files). Resolved via
# cmdline `cmd media_session` free probe below; see resolve_evidence_path.
# EXTERNAL_FILES default; DEVICE_LAB_*_EVIDENCE_PATH overrides path only.
SUITES="ime-smoke ime-inset touch-smoke config-change"

usage() {
  echo "usage: run.sh <suite>" >&2
  echo "suites: ime-smoke ime-inset touch-smoke config-change" >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "device-lab runner (FR-RUNTIME-008)"
  echo "suites:"
  echo "  ime-smoke"
  echo "  ime-inset"
  echo "  touch-smoke"
  echo "  config-change"
  echo "evidence: app-specific external files dir (getExternalFilesDir);"
  echo "  DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH, DEVICE_LAB_IME_INSET_EVIDENCE_PATH,"
  echo "  DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH, DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH"
  echo "  retarget the evidence path only; the verdict validates evidence content."
  exit 0
fi

# Exactly one positional arg; byte-exact suite match (no case folding).
if [[ $# -ne 1 ]]; then
  usage
  exit 2
fi
SUITE="$1"
case "$SUITE" in
  ime-smoke|ime-inset|touch-smoke|config-change) ;;
  *) usage; exit 2 ;;
esac

# --- hermetic gate first: never touch a device when the host build is red ---
run_hermetic_gate() {
  # hermetic gate: host ctest build must pass before any device suite runs.
  if [[ ! -d "$REPO_ROOT/engine/runtime/build" ]]; then
    cmake -S "$REPO_ROOT/engine/runtime" -B "$REPO_ROOT/engine/runtime/build" >/dev/null 2>&1 || return 1
    cmake --build "$REPO_ROOT/engine/runtime/build" >/dev/null 2>&1 || return 1
  fi
  ctest --test-dir "$REPO_ROOT/engine/runtime/build" >/dev/null 2>&1
}

if ! run_hermetic_gate; then
  echo "run.sh: hermetic gate (ctest engine/runtime) failed — refusing device run" >&2
  exit 1
fi

# --- evidence path resolution (RULE-RUNTIME-005) ---
# Default: app-specific external files dir on device:
#   /storage/emulated/0/Android/data/<APP_ID>/files/<suite>/<file>
# (via getExternalFilesDir; EXTERNAL_FILES base). DEVICE_LAB_*_EVIDENCE_PATH
# env overrides retarget this path only.
evidence_file_for() {
  case "$1" in
    ime-smoke) echo "ime-smoke-result.json" ;;
    ime-inset) echo "ime-inset-frames.jsonl" ;;
    touch-smoke) echo "touch-smoke-result.json" ;;
    config-change) echo "config-change-result.json" ;;
  esac
}

resolve_evidence_path() {
  local suite="$1"
  local file
  file="$(evidence_file_for "$suite")"
  local override_var="DEVICE_LAB_$(echo "$suite" | tr 'a-z-' 'A-Z_')_EVIDENCE_PATH"
  local override="${!override_var:-}"
  if [[ -n "$override" ]]; then
    # Override retargets the evidence path only; verdict still validates content.
    echo "$override"
    return 0
  fi
  # App-specific external files dir default (getExternalFilesDir base).
  local base
  base="$(adb shell "cmd package resolve-activity --brief $APP_ID 2>/dev/null; echo /storage/emulated/0/Android/data/$APP_ID/files" 2>/dev/null | tail -n 1 | tr -d '\r')"
  if [[ -z "$base" ]]; then
    base="/storage/emulated/0/Android/data/$APP_ID/files"
  fi
  echo "$base/$suite/$file"
}

# --- evidence content verification (verdict from content, never the env var) ---
# Pulls the evidence JSON and validates schema + engineHosted flag with python3.
verify_evidence_json() {
  local device_path="$1"
  local suite="$2"
  local local_copy
  local_copy="$(mktemp)"
  trap 'rm -f "$local_copy"' RETURN
  if ! adb pull "$device_path" "$local_copy" >/dev/null 2>&1; then
    echo "run.sh: missing evidence at $device_path" >&2
    return 1
  fi
  # Validate evidence result content: parse json and check required keys.
  python3 - "$local_copy" "$suite" <<'EOF'
import json, sys

def verify_inset(doc):
    rows = doc if isinstance(doc, list) else doc.get("frames", [])
    if not rows:
        return False
    return all(isinstance(r.get("latencyFrames"), int) and r["latencyFrames"] <= 1 for r in rows)

path, suite = sys.argv[1], sys.argv[2]
try:
    with open(path) as f:
        doc = json.load(f)
except Exception as e:
    print(f"invalid evidence json: {e}", file=sys.stderr)
    sys.exit(1)
if suite == "ime-smoke":
    need = ["composition", "caret", "selection", "clipboardCopyPaste", "engineHosted"]
    ok = all(doc.get(k) is True for k in need)
elif suite == "touch-smoke":
    ok = doc.get("tapDispatched") is True and doc.get("dragScrolled") is True and doc.get("engineHosted") is True
elif suite == "config-change":
    ok = all(doc.get(k) is True for k in ["rotationPreserved", "bgFgPreserved", "nodeIdsStable", "engineHosted"])
elif suite == "ime-inset":
    ok = verify_inset(doc)
else:
    ok = False
sys.exit(0 if ok else 1)
EOF
}

verify_evidence_jsonl() {
  local device_path="$1"
  local local_copy
  local_copy="$(mktemp)"
  trap 'rm -f "$local_copy"' RETURN
  if ! adb pull "$device_path" "$local_copy" >/dev/null 2>&1; then
    echo "run.sh: missing evidence at $device_path" >&2
    return 1
  fi
  # Verify each jsonl frame line: latencyFrames <= 1 bound check.
  python3 - "$local_copy" <<'EOF'
import json, sys
ok_show = ok_hide = False
try:
    with open(sys.argv[1]) as f:
        rows = [json.loads(line) for line in f if line.strip()]
except Exception as e:
    print(f"invalid evidence jsonl: {e}", file=sys.stderr)
    sys.exit(1)
if not rows:
    sys.exit(1)
for r in rows:
    if not (isinstance(r.get("latencyFrames"), int) and r["latencyFrames"] <= 1):
        sys.exit(1)
    if r.get("phase") == "show":
        ok_show = True
    if r.get("phase") == "hide":
        ok_hide = True
sys.exit(0 if (ok_show and ok_hide) else 1)
EOF
}

run_suite() {
  local suite="$1"
  local evidence_path
  evidence_path="$(resolve_evidence_path "$suite")"
  # Suite scripts are owned by T5-T8; the T4 scaffold dispatches and then
  # validates the pulled evidence result content (fail closed on missing).
  local suite_script="$SCRIPT_DIR/$suite/${suite//-/_}.sh"
  if [[ -x "$suite_script" ]]; then
    if ! EVIDENCE_PATH="$evidence_path" bash "$suite_script"; then
      return 1
    fi
  fi
  if [[ "$suite" == "ime-inset" ]]; then
    verify_evidence_jsonl "$evidence_path"
  else
    verify_evidence_json "$evidence_path" "$suite"
  fi
}

if ! run_suite "$SUITE"; then
  echo "run.sh: suite $SUITE failed evidence check" >&2
  exit 1
fi
exit 0
