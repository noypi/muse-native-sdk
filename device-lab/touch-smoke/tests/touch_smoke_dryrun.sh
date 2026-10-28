#!/usr/bin/env bash
# Dry-run harness for T7 touch-smoke producer (no device, no network).
# Item: T7 — touch-smoke producer: real tap dispatches handler, real drag scrolls list.
# Spec: FR-RUNTIME-008 (AC-RUNTIME-023).
#
# Interface under test (plan verbatim):
#   runTouchSmoke(evidencePath: string): int — writes `touch-smoke-result.json`
#   with schema {tapDispatched, dragScrolled, engineHosted}; 0 iff both true,
#   1 otherwise.
#
# Hermetic strategy: fixtures are tmp-dir touch-smoke-result.json files; the suite
# script is driven via DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH (RULE-RUNTIME-005:
# override retargets the path only, never the verdict). A failing stub `adb` on
# PATH proves no device is contacted: valid evidence must still exit 0.
# ASSUMPTION: touch_smoke.sh honors DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH as a
# directory holding touch-smoke-result.json, exits 0 iff tapDispatched,
# dragScrolled, and engineHosted are all JSON boolean true (engineHosted true
# required per the T7 Acceptance line), 1 on any missing/invalid/false evidence,
# and supports --help.
#
# Usage: bash device-lab/touch-smoke/tests/touch_smoke_dryrun.sh
# Exit 0 = all contract assertions pass; nonzero = failure (RED before T7 ships).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUITE_SH="$SCRIPT_DIR/../touch_smoke.sh"
PRODUCER="$SCRIPT_DIR/../../app/src/main/java/com/muse/devicelab/TouchSmokeProducer.kt"
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); echo "PASS: $1"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL: $1${2:+ — $2}"; }

# --- 0. preconditions (RED when T7 not yet implemented) ---
RED=0
if [[ ! -f "$SUITE_SH" ]]; then
  echo "RED (expected): $SUITE_SH missing — touch-smoke suite script not shipped yet."
  RED=1
fi
if [[ ! -f "$PRODUCER" ]]; then
  echo "RED (expected): $PRODUCER missing — TouchSmokeProducer not shipped yet."
  RED=1
fi
if [[ "$RED" -ne 0 ]]; then exit 1; fi

# --- failing stub adb: suite must validate evidence files, never touch a device ---
STUBBIN="$(mktemp -d)"
cat > "$STUBBIN/adb" <<'EOF'
#!/usr/bin/env bash
echo "stub-adb: must not be called for hermetic evidence validation" >&2
exit 1
EOF
chmod +x "$STUBBIN/adb"
trap 'rm -rf "$STUBBIN"' EXIT

# --- write_fixture <dir> <body>: writes <dir>/touch-smoke-result.json ---
write_fixture() {
  mkdir -p "$1"
  printf '%s\n' "$2" > "$1/touch-smoke-result.json"
}

GOOD='{"tapDispatched":true,"dragScrolled":true,"engineHosted":true}'

# run_case <name> <json-body|__MISSING__> <want-exit>
run_case() {
  local name="$1" body="$2" want_exit="$3"
  local tmp; tmp="$(mktemp -d)"
  if [[ "$body" != "__MISSING__" ]]; then
    write_fixture "$tmp" "$body"
  fi
  local out code
  out="$(PATH="$STUBBIN:$PATH" DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH="$tmp" bash "$SUITE_SH" 2>&1)"
  code=$?
  if [[ "$code" -ne "$want_exit" ]]; then
    bad "$name" "exit=$code want=$want_exit out=[$out]"
  else
    ok "$name (exit=$code)"
  fi
  rm -rf "$tmp"
}

# --- 1. --help prints usage (no device needed) ---
if PATH="$STUBBIN:$PATH" bash "$SUITE_SH" --help 2>&1 | grep -qi "usage"; then
  ok "--help prints usage"
else
  bad "--help prints usage"
fi

# --- 2. all-true evidence exits 0 (with failing adb: verdict is file-based) ---
run_case "all_true_exits_0" "$GOOD" 0

# --- 3. each single false flag exits nonzero ---
run_case "tapDispatched_false_fails" \
  '{"tapDispatched":false,"dragScrolled":true,"engineHosted":true}' 1
run_case "dragScrolled_false_fails" \
  '{"tapDispatched":true,"dragScrolled":false,"engineHosted":true}' 1

