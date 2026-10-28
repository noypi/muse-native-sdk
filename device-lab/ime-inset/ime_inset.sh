#!/usr/bin/env bash
# ime-inset suite script (FR-RUNTIME-008, AC-RUNTIME-022).
#
# Validates the ime-inset evidence JSONL produced by ImeInsetProducer
# (`runImeInset`), which drives keyboard show+hide with per-frame inset
# sampling against the hosted engine field.
# Exits 0 iff the JSONL holds >=1 show frame (announcedHeight > 0) and >=1
# hide frame (announcedHeight == 0) with every frame's latencyFrames an int in
# [0,1]; exits 1 otherwise (fail-closed: missing/empty/invalid/mistyped/late
# or one-sided evidence).
#
# Evidence is pulled from the app-specific external files dir
# (Activity.getExternalFilesDir, per-package EXTERNAL_FILES base:
# /storage/emulated/0/Android/data/com.muse.devicelab/files/ime-inset/).
# DEVICE_LAB_IME_INSET_EVIDENCE_PATH retargets the evidence path only; the
# verdict always validates the evidence file content, never the env var
# (RULE-RUNTIME-005). EVIDENCE_PATH (set by device-lab/run.sh dispatch) is
# honored the same way when the override is absent. No shared-root evidence
# paths are used.
#
# Usage: device-lab/ime-inset/ime_inset.sh [--help]
set -u

APP_ID="com.muse.devicelab"
RESULT_FILE="ime-inset-frames.jsonl"

usage() {
  echo "usage: ime_inset.sh [--help]" >&2
  echo "validates ime-inset-frames.jsonl from the app-specific external files dir;" >&2
  echo "DEVICE_LAB_IME_INSET_EVIDENCE_PATH retargets the evidence path only." >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "ime-inset suite (FR-RUNTIME-008)"
  echo "usage: ime_inset.sh [--help]"
  echo "evidence: app-specific external files dir (getExternalFilesDir);"
  echo "  DEVICE_LAB_IME_INSET_EVIDENCE_PATH retargets the evidence path only."
  exit 0
fi

# --- evidence path resolution (RULE-RUNTIME-005: override retargets path only) ---
resolve_evidence_file() {
  local override="${DEVICE_LAB_IME_INSET_EVIDENCE_PATH:-}"
  if [[ -n "$override" ]]; then
    # Override retargets the evidence path only; verdict still validates content.
    if [[ "$override" == *.jsonl ]]; then
      echo "$override"
    else
      echo "$override/$RESULT_FILE"
    fi
    return 0
  fi
  # device-lab/run.sh dispatch passes the resolved on-device evidence location.
  local dispatch_path="${EVIDENCE_PATH:-}"
  if [[ -n "$dispatch_path" ]]; then
    if [[ "$dispatch_path" == *.jsonl ]]; then
      echo "$dispatch_path"
    else
      echo "$dispatch_path/$RESULT_FILE"
    fi
    return 0
  fi
  # Default: app-specific external files dir (getExternalFilesDir EXTERNAL_FILES
  # base for APP_ID); pulled locally for hermetic validation.
  echo "/storage/emulated/0/Android/data/$APP_ID/files/ime-inset/$RESULT_FILE"
}

EVIDENCE_FILE="$(resolve_evidence_file)"

if [[ ! -f "$EVIDENCE_FILE" ]]; then
  echo "ime_inset.sh: missing evidence at $EVIDENCE_FILE" >&2
  exit 1
fi

# --- content verdict: show+hide cycle, every latencyFrames int in [0,1] ---
python3 - "$EVIDENCE_FILE" <<'EOF'
import json, sys

path = sys.argv[1]
try:
    with open(path) as f:
        lines = [line for line in f if line.strip()]
except Exception as e:
    print(f"ime_inset.sh: invalid evidence at {path}: {e}", file=sys.stderr)
    sys.exit(1)

if not lines:
    print(f"ime_inset.sh: empty evidence at {path}", file=sys.stderr)
    sys.exit(1)

ok_show = ok_hide = False
for i, line in enumerate(lines):
    try:
        r = json.loads(line)
    except Exception as e:
        print(f"ime_inset.sh: invalid json on line {i} at {path}: {e}", file=sys.stderr)
        sys.exit(1)
    if not isinstance(r, dict):
        print(f"ime_inset.sh: line {i} at {path}: object required", file=sys.stderr)
        sys.exit(1)
    for key in ("frame", "announcedHeight", "appliedHeight", "latencyFrames"):
        if key not in r:
            print(f"ime_inset.sh: line {i} at {path}: missing key {key}", file=sys.stderr)
            sys.exit(1)
    frame, announced, applied, latency = (
        r["frame"], r["announcedHeight"], r["appliedHeight"], r["latencyFrames"],
    )
    # Strict int checks (bool is not an int here); heights must be ints >= 0.
    if type(frame) is not int or type(announced) is not int or type(applied) is not int:
        print(f"ime_inset.sh: line {i} at {path}: frame/heights must be ints", file=sys.stderr)
        sys.exit(1)
    if announced < 0 or applied < 0:
        print(f"ime_inset.sh: line {i} at {path}: negative height", file=sys.stderr)
        sys.exit(1)
    if type(latency) is not int or latency < 0 or latency > 1:
        print(f"ime_inset.sh: line {i} at {path}: latencyFrames must be int in [0,1]", file=sys.stderr)
        sys.exit(1)
    if announced > 0:
        ok_show = True
    elif announced == 0 and ok_show:
        # Hide counts only after a show frame: a leading zero baseline is not
        # a sampled hide; the show+hide cycle must complete in order.
        ok_hide = True

if not (ok_show and ok_hide):
    print(f"ime_inset.sh: evidence at {path}: show+hide cycle incomplete", file=sys.stderr)
    sys.exit(1)
sys.exit(0)
EOF
