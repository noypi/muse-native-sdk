#!/bin/sh
# run-efail.sh — T5 EFAIL runner (FR-GRAPHICS-011, TC-GRAPHICS-032).
# Hermetic host test mirroring engine/graphics/device/run-t1.sh pattern:
# compiles T1 sources with g++ -std=c++17, output to /tmp, timeout-guarded,
# no crash/hang/skip. Prints EFAIL-VERDICT and exits accordingly.
#
# Usage: run-efail.sh [--device SERIAL]
#   Default device is 127.0.0.1:6562 (or $ANDROID_SERIAL when set).
#   Every leg executes or the script FAILs — there is no pending/skip path.
set -u

HERE=$(dirname "$0")
# Absolute probe dir so the script works from any cwd (checkout root or here).
PROBE_DIR=$(cd "$HERE" && pwd)
TIMEOUT_SECS=${EFAIL_TIMEOUT_SECS:-30}
OUT=/tmp/nfr_efail.test
STAGES="init config context surface"
UNKNOWN_STAGE="bogus-stage-efail"

DEVICE="${ANDROID_SERIAL:-127.0.0.1:6562}"
while [ $# -gt 0 ]; do
  case "$1" in
    --device)
      [ $# -lt 2 ] && { echo "EFAIL-VERDICT: FAIL --device needs a SERIAL value"; exit 1; }
      DEVICE="$2"; shift 2;;
    -h|--help)
      echo "Usage: run-efail.sh [--device SERIAL] (default: 127.0.0.1:6562)"; exit 0;;
    --)
      shift; break;;
    -*)
      echo "EFAIL-VERDICT: FAIL unknown option: $1"; exit 1;;
    *)
      echo "EFAIL-VERDICT: FAIL unexpected argument: $1"; exit 1;;
  esac
done
export ANDROID_SERIAL="$DEVICE"

fail() {
  echo "EFAIL-VERDICT: FAIL $1"
  exit 1
}

blocked() {
  echo "BLOCKED $DEVICE $1"
  echo "EFAIL-VERDICT: FAIL BLOCKED $DEVICE $1"
  exit 1
}

need_timeout() {
  command -v timeout >/dev/null 2>&1 || fail "timeout(1) missing — cannot guard legs against hangs"
}

# Run a leg under the timeout guard; echoes the exit code. Usage: run_guarded LOG -- cmd...
run_guarded() {
  _log="$1"; shift
  # shellcheck disable=SC2068
  timeout "$TIMEOUT_SECS" $@ >"$_log" 2>&1
  echo $?
}

# Assert a completed force-fail leg: nonzero, no signal/timeout, diagnostic.
# $1 = label, $2 = exit code, $3 = log file, $4 = expected stage substring.
assert_fail_leg() {
  _label="$1"; _code="$2"; _log="$3"; _stage="$4"
  [ "$_code" -eq 0 ] && fail "$_label exited 0, want nonzero (see $_log)"
  [ "$_code" -eq 124 ] && fail "$_label timed out after ${TIMEOUT_SECS}s (hang)"
  [ "$_code" -ge 128 ] && fail "$_label crashed (signal exit $_code)"
  grep -q "EGL-PROBE-DIAGNOSTIC" "$_log" || fail "$_label log missing 'EGL-PROBE-DIAGNOSTIC' (see $_log)"
  grep -q "$_stage" "$_log" || fail "$_label log missing diagnostic substring '$_stage' (see $_log)"
  echo "PASS $_label (exit $_code, diagnostic names '$_stage')"
}

need_timeout

# 1. Build the host probe binary (Makefile host target; direct g++ fallback
#    mirroring run-t1.sh). The binary MUST exist — no pending/skip path.
if [ ! -x "$PROBE_DIR/egl-probe-host" ]; then
  echo "--- building egl-probe-host (make host) ---"
  if ! make -C "$PROBE_DIR" host >/tmp/nfr_efail.host-build.log 2>&1; then
    echo "--- make host failed; fallback direct g++ line ---"
    cat /tmp/nfr_efail.host-build.log
    if ! g++ -std=c++17 -Wall -Wextra -Werror -O2 -I "$PROBE_DIR/.." \
      "$PROBE_DIR/egl_probe.cc" "$PROBE_DIR/egl_probe_main.cc" \
      -o "$PROBE_DIR/egl-probe-host" -ldl 2>/tmp/nfr_efail.host-build.log; then
      cat /tmp/nfr_efail.host-build.log
      fail "egl-probe-host build failed (make host + g++ fallback)"
    fi
  fi
