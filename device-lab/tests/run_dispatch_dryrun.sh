#!/usr/bin/env bash
# Dry-run harness for T4 device-lab scaffold + run.sh dispatch (no device, no network).
# Item: T4 — Scaffold engine-hosted device-lab app + run.sh dispatch.
# Spec: FR-RUNTIME-008 (AC-RUNTIME-021). RULE-RUNTIME-005: evidence always resolves
# to the app-specific external files dir; DEVICE_LAB_*_EVIDENCE_PATH overrides
# retarget the path only, never the verdict.
#
# Usage: bash device-lab/tests/run_dispatch_dryrun.sh
# Exit 0 = all contract assertions pass; nonzero = failure (RED before T4 ships run.sh).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_SH="$SCRIPT_DIR/../run.sh"
APP_DIR="$SCRIPT_DIR/../app"
HOST_ACTIVITY="$APP_DIR/src/main/java/com/muse/devicelab/HostActivity.kt"
ENGINE_HOST="$APP_DIR/src/main/java/com/muse/devicelab/EngineHost.kt"
BUILD_GRADLE="$APP_DIR/build.gradle"
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); echo "PASS: $1"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL: $1${2:+ — $2}"; }

# --- 0. preconditions: production files exist (RED when T4 not yet implemented) ---
RED=0
for f in "$RUN_SH" "$HOST_ACTIVITY" "$ENGINE_HOST" "$BUILD_GRADLE"; do
  if [[ ! -f "$f" ]]; then
    echo "RED (expected): $f missing — T4 scaffold not shipped yet."
    RED=1
  fi
done
if [[ "$RED" -ne 0 ]]; then exit 1; fi

# --- make_failing_adb <dir>: stub adb that fails if ever invoked ---
# Dispatch-level checks (--help, unknown suite, no args) must not need a device.
make_failing_adb() {
  local dir="$1"
  cat > "$dir/adb" <<'EOF'
#!/usr/bin/env bash
echo "stub-adb: must not be called for dispatch-level checks" >&2
exit 1
EOF
  chmod +x "$dir/adb"
}

STUBBIN="$(mktemp -d)"
make_failing_adb "$STUBBIN"
trap 'rm -rf "$STUBBIN"' EXIT

# --- 1. --help lists all four suites ---
HELP_OUT="$(PATH="$STUBBIN:$PATH" bash "$RUN_SH" --help 2>&1)"
HELP_CODE=$?
if [[ "$HELP_CODE" -eq 0 ]]; then ok "--help exits 0"; else bad "--help exits 0" "exit=$HELP_CODE out=[$HELP_OUT]"; fi
for suite in ime-smoke ime-inset touch-smoke config-change; do
  if grep -q -- "$suite" <<<"$HELP_OUT"; then ok "--help lists $suite";
  else bad "--help lists $suite" "out=[$HELP_OUT]"; fi
done

# --- 2. unknown suite exits 2 (with failing adb on PATH: no device needed) ---
OUT="$(PATH="$STUBBIN:$PATH" bash "$RUN_SH" bogus-suite 2>&1)"; CODE=$?
if [[ "$CODE" -eq 2 ]]; then ok "unknown suite exits 2";
else bad "unknown suite exits 2" "exit=$CODE out=[$OUT]"; fi

# --- 3. no args exits 2 (usage error) ---
OUT="$(PATH="$STUBBIN:$PATH" bash "$RUN_SH" 2>&1)"; CODE=$?
if [[ "$CODE" -eq 2 ]]; then ok "no args exits 2";
else bad "no args exits 2" "exit=$CODE out=[$OUT]"; fi

# --- 4. empty-string suite exits 2 ---
OUT="$(PATH="$STUBBIN:$PATH" bash "$RUN_SH" "" 2>&1)"; CODE=$?
if [[ "$CODE" -eq 2 ]]; then ok "empty suite exits 2";
else bad "empty suite exits 2" "exit=$CODE out=[$OUT]"; fi

