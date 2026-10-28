#!/usr/bin/env bash
# Dry-run harness for T5 device-lab procedure (stubbed adb, no device, no network).
# Item: T5 — check_applinks.sh contract verification.
# Spec: FR-NAV-006 AC-NAV-016 (TC-NAV-016 stays external/device-lab; no live-device pass claimed).
#
# Usage: bash device-lab/applinks/tests/check_applinks_dryrun.sh
# Exit 0 = all contract assertions pass; nonzero = failure (RED before T5 ships check_applinks.sh).
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}" )" && pwd)"
CHECK="$SCRIPT_DIR/../check_applinks.sh"
PASS=0
FAIL=0

ok()   { PASS=$((PASS+1)); echo "PASS: $1"; }
bad()  { FAIL=$((FAIL+1)); echo "FAIL: $1${2:+ — $2}"; }

# --- make_stub_adb <dir> <mode>: writes a canned `adb` into <dir> ---
# modes: verified | not_verified | offline (adb present but device error)
make_stub_adb() {
  local dir="$1" mode="$2"
  cat > "$dir/adb" <<EOF
#!/usr/bin/env bash
# stub adb for T5 dry-run; mode=$mode
MODE="$mode"
if [[ "\$*" == *"pm get-app-links com.example.shop"* ]]; then
  if [[ "\$MODE" == "verified" ]]; then
    echo "com.example.shop: verified link handles for shop.example.com"
    exit 0
  elif [[ "\$MODE" == "not_verified" ]]; then
    echo "com.example.shop: unverified link handles for shop.example.com"
    exit 0
  else
    echo "error: device offline" >&2
    exit 1
  fi
fi
if [[ "\$*" == *"pm verify-app-links --re-verify com.example.shop"* ]]; then
  exit 0
fi
echo "error: device offline" >&2
exit 1
EOF
  chmod +x "$dir/adb"
}

run_case() {
  local name="$1" mode="$2" want_exit="$3" want_status="$4"
  local tmp; tmp="$(mktemp -d)"
  make_stub_adb "$tmp" "$mode"
  local out exit_code
  out="$(PATH="$tmp:$PATH" bash "$CHECK" com.example.shop shop.example.com 2>&1)"
  exit_code=$?
  if [[ "$exit_code" -ne "$want_exit" ]]; then
    bad "$name" "exit=$exit_code want=$want_exit out=[$out]"
  elif [[ "$out" != "package=com.example.shop host=shop.example.com status=$want_status detail="* ]]; then
    bad "$name" "stdout contract mismatch: [$out]"
  else
    ok "$name (exit=$exit_code status=$want_status)"
  fi
  rm -rf "$tmp"
}

# 0. precondition: procedure script exists (RED when T5 not yet implemented)
if [[ ! -f "$CHECK" ]]; then
  echo "RED (expected): $CHECK missing — procedure not shipped yet."
  exit 1
fi

# 1. --help prints usage
if bash "$CHECK" --help 2>&1 | grep -qi "usage"; then ok "--help prints usage"; else bad "--help prints usage"; fi

# 2. VERIFIED case
run_case "verified_host_returns_VERIFIED" verified 0 verified

# 3. NOT_VERIFIED case
run_case "unverified_host_returns_NOT_VERIFIED" not_verified 1 not_verified

# 4. usage error (missing args) -> exit 2
if bash "$CHECK" 2>/dev/null; then bad "usage_error_exits_2" "exit=0"; else
  code=$?
  if [[ "$code" -eq 2 ]]; then ok "usage_error_exits_2"; else bad "usage_error_exits_2" "exit=$code want=2"; fi
fi

# 5. DEVICE_ERROR when adb missing from PATH -> exit 2
tmp="$(mktemp -d)"
out="$(PATH="$tmp:/usr/bin:/bin" bash "$CHECK" com.example.shop shop.example.com 2>&1)"
code=$?
rmdir "$tmp"
if [[ "$code" -eq 2 && "$out" == *"status=error"* ]]; then ok "missing_adb_returns_DEVICE_ERROR"; else bad "missing_adb_returns_DEVICE_ERROR" "exit=$code out=[$out]"; fi

# 6. DEVICE_ERROR when device offline -> exit 2
run_case "device_offline_returns_DEVICE_ERROR" offline 2 error

echo "---"
echo "pass=$PASS fail=$FAIL"
[[ "$FAIL" -eq 0 ]]