fi
[ -x "$PROBE_DIR/egl-probe-host" ] || fail "egl-probe-host binary absent after build — matrix cannot run"
echo "host binary: $PROBE_DIR/egl-probe-host"

# 2. Compile the hermetic force-fail test against the T1 sources.
if ! g++ -std=c++17 -Wall -Wextra -Werror -I "$PROBE_DIR/.." \
  "$PROBE_DIR/egl_probe_force_fail.test.cpp" \
  "$PROBE_DIR/egl_probe.cc" \
  -o "$OUT" 2>/tmp/nfr_efail.build.log; then
  echo "--- build log (/tmp/nfr_efail.build.log) ---"
  cat /tmp/nfr_efail.build.log
  fail "compile-error"
fi

# 3. Run the compiled test binary under the timeout guard (no hang).
if ! timeout "$TIMEOUT_SECS" "$OUT"; then
  fail "force-fail-test exited nonzero"
fi

# 4. Host stage matrix: every stage init|config|context|surface must exit
#    nonzero with a stage-naming diagnostic. Every leg executes or FAILs.
for s in $STAGES; do
  LOG=/tmp/nfr_efail.stage.$s.log
  code=$(run_guarded "$LOG" "$PROBE_DIR/egl-probe-host" --force-fail "$s")
  assert_fail_leg "host stage $s" "$code" "$LOG" "$s"
done

# 5. Unknown-stage case: nonzero + diagnostic, never crash/hang/skip.
LOG=/tmp/nfr_efail.stage.unknown.log
code=$(run_guarded "$LOG" "$PROBE_DIR/egl-probe-host" --force-fail "$UNKNOWN_STAGE")
assert_fail_leg "host unknown-stage $UNKNOWN_STAGE" "$code" "$LOG" "$UNKNOWN_STAGE"

# 6. On-device legs against $DEVICE (default 127.0.0.1:6562):
#    muse-native egl-probe --force-fail <stage> --expect-exit nonzero
#    --expect-log EGL-PROBE-DIAGNOSTIC, all four stages, -s on every adb call.
#    Unreachable device => BLOCKED-with-reason (never a silent skip).
#    NOTE: security-coverage legs (section 6b, hermetic) run BEFORE the live
#    device legs so a missing device still leaves the security signal.
# 6b. Security-coverage legs (hermetic: host binary + run-eprobe.sh parse
# layer only; fake adb stub on PATH; no device, no NDK, no network).
# (1) metachar-serial quoting (2) force-fail allow-list
# (3) leading-dash --expect-log (4) hostile-ABI rejection
# (5) config/context diagnostic secrecy.
SEC_TMP=$(mktemp -d /tmp/nfr_efail.sec.XXXXXX) || fail "mktemp for sec legs failed"
SEC_ADB_LOG="$SEC_TMP/adb.log"
SEC_ABI_REPLY="x86_64"
# Fake adb: logs each invocation as <arg> tokens (proves single-arg quoting),
# answers getprop with $SEC_ABI_REPLY_FILE content, push/shell succeed.
echo "x86_64" >"$SEC_TMP/abi.txt"
cat >"$SEC_TMP/bin_adb" <<'STUBEOF'
#!/bin/sh
STUBEOF
mkdir -p "$SEC_TMP/bin"
{
  echo '#!/bin/sh'
  echo 'LOG="'"$SEC_ADB_LOG"'"'
  echo 'ABI_FILE="'"$SEC_TMP/abi.txt"'"'
  echo 'printf "adb-call:" >>"$LOG"'
  echo 'for a in "$@"; do printf "<%s>" "$a" >>"$LOG"; done'
  echo 'printf "\n" >>"$LOG"'
  echo 'for a in "$@"; do case "$a" in *getprop*) cat "$ABI_FILE"; exit 0;; esac; done'
  echo 'case "$*" in *"/data/local/tmp/egl-probe"*) echo "EGL-PROBE-VERDICT: PASS stub"; exit 0;; esac'
  echo 'exit 0'
} >>"$SEC_TMP/bin_adb"
mv "$SEC_TMP/bin_adb" "$SEC_TMP/bin/adb"
chmod +x "$SEC_TMP/bin/adb"
SEC_PATH="PATH=$SEC_TMP/bin:$PATH"

