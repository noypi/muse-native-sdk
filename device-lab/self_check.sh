#!/usr/bin/env bash
# Hermetic self-check for the batch-2 device-lab harness (T5, T6).
# Verifies WITHOUT any device/emulator:
#   1. every harness script parses (bash -n),
#   2. both suites fail CLOSED with a missing-harness message when the SDK/adb
#      cannot be found (forced via DEVICE_LAB_ADB + empty ANDROID_HOME),
#   3. the dispatcher rejects unknown suites non-zero.
# Exit 0 iff all self-checks hold. This is the only device-lab check that is
# expected GREEN without an emulator; ime-smoke/ime-inset themselves must be RED here.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
pass=0; fail=0
ok()   { echo "ok: $1"; pass=$((pass+1)); }
bad()  { echo "NOT-OK: $1" >&2; fail=$((fail+1)); }

for s in "$HERE/run.sh" "$HERE/ime-smoke/ime_smoke.sh" "$HERE/ime-inset/ime_inset.sh" "$HERE/self_check.sh"; do
  if bash -n "$s"; then ok "syntax $s"; else bad "syntax $s"; fi
done

# Forced missing-harness run: bogus adb + empty SDK home => must exit 2 naming the harness.
for suite in ime-smoke ime-inset; do
  out="$(ANDROID_HOME=/nonexistent-sdk-root ANDROID_SDK_ROOT=/nonexistent-sdk-root \
         DEVICE_LAB_ADB=/nonexistent-adb-xyz "$HERE/run.sh" "$suite" 2>&1)"
  rc=$?
  if [[ $rc -ne 0 ]] && echo "$out" | grep -q "missing Android harness"; then
    ok "$suite fails closed without SDK/adb (rc=$rc)"
  else
    bad "$suite missing-harness guard (rc=$rc, out='$out')"
  fi
done

# No device attached (real adb, but adb devices is empty) => must still be non-zero
# and name the missing emulator/device. Skipped if a device happens to be attached.
if adb devices 2>/dev/null | awk 'NR>1 && $2 ~ /^(device|emulator)$/ {found=1} END {exit !found}'; then
  echo "skip: device attached — no-device guard not exercised (suites should run for real)"
else
  for suite in ime-smoke ime-inset; do
    out="$("$HERE/run.sh" "$suite" 2>&1)"; rc=$?
    if [[ $rc -ne 0 ]] && echo "$out" | grep -qi "no emulator/device attached"; then
      ok "$suite names missing emulator/device (rc=$rc)"
    else
      bad "$suite no-device guard (rc=$rc, out='$out')"
    fi
  done
fi

# Dispatcher rejects unknown suites.
if "$HERE/run.sh" bogus-suite >/dev/null 2>&1; then bad "dispatcher accepts unknown suite"; else ok "dispatcher rejects unknown suite"; fi

echo "self-check: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
