#!/bin/sh
# TC-GRAPHICS-041 contract test — AC-GRAPHICS-039 (parity leg via AC-035).
# Binds ONLY to the T4 `Interface:` line; never to implementation.
# Hermetic: POSIX sh + md5sum + mktemp only; frames are test-local fixtures
# under a mktemp dir (cleaned via trap); no device, no network, no env/config
# reads, no live services. The `muse-native gallery-parity --cpu-vs-skia
# --assert-md5` CLI itself is the coder's scope (plan A2); this script asserts
# its contract and must FAIL until the coder wires it.
#
# Contract under test:
#   `muse-native gallery-parity --cpu-vs-skia --assert-md5` exits 0 iff
#   md5(cpu_frame) == md5(skia_frame) for the same gallery tree; prints both
#   md5s; nonzero with both md5s on divergence; no tolerance flag.
#
# Layout:
#   Phase A (hermetic, passes now): md5-compare oracle self-check on local
#     fixtures, including the negative control (flipped byte -> divergence).
#   Phase B (red until coder): the real CLI contract check — skipped with
#     FAIL (not skip) when `muse-native` lacks the verb, so the file is RED
#     until T4 lands.
# Expected red: `FAIL cli-present` — `muse-native: ... gallery-parity ...
# not found` (verb does not exist until the coder adds it).
set -u

failures=0
pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s\n' "$1"; failures=$((failures + 1)); }

TMPD="$(mktemp -d)" || { printf 'FAIL mktemp\n'; exit 1; }
trap 'rm -rf "$TMPD"' EXIT INT TERM

md5_of() { md5sum "$1" | awk '{print $1}'; }

# Phase A1: identical trees -> md5s match (positive control).
printf 'gallery-tree-frame-v1' > "$TMPD/cpu_frame.bin"
cp "$TMPD/cpu_frame.bin" "$TMPD/skia_frame.bin"
CPU_MD5="$(md5_of "$TMPD/cpu_frame.bin")"
SKIA_MD5="$(md5_of "$TMPD/skia_frame.bin")"
printf 'cpu_md5=%s\nskia_md5=%s\n' "$CPU_MD5" "$SKIA_MD5"
if [ "$CPU_MD5" = "$SKIA_MD5" ]; then
  pass oracle-identical-match
else
  fail oracle-identical-match
fi

# Phase A2: negative control — one flipped byte must diverge (proves the
# oracle fails on a real raster divergence, never spuriously green).
printf 'X' | dd of="$TMPD/skia_frame.bin" bs=1 seek=0 conv=notrunc status=none 2>/dev/null || \
  printf 'X' > "$TMPD/skia_frame.bin"
CPU_MD5_B="$(md5_of "$TMPD/cpu_frame.bin")"
SKIA_MD5_B="$(md5_of "$TMPD/skia_frame.bin")"
printf 'cpu_md5=%s\nskia_md5=%s\n' "$CPU_MD5_B" "$SKIA_MD5_B"
if [ "$CPU_MD5_B" != "$SKIA_MD5_B" ]; then
  pass negative-control-flipped-byte-diverges
else
  fail negative-control-flipped-byte-diverges
fi

# Phase B: the real CLI contract (coder's T4 scope: CLI verb + Makefile
# wiring reusing the TC-035 `gallery-parity --cpu-vs-egl` harness shape).
BIN="${MUSE_NATIVE_BIN:-muse-native}"
if ! command -v "$BIN" >/dev/null 2>&1; then
  fail cli-present
  printf 'detail: %s not on PATH (coder wires `gallery-parity --cpu-vs-skia --assert-md5`)\n' "$BIN"
elif ! "$BIN" gallery-parity --help 2>&1 | grep -q -- '--cpu-vs-skia'; then
  fail cli-present
  printf 'detail: %s lacks `gallery-parity --cpu-vs-skia` (coder wires it)\n' "$BIN"
else
  OUT="$("$BIN" gallery-parity --cpu-vs-skia --assert-md5 2>&1)"
  STATUS="$?"
  printf '%s\n' "$OUT"
  # Contract: both md5s printed on either outcome; exit 0 iff they match.
  OUT_CPU="$(printf '%s\n' "$OUT" | grep -Eo '[0-9a-f]{32}' | sed -n '1p')"
  OUT_SKIA="$(printf '%s\n' "$OUT" | grep -Eo '[0-9a-f]{32}' | sed -n '2p')"
  if [ -z "$OUT_CPU" ] || [ -z "$OUT_SKIA" ]; then
    fail cli-prints-both-md5s
  else
    pass cli-prints-both-md5s
    if [ "$OUT_CPU" = "$OUT_SKIA" ]; then
      if [ "$STATUS" -eq 0 ]; then pass cli-match-exits-zero; else fail cli-match-exits-zero; fi
    else
      if [ "$STATUS" -ne 0 ]; then pass cli-divergence-exits-nonzero; else fail cli-divergence-exits-nonzero; fi
    fi
  fi
  # Contract: no tolerance flag exists.
  if "$BIN" gallery-parity --help 2>&1 | grep -q -- '--tolerance'; then
    fail cli-no-tolerance-flag
  else
    pass cli-no-tolerance-flag
  fi
fi

if [ "$failures" -gt 0 ]; then
  printf 'skia_parity.test.sh: %d failing\n' "$failures"
  exit 1
fi
printf 'skia_parity.test.sh: all passing\n'
exit 0