# (2) force-fail allow-list: non-listed stages rejected with diagnostic,
# never passed to a sink (no verdict line, exit 2).
for cand in ';id' 'bogus-stage' '$(id)' 'init;id' '--report'; do
  SEC_LOG=/tmp/nfr_efail.sec.forcereject.log
  code=$(run_guarded "$SEC_LOG" sh "$PROBE_DIR/run-eprobe.sh" --force-fail "$cand")
  [ "$code" -eq 124 ] && fail "sec force-fail reject '$cand' timed out (hang)"
  [ "$code" -ge 128 ] && fail "sec force-fail reject '$cand' crashed (signal exit $code)"
  [ "$code" -ne 2 ] && fail "sec force-fail reject '$cand' exited $code, want 2 (see $SEC_LOG)"
  grep -q "Expected one of" "$SEC_LOG" || fail "sec force-fail reject '$cand' missing allow-list diagnostic (see $SEC_LOG)"
  grep -q "EGL-PROBE-VERDICT" "$SEC_LOG" && fail "sec force-fail reject '$cand' leaked a verdict line past the allow-list gate"
  echo "PASS sec force-fail allow-list rejects '$cand' (exit 2, diagnostic, no sink)"
done
# Empty-string stage: run_guarded word-splits empty argv away, so invoke
# timeout directly with a quoted empty arg. Must still exit 2 with the
# allow-list diagnostic and no verdict line.
SEC_LOG=/tmp/nfr_efail.sec.forcereject.log
timeout "$TIMEOUT_SECS" sh "$PROBE_DIR/run-eprobe.sh" --force-fail "" >"$SEC_LOG" 2>&1
code=$?
[ "$code" -eq 124 ] && fail "sec force-fail reject '' timed out (hang)"
[ "$code" -ge 128 ] && fail "sec force-fail reject '' crashed (signal exit $code)"
[ "$code" -ne 2 ] && fail "sec force-fail reject '' exited $code, want 2 (see $SEC_LOG)"
grep -q "Expected one of" "$SEC_LOG" || fail "sec force-fail reject '' missing allow-list diagnostic (see $SEC_LOG)"
grep -q "EGL-PROBE-VERDICT" "$SEC_LOG" && fail "sec force-fail reject '' leaked a verdict line past the allow-list gate"
echo "PASS sec force-fail allow-list rejects '' (exit 2, diagnostic, no sink)"

# (3) leading-dash --expect-log: value starting with '-' is a pattern, not a
# flag. '-PROBE' is a substring of host output 'EGL-PROBE-VERDICT: ...', so a
# correct 'grep -qF --' matches and the leg exits 0. A flag-injection
# regression (grep without '--') would error on the option and exit nonzero.
SEC_LOG=/tmp/nfr_efail.sec.expectdash.log
code=$(run_guarded "$SEC_LOG" sh "$PROBE_DIR/run-eprobe.sh" --expect-log "-PROBE")
[ "$code" -eq 124 ] && fail "sec expect-log dash leg timed out (hang)"
[ "$code" -ge 128 ] && fail "sec expect-log dash leg crashed (signal exit $code)"
[ "$code" -ne 0 ] && fail "sec expect-log '-PROBE' exited $code, want 0 — leading dash treated as flag (see $SEC_LOG)"
grep -q "EGL-PROBE-VERDICT" "$SEC_LOG" || fail "sec expect-log dash leg missing probe verdict (see $SEC_LOG)"
echo "PASS sec leading-dash --expect-log treated as value (exit 0, pattern matched)"

# (4) hostile-ABI rejection: fake getprop answers '../../etc'; the case gate
# must refuse before any artifact path use. Assert exit 1 + 'unknown ABI',
# exactly one adb call (no push leg => no path traversal into a sink).
echo '../../etc' >"$SEC_TMP/abi.txt"
: >"$SEC_ADB_LOG"
SEC_LOG=/tmp/nfr_efail.sec.abi.log
code=$(run_guarded "$SEC_LOG" env "$SEC_PATH" sh "$PROBE_DIR/run-eprobe.sh" --device sec-test-serial)
[ "$code" -eq 124 ] && fail "sec hostile-ABI leg timed out (hang)"
[ "$code" -ge 128 ] && fail "sec hostile-ABI leg crashed (signal exit $code)"
[ "$code" -ne 1 ] && fail "sec hostile-ABI leg exited $code, want 1 (see $SEC_LOG)"
grep -q "unknown ABI" "$SEC_LOG" || fail "sec hostile-ABI leg missing 'unknown ABI' diagnostic (see $SEC_LOG)"
grep -qF '../../etc' "$SEC_LOG" || fail "sec hostile-ABI leg diagnostic does not name the hostile value (see $SEC_LOG)"
ABI_CALLS=$(grep -c "^adb-call:" "$SEC_ADB_LOG" 2>/dev/null || true)
[ "$ABI_CALLS" -eq 1 ] || fail "sec hostile-ABI leg made $ABI_CALLS adb calls, want exactly 1 (no push after reject)"
grep -q "push" "$SEC_ADB_LOG" && fail "sec hostile-ABI leg reached adb push — traversal value entered a sink"
echo "PASS sec hostile-ABI '../../etc' refused (exit 1, diagnostic, no push)"

