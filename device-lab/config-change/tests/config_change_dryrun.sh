#!/usr/bin/env bash
# Dry-run harness for T8 config-change producer (no device, no network).
# Item: T8 — config-change producer: rotation + background/foreground with tree preserved.
# Spec: FR-RUNTIME-008 (AC-RUNTIME-024).
#
# Interface under test (plan verbatim):
#   runConfigChange(evidencePath: string): int — writes
#   `config-change-result.json` with schema {rotationPreserved, bgFgPreserved,
#   nodeIdsStable, engineHosted}; 0 iff all true, 1 otherwise.
#
# Hermetic strategy: fixtures are tmp-dir config-change-result.json files; the suite
# script is driven via DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH (RULE-RUNTIME-005:
# override retargets the path only, never the verdict). A failing stub `adb` on
# PATH proves no device is contacted: valid evidence must still exit 0.
# ASSUMPTION: config_change.sh honors DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH as a
# directory holding config-change-result.json, exits 0 iff all four flags are JSON
# boolean true, 1 on any missing/invalid/false evidence, and supports --help.
#
# Usage: bash device-lab/config-change/tests/config_change_dryrun.sh
# Exit 0 = all contract assertions pass; nonzero = failure (RED before T8 ships).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUITE_SH="$SCRIPT_DIR/../config_change.sh"
PRODUCER="$SCRIPT_DIR/../../app/src/main/java/com/muse/devicelab/ConfigChangeProducer.kt"
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); echo "PASS: $1"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL: $1${2:+ — $2}"; }

# --- 0. preconditions (RED when T8 not yet implemented) ---
RED=0
if [[ ! -f "$SUITE_SH" ]]; then
  echo "RED (expected): $SUITE_SH missing — config-change suite script not shipped yet."
  RED=1
fi
if [[ ! -f "$PRODUCER" ]]; then
  echo "RED (expected): $PRODUCER missing — ConfigChangeProducer not shipped yet."
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

# --- write_fixture <dir> <body>: writes <dir>/config-change-result.json ---
write_fixture() {
  mkdir -p "$1"
  printf '%s\n' "$2" > "$1/config-change-result.json"
}

GOOD='{"rotationPreserved":true,"bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":true}'

# run_case <name> <json-body|__MISSING__> <want-exit>
run_case() {
  local name="$1" body="$2" want_exit="$3"
  local tmp; tmp="$(mktemp -d)"
  if [[ "$body" != "__MISSING__" ]]; then
    write_fixture "$tmp" "$body"
  fi
  local out code
  out="$(PATH="$STUBBIN:$PATH" DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH="$tmp" bash "$SUITE_SH" 2>&1)"
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
run_case "rotationPreserved_false_fails" \
  '{"rotationPreserved":false,"bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":true}' 1
run_case "bgFgPreserved_false_fails" \
  '{"rotationPreserved":true,"bgFgPreserved":false,"nodeIdsStable":true,"engineHosted":true}' 1
run_case "nodeIdsStable_false_fails" \
  '{"rotationPreserved":true,"bgFgPreserved":true,"nodeIdsStable":false,"engineHosted":true}' 1

# --- 4. engineHosted must be exactly true (standalone-view evidence never passes) ---
run_case "engineHosted_false_fails" \
  '{"rotationPreserved":true,"bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":false}' 1
run_case "engineHosted_missing_fails" \
  '{"rotationPreserved":true,"bgFgPreserved":true,"nodeIdsStable":true}' 1

# --- 5. missing/invalid evidence fails closed ---
run_case "missing_evidence_dir_entry_fails" "__MISSING__" 1
run_case "invalid_json_fails" '{not json' 1
run_case "empty_object_fails" '{}' 1
run_case "missing_key_fails" \
  '{"rotationPreserved":true,"bgFgPreserved":true,"engineHosted":true}' 1

# --- 6. wrong JSON types fail (string "true" is not boolean true) ---
run_case "string_true_is_not_true" \
  '{"rotationPreserved":"true","bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":true}' 1
run_case "numeric_one_is_not_true" \
  '{"rotationPreserved":1,"bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":true}' 1
run_case "null_flag_fails" \
  '{"rotationPreserved":null,"bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":true}' 1

# --- 7. RULE-RUNTIME-005: override retargets path only, never the verdict ---
BAD='{"rotationPreserved":true,"bgFgPreserved":false,"nodeIdsStable":true,"engineHosted":true}'
A="$(mktemp -d)"; B="$(mktemp -d)"
write_fixture "$A" "$BAD"; write_fixture "$B" "$BAD"
CA=0; CB=0
PATH="$STUBBIN:$PATH" DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH="$A" bash "$SUITE_SH" >/dev/null 2>&1; CA=$?
PATH="$STUBBIN:$PATH" DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH="$B" bash "$SUITE_SH" >/dev/null 2>&1; CB=$?
if [[ "$CA" -ne 0 && "$CB" -ne 0 ]]; then ok "override path does not flip invalid verdict (exit=$CA,$CB)";
else bad "override path does not flip invalid verdict" "exit=$CA,$CB"; fi
rm -rf "$A" "$B"
C="$(mktemp -d)"; write_fixture "$C" "$GOOD"
PATH="$STUBBIN:$PATH" DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH="$C" bash "$SUITE_SH" >/dev/null 2>&1; CC=$?
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
if grep -q "fun runConfigChange" "$PRODUCER" && grep -q "evidencePath" "$PRODUCER"; then
  ok "producer exposes runConfigChange(evidencePath)"
else
  bad "producer exposes runConfigChange(evidencePath)"
fi
if grep -q "config-change-result.json" "$PRODUCER"; then
  ok "producer writes config-change-result.json"
else
  bad "producer writes config-change-result.json"
fi
for key in rotationPreserved bgFgPreserved nodeIdsStable engineHosted; do
  if grep -q "$key" "$PRODUCER"; then ok "producer records $key";
  else bad "producer records $key"; fi
done
if grep -qiE "rotation|orientation|background|foreground|onPause|onResume|config.*change|recreat" "$PRODUCER"; then
  ok "producer performs rotation plus background/foreground transitions"
else
  bad "producer performs rotation plus background/foreground transitions"
fi
if grep -qiE "nodeId|tree_nodes|stable" "$PRODUCER"; then
  ok "producer asserts stable tree node ids"
else
  bad "producer asserts stable tree node ids"
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
