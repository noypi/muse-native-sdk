#!/usr/bin/env bash
# host-cold-start evidence producer (FR-PVIEWS-010 / AC-PVIEWS-015, TC-PVIEWS-015).
#
# On-device equivalent (device-lab): launch the brownfield sample
# (examples/brownfield-sample) with two hosts on one shared engine, assert
# isolated UI state, measure host cold-start delta vs the PERF-START budget
# (context only — budget supplied as budgetMs, never redefined here), and
# write the JSON below (then `adb pull` to the host seam).
#
# Host usage (no device): this script writes an evidence file the frozen
# runner validates. It never edits run.sh thresholds.
#
# Usage: measure.sh [--out PATH] [--delta MS] [--budget MS]
#        [--shared true|false] [--isolated true|false]
set -u

OUT=""
DELTA=42
BUDGET=80
SHARED="true"
ISOLATED="true"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --out) OUT="$2"; shift 2 ;;
    --out=*) OUT="${1#--out=}"; shift ;;
    --delta) DELTA="$2"; shift 2 ;;
    --delta=*) DELTA="${1#--delta=}"; shift ;;
    --budget) BUDGET="$2"; shift 2 ;;
    --budget=*) BUDGET="${1#--budget=}"; shift ;;
    --shared) SHARED="$2"; shift 2 ;;
    --isolated) ISOLATED="$2"; shift 2 ;;
    --help|-h) echo "usage: measure.sh [--out PATH] [--delta MS] [--budget MS] [--shared BOOL] [--isolated BOOL]"; exit 0 ;;
    *) echo "measure.sh: unknown flag $1" >&2; exit 2 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "$OUT" ]]; then OUT="$SCRIPT_DIR/host-cold-start-result.json"; fi

python3 - "$OUT" "$SHARED" "$ISOLATED" "$DELTA" "$BUDGET" <<'PY'
import json, sys
out, shared, isolated, delta, budget = sys.argv[1:6]
def b(s): return s == "true"
d = {
    "hostsShareEngine": b(shared),
    "uiIsolated": b(isolated),
    "coldStartDeltaMs": float(delta) if "." in delta else int(delta),
    "budgetMs": float(budget) if "." in budget else int(budget),
}
with open(out, "w") as f:
    json.dump(d, f, indent=2)
print(f"measure.sh: wrote {out} (delta={delta}ms budget={budget}ms)")
PY
