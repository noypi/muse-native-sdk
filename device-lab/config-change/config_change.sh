#!/usr/bin/env bash
# config-change suite script (FR-RUNTIME-008, AC-RUNTIME-024).
#
# Validates the config-change evidence JSON produced by ConfigChangeProducer
# (`runConfigChange`), which performs rotation plus background/foreground
# transitions against the hosted engine tree with the tree preserved, stable
# node ids, and the surface re-created without crash or blank
# (never a standalone view).
# Exits 0 iff rotationPreserved, bgFgPreserved, nodeIdsStable, and
# engineHosted are all JSON boolean true, 1 otherwise (fail-closed).
#
# Evidence is pulled from the app-specific external files dir
# (Activity.getExternalFilesDir, per-package EXTERNAL_FILES base:
# /storage/emulated/0/Android/data/com.muse.devicelab/files/config-change/).
# DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH retargets the evidence path only; the
# verdict always validates the evidence file content, never the env var
# (RULE-RUNTIME-005). EVIDENCE_PATH (set by device-lab/run.sh dispatch) is
# honored the same way when the override is absent. No shared-root evidence
# paths are used.
#
# Usage: device-lab/config-change/config_change.sh [--help]
set -u

APP_ID="com.muse.devicelab"
RESULT_FILE="config-change-result.json"

usage() {
  echo "usage: config_change.sh [--help]" >&2
  echo "validates config-change-result.json from the app-specific external files dir;" >&2
  echo "DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH retargets the evidence path only." >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "config-change suite (FR-RUNTIME-008)"
  echo "usage: config_change.sh [--help]"
  echo "evidence: app-specific external files dir (getExternalFilesDir);"
  echo "  DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH retargets the evidence path only."
  exit 0
fi

# --- evidence path resolution (RULE-RUNTIME-005: override retargets path only) ---
resolve_evidence_file() {
  local override="${DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH:-}"
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
  echo "/storage/emulated/0/Android/data/$APP_ID/files/config-change/$RESULT_FILE"
}

EVIDENCE_FILE="$(resolve_evidence_file)"

if [[ ! -f "$EVIDENCE_FILE" ]]; then
  echo "config_change.sh: missing evidence at $EVIDENCE_FILE" >&2
  exit 1
fi

# --- content verdict: all four flags must be JSON boolean true (strict) ---
python3 - "$EVIDENCE_FILE" <<'EOF'
import json, sys

path = sys.argv[1]
try:
    with open(path) as f:
        doc = json.load(f)
except Exception as e:
    print(f"config_change.sh: invalid evidence json at {path}: {e}", file=sys.stderr)
    sys.exit(1)

if not isinstance(doc, dict):
    print(f"config_change.sh: invalid evidence at {path}: top-level object required", file=sys.stderr)
    sys.exit(1)

need = ["rotationPreserved", "bgFgPreserved", "nodeIdsStable", "engineHosted"]
bad = [k for k in need if doc.get(k) is not True]
if bad:
    print(f"config_change.sh: evidence at {path} fails: {', '.join(bad)} not boolean true", file=sys.stderr)
    sys.exit(1)
sys.exit(0)
EOF