# (1) metachar-serial quoting: serials with whitespace/metachars must arrive
# at 'adb -s' as ONE opaque arg; no word-split execution. Fake adb logs argv;
# hostile ABI reply keeps the leg hermetic (rejects before push/build).
# (1) metachar-serial quoting: serials with shell metachars must arrive at
# 'adb -s' as opaque quoted args — no word-split execution, no shell
# evaluation. The DEVICES accumulator word-splits on whitespace (known,
# fail-closed per security review), so each fragment must still be a single
# quoted token. Hostile ABI reply keeps every leg hermetic (rejects before
# push/build) while the fake-adb argv log proves the quoting.
# NOTE: serial legs invoke timeout directly (not via run_guarded) because
# run_guarded re-expands "$@" unquoted and would split serials containing
# whitespace before run-eprobe.sh ever sees them — exactly the accumulator
# behavior under test, which must be exercised by the script under test.
for serial in 'x$(id)y' 'a;b' 'a"b'; do
  : >"$SEC_ADB_LOG"
  SEC_LOG=/tmp/nfr_efail.sec.serial.log
  timeout "$TIMEOUT_SECS" env "$SEC_PATH" sh "$PROBE_DIR/run-eprobe.sh" --device "$serial" >"$SEC_LOG" 2>&1
  code=$?
  [ "$code" -eq 124 ] && fail "sec serial quoting leg '$serial' timed out (hang)"
  # Leg must fail closed (nonzero: hostile-ABI reject) ...
  [ "$code" -eq 0 ] && fail "sec serial quoting leg '$serial' exited 0, want nonzero fail-closed (see $SEC_LOG)"
  # ... with the serial delivered as a single <-s> token ...
  grep -qF "<$serial>" "$SEC_ADB_LOG" || fail "sec serial quoting leg '$serial' not passed as one adb -s arg (see $SEC_ADB_LOG)"
done
# Whitespace/metachar serial: the accumulator splits on whitespace into two
# legs (fail-closed, per security review) — each fragment must still arrive
# as one quoted token, and nothing may execute.
: >"$SEC_ADB_LOG"
rm -f /tmp/nfr_efail.pwned
SEC_LOG=/tmp/nfr_efail.sec.serial.log
timeout "$TIMEOUT_SECS" env "$SEC_PATH" sh "$PROBE_DIR/run-eprobe.sh" --device 'evil;touch /tmp/nfr_efail.pwned' >"$SEC_LOG" 2>&1
code=$?
[ "$code" -eq 124 ] && fail "sec serial quoting leg 'evil;touch ...' timed out (hang)"
[ "$code" -eq 0 ] && fail "sec serial quoting leg 'evil;touch ...' exited 0, want nonzero fail-closed (see $SEC_LOG)"
grep -qF "<evil;touch>" "$SEC_ADB_LOG" || fail "sec serial quoting leg fragment '<evil;touch>' missing (see $SEC_ADB_LOG)"
grep -qF "</tmp/nfr_efail.pwned>" "$SEC_ADB_LOG" || fail "sec serial quoting leg fragment '</tmp/nfr_efail.pwned>' missing (see $SEC_ADB_LOG)"
[ -e /tmp/nfr_efail.pwned ] && fail "sec serial quoting leg executed metachars (pwn file exists)"
# Whitespace serial: accumulator splits into two legs (fail-closed, per
# security review) — each fragment must still arrive as one quoted token.
: >"$SEC_ADB_LOG"
rm -f /tmp/nfr_efail.pwned
SEC_LOG=/tmp/nfr_efail.sec.serial.log
timeout "$TIMEOUT_SECS" env "$SEC_PATH" sh "$PROBE_DIR/run-eprobe.sh" --device 'a b' >"$SEC_LOG" 2>&1
code=$?
[ "$code" -eq 124 ] && fail "sec serial quoting leg 'a b' timed out (hang)"
[ "$code" -eq 0 ] && fail "sec serial quoting leg 'a b' exited 0, want nonzero fail-closed (see $SEC_LOG)"
grep -qF "<a>" "$SEC_ADB_LOG" || fail "sec serial quoting leg 'a b' fragment '<a>' missing (see $SEC_ADB_LOG)"
grep -qF "<b>" "$SEC_ADB_LOG" || fail "sec serial quoting leg 'a b' fragment '<b>' missing (see $SEC_ADB_LOG)"
[ -e /tmp/nfr_efail.pwned ] && fail "sec serial quoting leg 'a b' executed metachars (pwn file exists)"
echo "PASS sec metachar-serial quoting (opaque quoted adb -s legs, fail-closed, no execution)"

