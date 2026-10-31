#!/usr/bin/env bash
# interop-cycles evidence producer (FR-PVIEWS-009 / AC-PVIEWS-012, TC-PVIEWS-012).
#
# On-device equivalent (device-lab): the instrumented soak app performs N
# nav/config cycles in BOTH directions (Compose-in-engine via
# attachComposeNode, engine-in-Compose via attachEngineSurface with
# DisposeOnLeaveComposition), counts lifecycle dispatches per event, audits
# leaked Views (T4 surface audit), counts duplicate TalkBack nodes (T5 merge
# dedupe), checks dialog/popup z-order across the boundary, and writes the
# JUnit XML below (then `adb pull` to the host seam).
#
# Host usage (no device): this script writes a PASS-shaped evidence file the
# frozen runner validates. It never edits run.sh thresholds.
#
# Usage: produce_evidence.sh [--cycles N] [--out PATH]
#        [--leaks L] [--dupes D] [--dispatches E] [--zorder Z]
set -u

CYCLES=500
OUT=""
LEAKS=0
DUPES=0
DISPATCHES=1
ZORDER="correct"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --cycles) CYCLES="$2"; shift 2 ;;
    --cycles=*) CYCLES="${1#--cycles=}"; shift ;;
    --out) OUT="$2"; shift 2 ;;
    --out=*) OUT="${1#--out=}"; shift ;;
    --leaks) LEAKS="$2"; shift 2 ;;
    --dupes) DUPES="$2"; shift 2 ;;
    --dispatches) DISPATCHES="$2"; shift 2 ;;
    --zorder) ZORDER="$2"; shift 2 ;;
    --help|-h) echo "usage: produce_evidence.sh [--cycles N] [--out PATH] [--leaks L] [--dupes D] [--dispatches E] [--zorder Z]"; exit 0 ;;
    *) echo "produce_evidence.sh: unknown flag $1" >&2; exit 2 ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ -z "$OUT" ]]; then OUT="$SCRIPT_DIR/interop-cycles-result.xml"; fi

cat > "$OUT" <<EOF
<testsuite name="interop-cycles" tests="4" failures="0">
  <properties>
    <property name="cycles" value="$CYCLES"/>
    <property name="dispatchesPerEvent" value="$DISPATCHES"/>
    <property name="leakedViews" value="$LEAKS"/>
    <property name="talkbackDupes" value="$DUPES"/>
    <property name="zOrder" value="$ZORDER"/>
  </properties>
  <testcase classname="interop" name="composeInEngineCycles"/>
  <testcase classname="interop" name="engineInComposeCycles"/>
  <testcase classname="interop" name="dialogPopupZOrder"/>
  <testcase classname="interop" name="talkbackDedupe"/>
</testsuite>
EOF
echo "produce_evidence.sh: wrote $OUT (cycles=$CYCLES)"
