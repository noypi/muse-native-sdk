#!/usr/bin/env bash
# host-cold-start suite runner (FR-PVIEWS-010 / AC-PVIEWS-015, TC-PVIEWS-015).
#
# Verifies the brownfield sample: stock app hosts EngineView in an existing
# Fragment (one dependency addition); shared-engine two-host isolation; host
# cold-start delta stays within the PERF-START budget (context only — this
# procedure reports delta vs budget, it does not redefine it).
#
# Usage: device-lab/host-cold-start/run.sh [--help]
# Exits 0 only when the evidence JSON validates; non-zero otherwise.
# Unknown flags or extra args exit 2 with usage.
#
# Evidence (host-side seam; on-device producer + sample TBD by the coder):
#   device-lab/host-cold-start/host-cold-start-result.json, or the path in
#   $HOST_COLD_START_EVIDENCE_PATH (retargets the path only; the verdict
#   always validates the file content, never the env var).
# Required JSON: {"hostsShareEngine": true, "uiIsolated": true,
#   "coldStartDeltaMs": <number>, "budgetMs": <number>}
# Verdict: hostsShareEngine && uiIsolated && coldStartDeltaMs <= budgetMs.
# Missing evidence or malformed JSON exits 1 (fail closed); budget overrun
# exits 1 naming COLD_START_OVER_BUDGET {deltaMs, budgetMs}. No device is
# touched by the host-side checks below; measurement requires device-lab.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_EVIDENCE="$SCRIPT_DIR/host-cold-start-result.json"

usage() {
  echo "usage: run.sh [--help]" >&2
  echo "  evidence: JSON at device-lab/host-cold-start/host-cold-start-result.json" >&2
  echo "  (HOST_COLD_START_EVIDENCE_PATH retargets the evidence path only)" >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "host-cold-start check (FR-PVIEWS-010 / AC-PVIEWS-015, TC-PVIEWS-015)"
  echo "usage: run.sh [--help]"
  echo "  requires JSON {hostsShareEngine: true, uiIsolated: true,"
  echo "  coldStartDeltaMs <= budgetMs}; overrun reports COLD_START_OVER_BUDGET."
  exit 0
fi

if [[ $# -ne 0 ]]; then
  usage
  exit 2
fi

EVIDENCE="${HOST_COLD_START_EVIDENCE_PATH:-$DEFAULT_EVIDENCE}"
if [[ ! -f "$EVIDENCE" ]]; then
  echo "run.sh: FAIL — missing evidence at $EVIDENCE" >&2
  echo "run.sh: cold-start check needs device-lab (no evidence, refusing PASS)" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "run.sh: FAIL — python3 required to validate evidence JSON" >&2
  exit 1
fi

VERDICT="$(python3 - "$EVIDENCE" <<'PY'
import json, sys
try:
    with open(sys.argv[1]) as f:
        d = json.load(f)
except Exception as e:
    print(f"INVALID: malformed evidence json: {e}")
    sys.exit(0)
ok_share = d.get("hostsShareEngine") is True
ok_iso = d.get("uiIsolated") is True
delta = d.get("coldStartDeltaMs")
budget = d.get("budgetMs")
if not ok_share:
    print("FAIL: hostsShareEngine != true")
elif not ok_iso:
    print("FAIL: uiIsolated != true")
elif isinstance(delta, bool) or not isinstance(delta, (int, float)) or isinstance(budget, bool) or not isinstance(budget, (int, float)):
    print("INVALID: coldStartDeltaMs/budgetMs must be numbers")
elif delta > budget:
    print(f"COLD_START_OVER_BUDGET {{ deltaMs: {delta}, budgetMs: {budget} }}")
else:
    print(f"PASS: share+isolation ok, delta {delta}ms within budget {budget}ms")
PY
)"
echo "run.sh: $VERDICT"
case "$VERDICT" in
  PASS*) exit 0 ;;
  *) exit 1 ;;
esac