# (5) config/context diagnostic secrecy: fixed 'stage + reason' literals only —
# exact-match the CLI-forced diagnostics, then prove a planted env secret is
# never reflected. (In-process init shape is pinned by the .cpp legs above.)
for s in config context; do
  SEC_LOG=/tmp/nfr_efail.sec.diag.$s.log
  code=$(run_guarded "$SEC_LOG" "$PROBE_DIR/egl-probe-host" --force-fail "$s")
  [ "$code" -eq 0 ] && fail "sec diagnostic leg stage $s exited 0, want nonzero"
  want="EGL-PROBE-DIAGNOSTIC: $s forced failure via CLI"
  grep -qF "$want" "$SEC_LOG" || fail "sec diagnostic leg stage $s missing exact fixed diagnostic '$want' (see $SEC_LOG)"
  if grep -q "KEY\|TOKEN\|SECRET" "$SEC_LOG"; then
    fail "sec diagnostic leg stage $s leaks secret-like token (see $SEC_LOG)"
  fi
  echo "PASS sec diagnostic secrecy stage $s (exact fixed reason, no internals)"
done
SEC_LOG=/tmp/nfr_efail.sec.diag.env.log
code=$(run_guarded "$SEC_LOG" env EFAIL_PLANTED_SECRET_FOR_TEST="s3cr3t-planted-xyz-123" "$PROBE_DIR/egl-probe-host" --force-fail config)
grep -q "s3cr3t-planted" "$SEC_LOG" && fail "sec diagnostic env-secret reflected into output (see $SEC_LOG)"
echo "PASS sec diagnostic secrecy env-secret never reflected"
rm -rf "$SEC_TMP"

if ! command -v adb >/dev/null 2>&1; then
  blocked "adb not found on PATH"
fi
FINGERPRINT=""
if ! timeout "$TIMEOUT_SECS" adb -s "$DEVICE" shell getprop ro.build.fingerprint >/tmp/nfr_efail.fp.log 2>&1; then
  blocked "unreachable ($(cat /tmp/nfr_efail.fp.log | tr '\n' ' ' | cut -c1-160))"
fi
FINGERPRINT=$(tr -d '\r\n ' </tmp/nfr_efail.fp.log)
ABI=$(timeout "$TIMEOUT_SECS" adb -s "$DEVICE" shell getprop ro.product.cpu.abi 2>/dev/null | tr -d '\r\n ')
echo "device $DEVICE reachable (fingerprint: $FINGERPRINT; abi: $ABI)"

# Dispatch: prefer `muse-native egl-probe` when it implements the subcommand
# (the T2 CLI wiring; detected by its --help advertising --force-fail —
# the bare `muse-native --help` fallback exits 0 without it), else the
# engine dispatch script (same flags, thin wrapper over the probe binary).
if muse-native egl-probe --help 2>&1 | grep -q "force-fail"; then
  EPROBE="muse-native egl-probe"
else
  EPROBE="sh $PROBE_DIR/run-eprobe.sh"
  echo "note: installed muse-native has no egl-probe subcommand; dispatching via run-eprobe.sh (same flags)"
fi
for s in $STAGES; do
  LOG=/tmp/nfr_efail.device.$s.log
  # shellcheck disable=SC2086
  code=$(run_guarded "$LOG" $EPROBE --device "$DEVICE" --force-fail "$s" --expect-exit nonzero --expect-log EGL-PROBE-DIAGNOSTIC)
  if [ "$code" -eq 124 ]; then
    blocked "leg $s timed out after ${TIMEOUT_SECS}s (hang)"
  fi
  [ "$code" -ge 128 ] && fail "device $DEVICE stage $s crashed (signal exit $code)"
  [ "$code" -eq 0 ] && fail "device $DEVICE stage $s exited 0, want nonzero (see $LOG)"
  grep -q "EGL-PROBE-DIAGNOSTIC" "$LOG" || fail "device $DEVICE stage $s log missing 'EGL-PROBE-DIAGNOSTIC' (see $LOG)"
  grep -q "$s" "$LOG" || fail "device $DEVICE stage $s log missing diagnostic substring '$s' (see $LOG)"
  echo "PASS device $DEVICE stage $s (exit $code, diagnostic names '$s')"
done

echo "EFAIL-VERDICT: PASS"
exit 0
