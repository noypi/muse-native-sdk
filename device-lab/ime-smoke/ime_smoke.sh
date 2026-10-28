#!/usr/bin/env bash
# ime-smoke suite script (FR-RUNTIME-008, AC-RUNTIME-021).
#
# Validates the ime-smoke evidence JSON produced by ImeSmokeProducer
# (`runImeSmoke`), which drives platform IME InputConnection composition plus
# caret/selection and clipboard copy/paste against the hosted engine field.
# Exits 0 iff all five flags are JSON boolean true, 1 otherwise (fail-closed).
#
# Evidence is pulled from the app-specific external files dir
# (Activity.getExternalFilesDir, per-package EXTERNAL_FILES base:
# /storage/emulated/0/Android/data/com.muse.devicelab/files/ime-smoke/).
# DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH retargets the evidence path only; the
# verdict always validates the evidence file content, never the env var
# (RULE-RUNTIME-005). EVIDENCE_PATH (set by device-lab/run.sh dispatch) is
# honored the same way when the override is absent. No shared-root evidence
# paths are used.
#
# Usage: device-lab/ime-smoke/ime_smoke.sh [--help]
set -u

APP_ID="com.muse.devicelab"
RESULT_FILE="ime-smoke-result.json"

usage() {
  echo "usage: ime_smoke.sh [--help]" >&2
  echo "validates ime-smoke-result.json from the app-specific external files dir;" >&2
  echo "DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH retargets the evidence path only." >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "ime-smoke suite (FR-RUNTIME-008)"
  echo "usage: ime_smoke.sh [--help]"
  echo "evidence: app-specific external files dir (getExternalFilesDir);"
  echo "  DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH retargets the evidence path only."
  exit 0
fi

# --- evidence path resolution (RULE-RUNTIME-005: override retargets path only) ---
resolve_evidence_file() {
  local override="${DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH:-}"
  if [[ -n "$override" ]]; then
    # Override retargets the evidence path only; verdict still validates content.
    if [[ "$override" == *.json ]]; then
      echo "$override"
    else
      echo "$override/$RESULT_FILE"
    fi
    return 0
  fi
  # device-lab/run.sh dispatch passes the resolved on-device evidence location.
  local dispatch_path="${EVIDENCE_PATH:-}"
  if [[ -n "$dispatch_path" ]]; then
    if [[ "$dispatch_path" == *.json ]]; then
      echo "$dispatch_path"
    else
      echo "$dispatch_path/$RESULT_FILE"
    fi
    return 0
  fi
  # Default: app-specific external files dir (getExternalFilesDir EXTERNAL_FILES
  # base for APP_ID); pulled locally for hermetic validation.
  echo "/storage/emulated/0/Android/data/$APP_ID/files/ime-smoke/$RESULT_FILE"
}

EVIDENCE_FILE="$(resolve_evidence_file)"

if [[ ! -f "$EVIDENCE_FILE" ]]; then
  echo "ime_smoke.sh: missing evidence at $EVIDENCE_FILE" >&2
  exit 1
fi

# --- content verdict: all five flags must be JSON boolean true (strict) ---
python3 - "$EVIDENCE_FILE" <<'EOF'
import json, sys

path = sys.argv[1]
try:
    with open(path) as f:
        doc = json.load(f)
except Exception as e:
    print(f"ime_smoke.sh: invalid evidence json at {path}: {e}", file=sys.stderr)
    sys.exit(1)

if not isinstance(doc, dict):
    print(f"ime_smoke.sh: invalid evidence at {path}: top-level object required", file=sys.stderr)
    sys.exit(1)

need = ["composition", "caret", "selection", "clipboardCopyPaste", "engineHosted"]
bad = [k for k in need if doc.get(k) is not True]
if bad:
    print(f"ime_smoke.sh: evidence at {path} fails: {', '.join(bad)} not boolean true", file=sys.stderr)
    sys.exit(1)
sys.exit(0)
EOF
