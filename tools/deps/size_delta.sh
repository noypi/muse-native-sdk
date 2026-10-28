#!/bin/sh
# size_delta.sh — measured dependency size delta (FR-DEPS-001 / AC-DEPS-002).
# Sizes via portable wc -c; delta = candidate - reference (signed).
# Both binaries are required inputs — never estimates. Missing/unreadable
# input or incomplete args fail closed (exit 1).
set -u

usage() {
  echo "usage: size_delta.sh [--self-test] --reference <ref-binary> --candidate <cand-binary>" >&2
}

file_size() {
  wc -c < "$1" | tr -d ' \t\n'
}

make_fixture() {
  # make_fixture <path> <bytes>: fixed-size zero-filled fixture file.
  dd if=/dev/zero of="$1" bs=1 count="$2" 2>/dev/null
}

run_case() {
  # run_case <name> <ref-bytes|MISSING:actual-path> <cand-bytes|...> <expect-exit> [expect-delta expect-ref expect-cand]
  _name="$1"
  _refspec="$2"
  _candspec="$3"
  _expect_exit="$4"
  _refpath=""
  _candpath=""
  case "$_refspec" in
    MISSING:*) _refpath="${_refspec#MISSING:}" ;;
    *) _refpath="$TMPDIR/ref.bin"; make_fixture "$_refpath" "$_refspec" ;;
  esac
  case "$_candspec" in
    MISSING:*) _candpath="${_candspec#MISSING:}" ;;
    *) _candpath="$TMPDIR/cand.bin"; make_fixture "$_candpath" "$_candspec" ;;
  esac
  _out="$("$0" --reference "$_refpath" --candidate "$_candpath" 2>"$TMPDIR/stderr.txt")"
  _rc="$?"
  rm -f "$_refpath" "$_candpath"
  if [ "$_rc" -ne "$_expect_exit" ]; then
    echo "self-test FAIL ($_name): exit $_rc, expected $_expect_exit" >&2
    echo "$_out" >&2
    return 1
  fi
  if [ "$_expect_exit" -eq 0 ]; then
    _delta="$5"; _r="$6"; _c="$7"
    _want_delta="size_delta_bytes=$_delta"
    _want_pr="Dependency size delta (same-commit reference): $_delta bytes ($_r → $_c)"
    case "$_out" in
      *"$_want_delta"*) : ;;
      *) echo "self-test FAIL ($_name): missing '$_want_delta'" >&2; echo "$_out" >&2; return 1 ;;
    esac
    case "$_out" in
      *"$_want_pr"*) : ;;
      *) echo "self-test FAIL ($_name): missing PR line '$_want_pr'" >&2; echo "$_out" >&2; return 1 ;;
    esac
  else
    case "$_out" in
      *"size_delta_bytes="*)
        echo "self-test FAIL ($_name): delta emitted on failure path" >&2
        return 1
        ;;
    esac
  fi
  echo "self-test ok ($_name)"
  return 0
}

self_test() {
  TMPDIR="${TMPDIR:-/tmp}/size_delta_selftest_$$"
  mkdir -p "$TMPDIR" || { echo "self-test FAIL: cannot create TMPDIR" >&2; return 1; }
  _fail=0
  run_case "growth 100->150" 100 150 0 50 100 150 || _fail=1
  run_case "shrinkage 200->150" 200 150 0 -50 200 150 || _fail=1
  run_case "identical 128->128" 128 128 0 0 128 128 || _fail=1
  # Missing binary fails closed: no delta line, exit 1.
  make_fixture "$TMPDIR/only.bin" 64
  if "$0" --reference "$TMPDIR/only.bin" --candidate "$TMPDIR/nope.bin" >"$TMPDIR/out.txt" 2>/dev/null; then
    echo "self-test FAIL (missing-candidate): exit 0, expected 1" >&2; _fail=1
  else
    if grep -q "size_delta_bytes=" "$TMPDIR/out.txt"; then
      echo "self-test FAIL (missing-candidate): delta emitted" >&2; _fail=1
    else
      echo "self-test ok (missing-candidate)"
    fi
  fi
  if "$0" --reference "$TMPDIR/nope.bin" --candidate "$TMPDIR/only.bin" >"$TMPDIR/out.txt" 2>/dev/null; then
    echo "self-test FAIL (missing-reference): exit 0, expected 1" >&2; _fail=1
  else
    if grep -q "size_delta_bytes=" "$TMPDIR/out.txt"; then
      echo "self-test FAIL (missing-reference): delta emitted" >&2; _fail=1
    else
      echo "self-test ok (missing-reference)"
    fi
  fi
  rm -f "$TMPDIR/only.bin" "$TMPDIR/out.txt"
  # Incomplete args fail closed: exit 1 with usage.
  for _args in "none" "no-candidate" "no-reference"; do
    case "$_args" in
      none) _err="$("$0" 2>&1)"; _rc="$?" ;;
      no-candidate) make_fixture "$TMPDIR/a.bin" 10; _err="$("$0" --reference "$TMPDIR/a.bin" 2>&1)"; _rc="$?"; rm -f "$TMPDIR/a.bin" ;;
      no-reference) make_fixture "$TMPDIR/b.bin" 10; _err="$("$0" --candidate "$TMPDIR/b.bin" 2>&1)"; _rc="$?"; rm -f "$TMPDIR/b.bin" ;;
    esac
    if [ "$_rc" -eq 0 ]; then
      echo "self-test FAIL (incomplete-$_args): exit 0, expected 1" >&2; _fail=1
    else
      case "$_err" in
        *usage:*) echo "self-test ok (incomplete-$_args)" ;;
        *) echo "self-test FAIL (incomplete-$_args): no usage message" >&2; _fail=1 ;;
      esac
    fi
  done
  rm -rf "$TMPDIR"
  if [ "$_fail" -ne 0 ]; then
    echo "self-test FAILED" >&2
    return 1
  fi
  echo "self-test passed"
  return 0
}

SELFTEST=0
REF=""
CAND=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --self-test) SELFTEST=1; shift ;;
    --reference)
      [ "$#" -ge 2 ] || { usage; exit 1; }
      REF="$2"; shift 2 ;;
    --candidate)
      [ "$#" -ge 2 ] || { usage; exit 1; }
      CAND="$2"; shift 2 ;;
    -h|--help) usage; exit 1 ;;
    *) usage; exit 1 ;;
  esac
done

if [ "$SELFTEST" -eq 1 ] && [ -z "$REF" ] && [ -z "$CAND" ]; then
  self_test
  exit "$?"
fi

if [ -z "$REF" ] || [ -z "$CAND" ]; then
  usage
  exit 1
fi

if [ ! -f "$REF" ] || [ ! -r "$REF" ]; then
  echo "size_delta.sh: reference binary missing or unreadable: $REF" >&2
  exit 1
fi
if [ ! -f "$CAND" ] || [ ! -r "$CAND" ]; then
  echo "size_delta.sh: candidate binary missing or unreadable: $CAND" >&2
  exit 1
fi

R="$(file_size "$REF")"
C="$(file_size "$CAND")"
case "$R" in ''|*[!0-9]*) echo "size_delta.sh: cannot measure reference size" >&2; exit 1 ;; esac
case "$C" in ''|*[!0-9]*) echo "size_delta.sh: cannot measure candidate size" >&2; exit 1 ;; esac

DELTA=$((C - R))
echo "size_delta_bytes=$DELTA"
echo "Dependency size delta (same-commit reference): $DELTA bytes ($R → $C)"
exit 0
