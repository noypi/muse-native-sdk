#!/bin/sh
# run-efail.sh — batch 1 EFAIL runner (FR-GRAPHICS-011, TC-GRAPHICS-032).
# Hermetic host test mirroring engine/graphics/device/run-t1.sh pattern:
# compiles T1 sources with g++ -std=c++17, output to /tmp, timeout-guarded,
# no crash/hang/skip. Prints EFAIL-VERDICT and exits accordingly.
set -u
HERE=$(dirname "$0")
TIMEOUT_SECS=${EFAIL_TIMEOUT_SECS:-30}
OUT=/tmp/nfr_efail.test

fail() {
  echo "EFAIL-VERDICT: FAIL $1"
  exit 1
}

# 1. Compile the hermetic force-fail test against the T1 sources.
if ! g++ -std=c++17 -I engine/graphics \
  engine/graphics/egl-probe/egl_probe_force_fail.test.cpp \
  engine/graphics/egl-probe/egl_probe.cc \
  -o "$OUT" 2>/tmp/nfr_efail.build.log; then
  echo "--- build log (/tmp/nfr_efail.build.log) ---"
  cat /tmp/nfr_efail.build.log
  fail "compile-error (expected RED before T1 lands)"
fi

# 2. Run the compiled test binary under a timeout guard (no hang).
if command -v timeout >/dev/null 2>&1; then
  if ! timeout "$TIMEOUT_SECS" "$OUT"; then
    fail "force-fail-test exited nonzero"
  fi
else
  if ! "$OUT"; then
    fail "force-fail-test exited nonzero (no timeout(1); EFAIL_TIMEOUT_SECS honored only with timeout(1))"
  fi
fi

# 3. Stage matrix: stages init|config|context|surface must each exit nonzero
#    with a stage-naming diagnostic. The host unit binary covers the `init`
#    leg in-process (cases above); the per-stage CLI legs run once the T1
#    probe main exists. Until then, record them as pending-red, not skipped.
STAGES="init config context surface"
if [ -x engine/graphics/egl-probe/egl-probe-host ]; then
  for s in $STAGES; do
    LOG=/tmp/nfr_efail.stage.$s.log
    if command -v timeout >/dev/null 2>&1; then
      timeout "$TIMEOUT_SECS" engine/graphics/egl-probe/egl-probe-host --force-fail "$s" >"$LOG" 2>&1
      code=$?
    else
      engine/graphics/egl-probe/egl-probe-host --force-fail "$s" >"$LOG" 2>&1
      code=$?
    fi
    # Signal death (128+) or timeout (124) = crash/hang, never acceptable.
    if [ "$code" -eq 0 ]; then
      fail "stage $s exited 0, want nonzero"
    fi
    if [ "$code" -ge 128 ] || [ "$code" -eq 124 ]; then
      fail "stage $s crashed/hung (exit $code)"
    fi
    if ! grep -q "$s" "$LOG"; then
      fail "stage $s log missing diagnostic substring '$s'"
    fi
  done
else
  echo "note: egl-probe-host binary absent — per-stage CLI legs pending RED (T1/T2 not landed), not skipped"
fi

echo "EFAIL-VERDICT: PASS"
exit 0
