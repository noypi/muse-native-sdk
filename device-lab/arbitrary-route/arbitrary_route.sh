#!/usr/bin/env bash
# arbitrary-route suite script (FR-NAV-011, AC-NAV-034; TC-NAV-034 external half).
#
# Validates the arbitrary-route evidence JSON produced on device by a single
# navigate to an arbitrary NON-ADJACENT route with params (a jump of >1 step
# in one invoke, mirroring the hermetic T2 `arbitrary_navigate_test` §(d)),
# issued against the hosted engine tree (never a standalone view).
# Exits 0 iff destinationReached, !singleStepAdvance, and engineHosted hold
# with the destination id equal to the requested id and params round-tripped;
# 1 otherwise (fail-closed).
#
# Evidence is pulled from the app-specific external files dir
# (Activity.getExternalFilesDir, per-package EXTERNAL_FILES base:
# /storage/emulated/0/Android/data/com.muse.devicelab/files/arbitrary-route/).
# DEVICE_LAB_ARBITRARY_ROUTE_EVIDENCE_PATH retargets the evidence path only;
# the verdict always validates the evidence file content, never the env var
# (RULE-RUNTIME-005). EVIDENCE_PATH (set by device-lab/run.sh dispatch) is
# honored the same way when the override is absent. No shared-root evidence
# paths are used.
#
# Evidence schema (exact):
#   { "route": "<non-adjacent-id>", "params": { … },
#     "destinationReached": true, "singleStepAdvance": false,
#     "engineHosted": true }
#
# Optional request pinning (path-only request description, verdict still from
# content): ARBITRARY_ROUTE_ID pins the requested destination id — when set,
# evidence "route" must equal it. ARBITRARY_ROUTE_EXPECTED_PARAMS (a JSON
# object literal) pins the requested params — when set, evidence "params"
# must equal it. When unset, "route" must be a non-empty string (adjacent
# ids alone never satisfy the lane: the producer jumps >1 step in one
# invoke) and "params" must be a JSON object.
#
# Usage: device-lab/arbitrary-route/arbitrary_route.sh [--help]
set -u

APP_ID="com.muse.devicelab"
RESULT_FILE="arbitrary-route-result.json"

usage() {
  echo "usage: arbitrary_route.sh [--help]" >&2
  echo "validates arbitrary-route-result.json from the app-specific external files dir;" >&2
  echo "DEVICE_LAB_ARBITRARY_ROUTE_EVIDENCE_PATH retargets the evidence path only." >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "arbitrary-route suite (FR-NAV-011)"
  echo "usage: arbitrary_route.sh [--help]"
  echo "evidence: app-specific external files dir (getExternalFilesDir);"
  echo "  DEVICE_LAB_ARBITRARY_ROUTE_EVIDENCE_PATH retargets the evidence path only."
  exit 0
fi

# --- evidence path resolution (RULE-RUNTIME-005: override retargets path only) ---
resolve_evidence_file() {
  local override="${DEVICE_LAB_ARBITRARY_ROUTE_EVIDENCE_PATH:-}"
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
  echo "/storage/emulated/0/Android/data/$APP_ID/files/arbitrary-route/$RESULT_FILE"
}

EVIDENCE_FILE="$(resolve_evidence_file)"

if [[ ! -f "$EVIDENCE_FILE" ]]; then
  echo "arbitrary_route.sh: missing evidence at $EVIDENCE_FILE" >&2
  exit 1
fi

# --- content verdict (fail closed on any divergence) ---
python3 - "$EVIDENCE_FILE" <<'EOF'
import json, os, sys

path = sys.argv[1]
try:
    with open(path) as f:
        doc = json.load(f)
except Exception as e:
    print(f"arbitrary_route.sh: invalid evidence json at {path}: {e}", file=sys.stderr)
    sys.exit(1)

if not isinstance(doc, dict):
    print(f"arbitrary_route.sh: invalid evidence at {path}: top-level object required", file=sys.stderr)
    sys.exit(1)

# Verdict flags (exact schema): reached ∧ ¬single-step ∧ hosted.
# singleStepAdvance must be JSON boolean false: no product lane retains
# forward-only +1 paging semantics in this lane.
if doc.get("destinationReached") is not True:
    print(f"arbitrary_route.sh: evidence at {path} fails: destinationReached not boolean true", file=sys.stderr)
    sys.exit(1)
if doc.get("singleStepAdvance") is not False:
    print(f"arbitrary_route.sh: evidence at {path} fails: singleStepAdvance not boolean false (+1 stepping loop?)", file=sys.stderr)
    sys.exit(1)
if doc.get("engineHosted") is not True:
    print(f"arbitrary_route.sh: evidence at {path} fails: engineHosted not boolean true", file=sys.stderr)
    sys.exit(1)

# Destination id equals requested id: when ARBITRARY_ROUTE_ID pins the
# request, route must equal it; otherwise route must be a non-empty string.
route = doc.get("route")
requested = os.environ.get("ARBITRARY_ROUTE_ID", "")
if not isinstance(route, str) or not route:
    print(f"arbitrary_route.sh: evidence at {path} fails: route must be a non-empty string", file=sys.stderr)
    sys.exit(1)
if requested and route != requested:
    print(f"arbitrary_route.sh: evidence at {path} fails: route {route!r} != requested {requested!r}", file=sys.stderr)
    sys.exit(1)

# Params round-tripped: params must be a JSON object; when
# ARBITRARY_ROUTE_EXPECTED_PARAMS pins the request params, they must match.
params = doc.get("params")
if not isinstance(params, dict):
    print(f"arbitrary_route.sh: evidence at {path} fails: params must be an object", file=sys.stderr)
    sys.exit(1)
expected_raw = os.environ.get("ARBITRARY_ROUTE_EXPECTED_PARAMS", "")
if expected_raw:
    try:
        expected = json.loads(expected_raw)
    except Exception as e:
        print(f"arbitrary_route.sh: invalid ARBITRARY_ROUTE_EXPECTED_PARAMS: {e}", file=sys.stderr)
        sys.exit(1)
    if params != expected:
        print(f"arbitrary_route.sh: evidence at {path} fails: params not round-tripped", file=sys.stderr)
        sys.exit(1)

# Exactly one navigate issued (no +1 stepping loop): when the producer
# records navigateCount, it must be exactly 1.
if "navigateCount" in doc and doc.get("navigateCount") != 1:
    print(f"arbitrary_route.sh: evidence at {path} fails: navigateCount must be exactly 1", file=sys.stderr)
    sys.exit(1)

sys.exit(0)
EOF
