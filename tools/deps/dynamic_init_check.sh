#!/bin/sh
# dynamic_init_check.sh — startup-path dynamic-initializer admission check
# (FR-DEPS-001 / AC-DEPS-003, TC-DEPS-003; failure cites CORE-012).
#
# Detection probe: dynamic-section scan via `readelf -d` (GNU binutils) or
# `llvm-readobj --dynamic-table` (LLVM), plus a portable byte fallback scan
# (`grep -a`) for initializer evidence markers (DT_INIT / DT_INIT_ARRAY /
# .init_array / __static_initialization_and_destruction). A file is rejected
# when either the toolchain probe output or the fallback scan names one of
# those markers. The fallback exists so synthetic (non-ELF) fixtures and
# toolchains where `readelf` cannot parse the input are still fail-closed.
# When neither `readelf` nor `llvm-readobj` exists on PATH the check fails
# closed (exit 1) naming the missing tools instead of silently passing.
#
# Usage: dynamic_init_check.sh [--self-test] [--manifest PATH] [obj ...]
#   --self-test   run hermetic fixture cases in TMPDIR, exit 0 iff all pass.
#   --manifest P  manifest path must exist/readable when given (fail closed).
#   obj ...       object/binary files to scan.
# Exit contract: exit 0 when no startup-path dynamic initializer found;
#   exit 1 naming the offending object AND symbol/section and citing CORE-012
#   on any initializer hit, missing/unreadable input, or missing probe tool.
#
# Convention: `#!/bin/sh` + `set -u` (cf. scripts/scan-keys.sh).

set -u

MANIFEST=""
SELFTEST=0
PROBE_OVERRIDE="${DYNAMIC_INIT_CHECK_PROBE_PATH:-}"

probe_cmd() {
  # Echo the probe binary to use, or nothing when none exists.
  if [ -n "$PROBE_OVERRIDE" ]; then
    if [ -x "$PROBE_OVERRIDE/readelf" ]; then
      printf '%s' "$PROBE_OVERRIDE/readelf"
    elif [ -x "$PROBE_OVERRIDE/llvm-readobj" ]; then
      printf '%s' "$PROBE_OVERRIDE/llvm-readobj"
    fi
    return 0
  fi
  if command -v readelf >/dev/null 2>&1; then
    command -v readelf
  elif command -v llvm-readobj >/dev/null 2>&1; then
    command -v llvm-readobj
  fi
}

check_one() {
  # check_one FILE: scan one object; exit 0 clean, 1 with CORE-012 report.
  obj="$1"
  if [ ! -r "$obj" ]; then
    printf 'dynamic_init_check: deny: %s: unreadable or missing input (CORE-012)\n' "$obj"
    return 1
  fi
  tool="$(probe_cmd)"
  if [ -z "$tool" ]; then
    printf 'dynamic_init_check: deny: missing probe tool: neither readelf nor llvm-readobj found on PATH (CORE-012)\n'
    return 1
  fi
  evidence=""
  case "$tool" in
    *llvm-readobj*)
      evidence="$("$tool" --dynamic-table "$obj" 2>/dev/null | grep -aEo 'DT_INIT(_ARRAY)?|\.init_array' | head -n 1)"
      ;;
    *)
      evidence="$("$tool" -d "$obj" 2>/dev/null | grep -aEo 'INIT(_ARRAY)?|init_array' | head -n 1)"
      ;;
  esac
  if [ -z "$evidence" ]; then
    # Portable byte fallback so synthetic fixtures are still detected.
    evidence="$(grep -aEo 'DT_INIT_ARRAY|DT_INIT|\.init_array|__static_initialization_and_destruction' "$obj" 2>/dev/null | head -n 1)"
  fi
  if [ -n "$evidence" ]; then
    printf 'dynamic_init_check: deny: %s: startup-path dynamic initializer: %s (CORE-012)\n' "$obj" "$evidence"
    return 1
  fi
  return 0
}

