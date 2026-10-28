#!/usr/bin/env bash
# Dry-run harness for T6 ime-inset producer (no device, no network).
# Item: T6 — ime-inset producer: keyboard show/hide with per-frame inset sampling.
# Spec: FR-RUNTIME-008 (AC-RUNTIME-022).
#
# Interface under test (plan verbatim):
#   runImeInset(evidencePath: string): int — appends `ime-inset-frames.jsonl`,
#   one object per frame {frame, announcedHeight, appliedHeight, latencyFrames};
#   returns 0 iff show+hide cycle sampled with `latencyFrames <= 1` on every
#   frame, 1 otherwise.
#
# Hermetic strategy: fixtures are tmp-dir ime-inset-frames.jsonl files; the suite
# script is driven via DEVICE_LAB_IME_INSET_EVIDENCE_PATH (RULE-RUNTIME-005:
# override retargets the path only, never the verdict). A failing stub `adb` on
# PATH proves no device is contacted: valid evidence must still exit 0.
# ASSUMPTION: ime_inset.sh honors DEVICE_LAB_IME_INSET_EVIDENCE_PATH as a directory
# holding ime-inset-frames.jsonl, exits 0 iff the JSONL holds >=1 show frame
# (announcedHeight > 0) and >=1 hide frame (announcedHeight == 0) with every
# frame's latencyFrames an int in [0,1], 1 on any missing/invalid/late evidence,
# and supports --help.
#
# Usage: bash device-lab/ime-inset/tests/ime_inset_dryrun.sh
# Exit 0 = all contract assertions pass; nonzero = failure (RED before T6 ships).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SUITE_SH="$SCRIPT_DIR/../ime_inset.sh"
PRODUCER="$SCRIPT_DIR/../../app/src/main/java/com/muse/devicelab/ImeInsetProducer.kt"
PASS=0
FAIL=0

ok()  { PASS=$((PASS+1)); echo "PASS: $1"; }
bad() { FAIL=$((FAIL+1)); echo "FAIL: $1${2:+ — $2}"; }

# --- 0. preconditions (RED when T6 not yet implemented) ---
RED=0
if [[ ! -f "$SUITE_SH" ]]; then
  echo "RED (expected): $SUITE_SH missing — ime-inset suite script not shipped yet."
  RED=1
fi
if [[ ! -f "$PRODUCER" ]]; then
  echo "RED (expected): $PRODUCER missing — ImeInsetProducer not shipped yet."
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

# --- run_jsonl_case <name> <want-exit> [extra-env...] reads the JSONL body on stdin ---
run_jsonl_case() {
  local name="$1" want_exit="$2"
  local tmp; tmp="$(mktemp -d)"
  cat > "$tmp/ime-inset-frames.jsonl"
  local out code
  out="$(PATH="$STUBBIN:$PATH" DEVICE_LAB_IME_INSET_EVIDENCE_PATH="$tmp" bash "$SUITE_SH" 2>&1)"
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

# --- 2. valid show+hide cycle with latency <= 1 exits 0 (failing adb: file-based) ---
run_jsonl_case "show_hide_latency_ok_exits_0" 0 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":720,"latencyFrames":0}
{"frame":2,"announcedHeight":720,"appliedHeight":720,"latencyFrames":1}
{"frame":3,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF

# --- 3. any frame with latencyFrames > 1 exits nonzero ---
run_jsonl_case "latency_two_fails" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":0,"latencyFrames":2}
{"frame":2,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF

# --- 4. show without hide (and hide without show) exits nonzero ---
run_jsonl_case "show_without_hide_fails" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":720,"latencyFrames":0}
EOF
run_jsonl_case "hide_without_show_fails" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF

# --- 5. missing/empty/invalid evidence fails closed ---
MISSING_TMP="$(mktemp -d)"
if PATH="$STUBBIN:$PATH" DEVICE_LAB_IME_INSET_EVIDENCE_PATH="$MISSING_TMP" bash "$SUITE_SH" >/dev/null 2>&1; then
  bad "missing_evidence_fails" "exited 0 with no evidence file"
else
  ok "missing_evidence_fails (exit nonzero)"
fi
rm -rf "$MISSING_TMP"
run_jsonl_case "empty_jsonl_fails" 1 <<'EOF'
EOF
run_jsonl_case "invalid_json_line_fails" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{not json}
EOF
run_jsonl_case "missing_key_fails" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":720}
{"frame":2,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF

# --- 6. wrong JSON types fail (string "1" is not int 1; negative latency fails) ---
run_jsonl_case "string_latency_is_not_int" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":720,"latencyFrames":"1"}
{"frame":2,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF
run_jsonl_case "negative_latency_fails" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":720,"latencyFrames":-1}
{"frame":2,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF
run_jsonl_case "null_height_fails" 1 <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":null,"appliedHeight":720,"latencyFrames":0}
{"frame":2,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF

# --- 7. RULE-RUNTIME-005: override retargets path only, never the verdict ---
BAD_ONE='{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":0,"latencyFrames":3}
{"frame":2,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}'
A="$(mktemp -d)"; B="$(mktemp -d)"
printf '%s\n' "$BAD_ONE" > "$A/ime-inset-frames.jsonl"
printf '%s\n' "$BAD_ONE" > "$B/ime-inset-frames.jsonl"
CA=0; CB=0
PATH="$STUBBIN:$PATH" DEVICE_LAB_IME_INSET_EVIDENCE_PATH="$A" bash "$SUITE_SH" >/dev/null 2>&1; CA=$?
PATH="$STUBBIN:$PATH" DEVICE_LAB_IME_INSET_EVIDENCE_PATH="$B" bash "$SUITE_SH" >/dev/null 2>&1; CB=$?
if [[ "$CA" -ne 0 && "$CB" -ne 0 ]]; then ok "override path does not flip invalid verdict (exit=$CA,$CB)";
else bad "override path does not flip invalid verdict" "exit=$CA,$CB"; fi
rm -rf "$A" "$B"
C="$(mktemp -d)"
cat > "$C/ime-inset-frames.jsonl" <<'EOF'
{"frame":0,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
{"frame":1,"announcedHeight":720,"appliedHeight":720,"latencyFrames":1}
{"frame":2,"announcedHeight":0,"appliedHeight":0,"latencyFrames":0}
EOF
PATH="$STUBBIN:$PATH" DEVICE_LAB_IME_INSET_EVIDENCE_PATH="$C" bash "$SUITE_SH" >/dev/null 2>&1; CC=$?
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
if grep -q "fun runImeInset" "$PRODUCER" && grep -q "evidencePath" "$PRODUCER"; then
  ok "producer exposes runImeInset(evidencePath)"
else
  bad "producer exposes runImeInset(evidencePath)"
fi
if grep -q "ime-inset-frames.jsonl" "$PRODUCER"; then
  ok "producer appends ime-inset-frames.jsonl"
else
  bad "producer appends ime-inset-frames.jsonl"
fi
for key in announcedHeight appliedHeight latencyFrames; do
  if grep -q "$key" "$PRODUCER"; then ok "producer records $key";
  else bad "producer records $key"; fi
done
if grep -qiE "WindowInsets|ime\(\)|keyboard|show.*hide|hide.*show|inset" "$PRODUCER"; then
  ok "producer drives keyboard show/hide with inset sampling"
else
  bad "producer drives keyboard show/hide with inset sampling"
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