# --- 5. suite matching is byte-exact: wrong case exits 2 ---
OUT="$(PATH="$STUBBIN:$PATH" bash "$RUN_SH" IME-SMOKE 2>&1)"; CODE=$?
if [[ "$CODE" -eq 2 ]]; then ok "wrong-case suite exits 2";
else bad "wrong-case suite exits 2" "exit=$CODE out=[$OUT]"; fi

# --- 6. extra positional arg is rejected (exactly one suite) ---
OUT="$(PATH="$STUBBIN:$PATH" bash "$RUN_SH" ime-smoke extra-arg 2>&1)"; CODE=$?
if [[ "$CODE" -eq 2 ]]; then ok "extra arg exits 2";
else bad "extra arg exits 2" "exit=$CODE out=[$OUT]"; fi

# --- 7. static: run.sh dispatches all four suites ---
for suite in ime-smoke ime-inset touch-smoke config-change; do
  if grep -q -- "$suite" "$RUN_SH"; then ok "run.sh dispatches $suite";
  else bad "run.sh dispatches $suite"; fi
done

# --- 8. static: unknown-suite path exits 2 (literal in script) ---
if grep -q "exit 2" "$RUN_SH"; then ok "run.sh carries exit-2 usage path";
else bad "run.sh carries exit-2 usage path"; fi

# --- 9. static: evidence defaults to the app-specific external files dir ---
if grep -qiE "getExternalFilesDir|EXTERNAL_FILES|external files dir" "$RUN_SH" \
  || grep -qiE "getExternalFilesDir|EXTERNAL_FILES|external files dir" "$HOST_ACTIVITY" \
  || grep -qiE "getExternalFilesDir|EXTERNAL_FILES|external files dir" "$ENGINE_HOST"; then
  ok "evidence defaults to app-specific external files dir"
else
  bad "evidence defaults to app-specific external files dir"
fi

# --- 10. static: DEVICE_LAB_*_EVIDENCE_PATH override is honored ---
if grep -q "DEVICE_LAB_" "$RUN_SH"; then ok "run.sh honors DEVICE_LAB_*_EVIDENCE_PATH override";
else bad "run.sh honors DEVICE_LAB_*_EVIDENCE_PATH override"; fi

# --- 11. static: override retargets path only — verdict comes from evidence content ---
# The override variable must never appear in a verdict comparison; validation must
# read an evidence file/JSON. (RULE-RUNTIME-005)
if grep -q "DEVICE_LAB_" "$RUN_SH" && grep -qiE "json|evidence.*(valid|parse|result|check)|verify" "$RUN_SH"; then
  ok "override retargets path only, verdict validates evidence content"
else
  bad "override retargets path only, verdict validates evidence content"
fi

# --- 12. static: no committed APK, no /sdcard/ root evidence path (RULE-RUNTIME-005) ---
if grep -q "/sdcard/" "$RUN_SH" "$HOST_ACTIVITY" "$ENGINE_HOST" 2>/dev/null; then
  bad "no /sdcard/ root evidence path" "found /sdcard/ reference"
else
  ok "no /sdcard/ root evidence path"
fi
if find "$SCRIPT_DIR/.." -maxdepth 2 -name "*.apk" | grep -q .; then
  bad "no committed APK" "apk file present"
else
  ok "no committed APK"
fi

# --- 13. static: scaffold hosts the engine through the JNI bridge (not a standalone widget) ---
if grep -qiE "EngineHost|System\.loadLibrary|jni|Runtime_create|Native" "$HOST_ACTIVITY"; then
  ok "HostActivity hosts engine via JNI bridge"
else
  bad "HostActivity hosts engine via JNI bridge"
fi
if grep -qiE "Runtime_create|Runtime_destroy|jni|loadLibrary|external fun" "$ENGINE_HOST"; then
  ok "EngineHost binds the T2 JNI bridge"
else
  bad "EngineHost binds the T2 JNI bridge"
fi

# --- 14. static: hermetic gate runs before any device suite ---
if grep -qiE "ctest|hermetic|gate" "$RUN_SH"; then ok "run.sh runs hermetic gate first";
else bad "run.sh runs hermetic gate first"; fi

echo "---"
echo "pass=$PASS fail=$FAIL"
[[ "$FAIL" -eq 0 ]]