run_self_test() {
  tmpdir="${TMPDIR:-/tmp}/dyninit-selftest-$$"
  mkdir -p "$tmpdir" || { printf 'self-test: FAIL: cannot create tmpdir\n'; return 1; }
  failures=""
  fail() { printf 'self-test: FAIL: %s\n' "$1"; failures="x${failures}x"; }
  pass() { printf 'self-test: ok: %s\n' "$1"; }
  script_path="$0"
  case "$script_path" in
    */*) ;;
    *) script_path="./$script_path" ;;
  esac

  clean="$tmpdir/clean.o"
  printf 'clean object with no initializers\n.text nop nop\n' > "$clean"
  out="$("$script_path" "$clean" 2>&1)"; rc=$?
  if [ "$rc" -eq 0 ]; then
    case "$out" in
      *CORE-012*) fail "case 1 (clean): output must not cite CORE-012" ;;
      *) pass "case 1 (clean passes)" ;;
    esac
  else
    fail "case 1 (clean): expected exit 0, got $rc: $out"
  fi

  bad="$tmpdir/init.o"
  printf 'object with .init_array DT_INIT_ARRAY entry\n' > "$bad"
  out="$("$script_path" "$bad" 2>&1)"; rc=$?
  if [ "$rc" -ne 1 ]; then
    fail "case 2 (initializer): expected exit 1, got $rc: $out"
  else
    ok=1
    case "$out" in *"$bad"*) ;; *) ok=0 ;; esac
    case "$out" in *init*|*INIT*) ;; *) ok=0 ;; esac
    case "$out" in *CORE-012*) ;; *) ok=0 ;; esac
    if [ "$ok" -eq 1 ]; then pass "case 2 (initializer names object+symbol+CORE-012)";
    else fail "case 2 (initializer): must name object AND symbol/section + CORE-012, got: $out"; fi
  fi

  missing="$tmpdir/does-not-exist.o"
  out="$("$script_path" "$missing" 2>&1)"; rc=$?
  if [ "$rc" -ne 1 ]; then fail "case 3 (missing input): expected exit 1, got $rc: $out";
  else pass "case 3 (missing input fails closed)"; fi
  out="$("$script_path" --manifest "$tmpdir/no-manifest.json" "$clean" 2>&1)"; rc=$?
  if [ "$rc" -ne 1 ]; then fail "case 3b (missing manifest): expected exit 1, got $rc: $out";
  else pass "case 3b (missing manifest fails closed)"; fi

  stub="$tmpdir/empty-path"
  mkdir -p "$stub"
  sh_bin="$(command -v sh)"
  case "$script_path" in
    /*) abs_script="$script_path" ;;
    *) abs_script="$PWD/$script_path" ;;
  esac
  out="$(PATH="$stub" DYNAMIC_INIT_CHECK_PROBE_PATH="$stub" "$sh_bin" "$abs_script" "$clean" 2>&1)"; rc=$?
  if [ "$rc" -ne 1 ]; then fail "case 4 (no probe tool): expected exit 1, got $rc: $out";
  else
    case "$out" in *readelf*|*llvm-readobj*|*probe*)
      pass "case 4 (no probe tool fails closed with named reason)" ;;
      *) fail "case 4 (no probe tool): must name missing tool/reason, got: $out" ;;
    esac
  fi

  rm -rf "$tmpdir"
  if [ -n "$failures" ]; then
    printf 'self-test: RESULT FAIL\n'
    return 1
  fi
  printf 'self-test: RESULT ok\n'
  return 0
}

MODE_CHECK=1
while [ $# -gt 0 ]; do
  case "$1" in
    --self-test) SELFTEST=1; shift ;;
    --manifest)
      if [ $# -lt 2 ]; then
        printf 'dynamic_init_check: deny: --manifest requires a path (CORE-012)\n'
        exit 1
      fi
      MANIFEST="$2"; shift 2 ;;
    --manifest=*) MANIFEST="${1#--manifest=}"; shift ;;
    -h|--help)
      printf 'Usage: dynamic_init_check.sh [--self-test] [--manifest PATH] [obj ...]\n'
      exit 0 ;;
    --) shift; break ;;
    --*) printf 'dynamic_init_check: deny: unknown option %s (CORE-012)\n' "$1"; exit 1 ;;
    *) break ;;
  esac
done

if [ "$SELFTEST" -eq 1 ]; then
  run_self_test
  exit $?
fi

if [ -n "$MANIFEST" ] && [ ! -r "$MANIFEST" ]; then
  printf 'dynamic_init_check: deny: manifest %s unreadable or missing (CORE-012)\n' "$MANIFEST"
  exit 1
fi

if [ $# -eq 0 ]; then
  tool="$(probe_cmd)"
  if [ -z "$tool" ]; then
    printf 'dynamic_init_check: deny: missing probe tool: neither readelf nor llvm-readobj found on PATH (CORE-012)\n'
    exit 1
  fi
  exit 0
fi

rc=0
for obj in "$@"; do
  if ! check_one "$obj"; then
    rc=1
  fi
done
exit "$rc"