# --- 4. engineHosted must be exactly true (standalone-view evidence never passes) ---
run_case "engineHosted_false_fails" \
  '{"tapDispatched":true,"dragScrolled":true,"engineHosted":false}' 1
run_case "engineHosted_missing_fails" \
  '{"tapDispatched":true,"dragScrolled":true}' 1

# --- 5. missing/invalid evidence fails closed ---
run_case "missing_evidence_dir_entry_fails" "__MISSING__" 1
run_case "invalid_json_fails" '{not json' 1
run_case "empty_object_fails" '{}' 1
run_case "missing_key_fails" \
  '{"tapDispatched":true,"engineHosted":true}' 1

# --- 6. wrong JSON types fail (string "true" is not boolean true) ---
run_case "string_true_is_not_true" \
  '{"tapDispatched":"true","dragScrolled":true,"engineHosted":true}' 1
run_case "numeric_one_is_not_true" \
  '{"tapDispatched":1,"dragScrolled":true,"engineHosted":true}' 1
run_case "null_flag_fails" \
  '{"tapDispatched":null,"dragScrolled":true,"engineHosted":true}' 1

# --- 7. RULE-RUNTIME-005: override retargets path only, never the verdict ---
BAD='{"tapDispatched":true,"dragScrolled":false,"engineHosted":true}'
A="$(mktemp -d)"; B="$(mktemp -d)"
write_fixture "$A" "$BAD"; write_fixture "$B" "$BAD"
CA=0; CB=0
PATH="$STUBBIN:$PATH" DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH="$A" bash "$SUITE_SH" >/dev/null 2>&1; CA=$?
PATH="$STUBBIN:$PATH" DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH="$B" bash "$SUITE_SH" >/dev/null 2>&1; CB=$?
if [[ "$CA" -ne 0 && "$CB" -ne 0 ]]; then ok "override path does not flip invalid verdict (exit=$CA,$CB)";
else bad "override path does not flip invalid verdict" "exit=$CA,$CB"; fi
rm -rf "$A" "$B"
C="$(mktemp -d)"; write_fixture "$C" "$GOOD"
PATH="$STUBBIN:$PATH" DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH="$C" bash "$SUITE_SH" >/dev/null 2>&1; CC=$?
if [[ "$CC" -eq 0 ]]; then ok "override path carries valid verdict (exit=0)";
else bad "override path carries valid verdict" "exit=$CC"; fi
rm -rf "$C"

# --- 8. static: suite pulls from the app-specific external files dir, no /sdcard/ root ---
if grep -qiE "getExternalFilesDir|EXTERNAL_FILES|external files dir|/data/data|/sdcard/Android/data" "$SUITE_SH"; then
  ok "suite pulls from app-specific external files dir"
else
  bad "suite pulls from app-specific external files dir"
fi
if grep -q "/sdcard/" "$SUITE_SH" 2>/dev/null; then
  bad "no /sdcard/ root evidence path" "found /sdcard/ reference"
else
  ok "no /sdcard/ root evidence path"
fi

# --- 9. static: Kotlin producer contract ---
if grep -q "fun runTouchSmoke" "$PRODUCER" && grep -q "evidencePath" "$PRODUCER"; then
  ok "producer exposes runTouchSmoke(evidencePath)"
else
  bad "producer exposes runTouchSmoke(evidencePath)"
fi
if grep -q "touch-smoke-result.json" "$PRODUCER"; then
  ok "producer writes touch-smoke-result.json"
else
  bad "producer writes touch-smoke-result.json"
fi
for key in tapDispatched dragScrolled engineHosted; do
  if grep -q "$key" "$PRODUCER"; then ok "producer records $key";
  else bad "producer records $key"; fi
done
if grep -qiE "MotionEvent|dispatchTouch|onTouch|inject|tap|drag|scroll" "$PRODUCER"; then
  ok "producer injects real tap and drag into the hosted engine tree"
else
  bad "producer injects real tap and drag into the hosted engine tree"
fi
if grep -qiE "getExternalFilesDir|EXTERNAL_FILES" "$PRODUCER"; then
  ok "producer resolves app-specific external files dir"
else
  bad "producer resolves app-specific external files dir"
fi
if grep -q "/sdcard/" "$PRODUCER" 2>/dev/null; then
  bad "producer has no /sdcard/ root path" "found /sdcard/ reference"
else
  ok "producer has no /sdcard/ root path"
fi

echo "---"
echo "pass=$PASS fail=$FAIL"
[[ "$FAIL" -eq 0 ]]
