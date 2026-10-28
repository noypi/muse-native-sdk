#!/usr/bin/env bash
# Acceptance: suite-script fail-closed verdicts + evidence-path override semantics
# (T4 runner part, T5-T8 hermetic part of FR-RUNTIME-008, AC-021..024).
# Hermetic: file fixtures in tmp dirs only, no device/adb needed.
# On-device success paths are EXTERNAL and not asserted here.
set -u
PASS=0; FAIL=0
ok() { PASS=$((PASS+1)); echo "ok - $1"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL - $1: $2"; }
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"

# T4: unknown suite exits 2, --help lists four suites
out="$("$ROOT/device-lab/run.sh" bogus-suite 2>&1)"; code=$?
if [ "$code" -eq 2 ]; then ok "T4 unknown suite exits 2"; else bad "T4 unknown suite exits 2" "exit=$code"; fi
if "$ROOT/device-lab/run.sh" --help 2>&1 | grep -q "ime-smoke" \
  && "$ROOT/device-lab/run.sh" --help 2>&1 | grep -q "ime-inset" \
  && "$ROOT/device-lab/run.sh" --help 2>&1 | grep -q "touch-smoke" \
  && "$ROOT/device-lab/run.sh" --help 2>&1 | grep -q "config-change"; then
  ok "T4 --help lists four suites"
else bad "T4 --help lists four suites" "missing suite name"; fi

# T5: engineHosted:false fails closed; valid passes
D="$(mktemp -d)"; trap 'rm -rf "$D"' EXIT
echo '{"composition":true,"caret":true,"selection":true,"clipboardCopyPaste":true,"engineHosted":false}' > "$D/ime-smoke-result.json"
if DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH="$D" bash "$ROOT/device-lab/ime-smoke/ime_smoke.sh" >/dev/null 2>&1; then
  bad "T5 engineHosted:false fails closed" "exited 0"
else ok "T5 engineHosted:false fails closed"; fi
echo '{"composition":true,"caret":true,"selection":true,"clipboardCopyPaste":true,"engineHosted":true}' > "$D/ime-smoke-result.json"
if DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH="$D" bash "$ROOT/device-lab/ime-smoke/ime_smoke.sh" >/dev/null 2>&1; then
  ok "T5 all-true passes"
else bad "T5 all-true passes" "exited non-zero"; fi
# override retargets path only: same invalid content fails under a second dir
D2="$(mktemp -d)"
echo '{"composition":true,"caret":true,"selection":true,"clipboardCopyPaste":"true","engineHosted":true}' > "$D2/ime-smoke-result.json"
if DEVICE_LAB_IME_SMOKE_EVIDENCE_PATH="$D2" bash "$ROOT/device-lab/ime-smoke/ime_smoke.sh" >/dev/null 2>&1; then
  bad "T5 string-true rejected under override dir" "exited 0"
else ok "T5 string-true rejected under override dir"; fi
rm -rf "$D2"

# T6: late frame fails closed; valid show+hide passes
E="$(mktemp -d)"
printf '%s\n' '{"frame":0,"announcedHeight":300,"appliedHeight":300,"latencyFrames":5}' > "$E/ime-inset-frames.jsonl"
if DEVICE_LAB_IME_INSET_EVIDENCE_PATH="$E" bash "$ROOT/device-lab/ime-inset/ime_inset.sh" >/dev/null 2>&1; then
  bad "T6 latencyFrames 5 fails closed" "exited 0"
else ok "T6 latencyFrames 5 fails closed"; fi
printf '%s\n' '{"frame":0,"announcedHeight":400,"appliedHeight":400,"latencyFrames":0}' '{"frame":1,"announcedHeight":0,"appliedHeight":0,"latencyFrames":1}' > "$E/ime-inset-frames.jsonl"
if DEVICE_LAB_IME_INSET_EVIDENCE_PATH="$E" bash "$ROOT/device-lab/ime-inset/ime_inset.sh" >/dev/null 2>&1; then
  ok "T6 valid show+hide passes"
else bad "T6 valid show+hide passes" "exited non-zero"; fi
rm -rf "$E"

# T7: missing evidence fails closed; valid passes
M="$(mktemp -d)"
if DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH="$M" bash "$ROOT/device-lab/touch-smoke/touch_smoke.sh" >/dev/null 2>&1; then
  bad "T7 missing evidence fails closed" "exited 0"
else ok "T7 missing evidence fails closed"; fi
echo '{"tapDispatched":true,"dragScrolled":true,"engineHosted":true}' > "$M/touch-smoke-result.json"
if DEVICE_LAB_TOUCH_SMOKE_EVIDENCE_PATH="$M" bash "$ROOT/device-lab/touch-smoke/touch_smoke.sh" >/dev/null 2>&1; then
  ok "T7 valid passes"
else bad "T7 valid passes" "exited non-zero"; fi
rm -rf "$M"

# T8: mistyped flag fails closed; valid passes
C="$(mktemp -d)"
echo '{"rotationPreserved":"true","bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":true}' > "$C/config-change-result.json"
if DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH="$C" bash "$ROOT/device-lab/config-change/config_change.sh" >/dev/null 2>&1; then
  bad "T8 string-true rejected" "exited 0"
else ok "T8 string-true rejected"; fi
echo '{"rotationPreserved":true,"bgFgPreserved":true,"nodeIdsStable":true,"engineHosted":true}' > "$C/config-change-result.json"
if DEVICE_LAB_CONFIG_CHANGE_EVIDENCE_PATH="$C" bash "$ROOT/device-lab/config-change/config_change.sh" >/dev/null 2>&1; then
  ok "T8 valid passes"
else bad "T8 valid passes" "exited non-zero"; fi
rm -rf "$C"

echo "---"; echo "pass=$PASS fail=$FAIL"
[ "$FAIL" -eq 0 ]
