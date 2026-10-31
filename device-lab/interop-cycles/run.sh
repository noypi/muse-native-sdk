#!/usr/bin/env bash
# interop-cycles suite runner (FR-PVIEWS-009 / AC-PVIEWS-012, TC-PVIEWS-012).
#
# Soak: a Compose screen embedded in the engine AND an engine surface embedded
# in Compose both survive N navigation/config cycles with exactly-once
# lifecycle dispatch, zero leaked Views, zero duplicate TalkBack nodes, and
# correct z-order for dialogs/popups crossing the boundary.
#
# Usage: device-lab/interop-cycles/run.sh [--cycles N] [--help]
#   --cycles N  number of nav/config cycles to require (default 500; positive int)
#   --help, -h  print this help and exit 0
# Exits 0 only when the evidence JUnit XML validates; non-zero otherwise.
# Unknown flags, non-numeric --cycles, or --cycles < 1 exit 2 with usage.
#
# Evidence (host-side seam; on-device producer TBD by the coder):
#   device-lab/interop-cycles/interop-cycles-result.xml (JUnit XML), or the
#   path in $INTEROP_CYCLES_EVIDENCE_PATH (retargets the path only; the
#   verdict always validates the file content, never the env var).
# Required properties (exact):
#   cycles=<required N>, dispatchesPerEvent=1, leakedViews=0,
#   talkbackDupes=0, zOrder=correct
# Missing evidence, malformed XML, a cycles shortfall, or any failing metric
# exits 1 naming the failing check (fail closed). No device is touched by the
# host-side checks below; the device run itself requires device-lab.
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_EVIDENCE="$SCRIPT_DIR/interop-cycles-result.xml"
CYCLES=500

usage() {
  echo "usage: run.sh [--cycles N] [--help]" >&2
  echo "  --cycles N: nav/config cycles to require (default 500, positive integer)" >&2
  echo "  evidence: JUnit XML at device-lab/interop-cycles/interop-cycles-result.xml" >&2
  echo "  (INTEROP_CYCLES_EVIDENCE_PATH retargets the evidence path only)" >&2
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  echo "interop-cycles soak (FR-PVIEWS-009 / AC-PVIEWS-012, TC-PVIEWS-012)"
  echo "usage: run.sh [--cycles N]"
  echo "  requires JUnit XML with {cycles, dispatchesPerEvent: 1, leakedViews: 0,"
  echo "  talkbackDupes: 0, zOrder: correct}; exit 0 iff every check passes."
  exit 0
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cycles)
      if [[ $# -lt 2 ]]; then usage; exit 2; fi
      CYCLES="$2"; shift 2
      ;;
    --cycles=*)
      CYCLES="${1#--cycles=}"; shift
      ;;
    *) usage; exit 2 ;;
  esac
done

if ! [[ "$CYCLES" =~ ^[0-9]+$ ]] || [[ "$CYCLES" -lt 1 ]]; then
  echo "run.sh: --cycles must be a positive integer (got '$CYCLES')" >&2
  usage
  exit 2
fi

EVIDENCE="${INTEROP_CYCLES_EVIDENCE_PATH:-$DEFAULT_EVIDENCE}"
if [[ ! -f "$EVIDENCE" ]]; then
  echo "run.sh: FAIL — missing evidence at $EVIDENCE" >&2
  echo "run.sh: interop soak needs device-lab (no evidence, refusing PASS)" >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "run.sh: FAIL — python3 required to validate evidence XML" >&2
  exit 1
fi

if ! python3 - "$EVIDENCE" "$CYCLES" <<'PY'; then
import sys
import xml.etree.ElementTree as ET
path, cycles = sys.argv[1], sys.argv[2]
try:
    with open(path, "rb") as f:
        raw = f.read()
    root = ET.fromstring(raw)
except Exception as e:
    print(f"run.sh: FAIL — invalid evidence xml: malformed ({e}) (see {path})")
    sys.exit(1)
# Strict element check: require a real <testsuite> element (comments ignored by the parser).
if root.tag != "testsuite" and root.find(".//testsuite") is None:
    print(f"run.sh: FAIL — invalid evidence xml: no <testsuite> in {path}")
    sys.exit(1)
# Strict property matching over real <property> elements only (XML comments
# are not elements, so comment-injected forgeries cannot satisfy a check).
props = {}
for p in root.iter("property"):
    props[p.get("name")] = p.get("value")
expected = {"cycles": cycles, "dispatchesPerEvent": "1", "leakedViews": "0",
            "talkbackDupes": "0", "zOrder": "correct"}
fail = 0
for name, want in expected.items():
    if props.get(name) == want:
        print(f"run.sh: PASS — {name}={want}")
    else:
        print(f"run.sh: FAIL — {name} != {want} (see {path})")
        fail = 1
sys.exit(fail)
PY
  echo "run.sh: FAIL — interop-cycles evidence check failed (cycles=$CYCLES)" >&2
  exit 1
fi
echo "run.sh: PASS — interop-cycles ($CYCLES cycles, single dispatch, no leaks, z-order correct)"
exit 0
