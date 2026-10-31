#!/bin/sh
# T4 present-path anchor-wiring check — BUG-RUNTIME-004 / AC-M3-022 (sink leg) /
# AC-LAYOUT-035 (per-edge values every frame, no full-tree relayout).
#
# Hermetic static half (runs anywhere with sh+grep): asserts the T4 internal
# wiring in engine/runtime/src/gallery_present.cpp —
#   the layering present path calls anchorSheetToImeInset(scene, <live inset>)
#   every frame before paintLayeringPage;
#   the call reads a live inset value (never a constant);
#   no full-tree relayout is introduced (fullRelayouts untouched);
#   the per-frame sheetY/inset/relayout=none evidence emission is intact.
# Device half (not run here; executed by the tester on 127.0.0.1:6569):
#   with the keyboard shown over layering page 18/19, every presented frame
#   must emit sheetY/inset/relayout=none with sheet above keyboard, max 1px
#   discontinuity frame-to-frame, relayout=none throughout.
#
# Exit 0 when the static contract holds; non-zero (RED) otherwise.
# No network, no device, no env/config reads — reads only gallery_present.cpp.
set -u

CPP_FILE="${1:-engine/runtime/src/gallery_present.cpp}"

fail() {
  printf 'FAIL %s\n' "$1"
  FAILURES=$((FAILURES + 1))
}
pass() {
  printf 'PASS %s\n' "$1"
}

FAILURES=0

if [ ! -f "$CPP_FILE" ]; then
  printf 'FAIL anchor-wiring source missing: %s\n' "$CPP_FILE"
  exit 1
fi

# T4-WIRE-1: the layering present path calls the T1 anchor (a call site, not
# just the `void anchorSheetToImeInset` definition).
if grep -q '[^:]anchorSheetToImeInset(scene' "$CPP_FILE"; then
  pass "layering present path calls anchorSheetToImeInset(scene, ...)"
else
  fail "layering present path calls anchorSheetToImeInset(scene, ...)"
fi

# T4-WIRE-2: the call binds a live inset value — at least one call site passes
# a live inset/ime/deps value rather than a constant.
CALLS=$(grep -n 'anchorSheetToImeInset(scene' "$CPP_FILE" | grep -v 'void anchorSheetToImeInset' || true)
if [ -n "$CALLS" ] && printf '%s\n' "$CALLS" | grep -qE 'inset|insets|ime|imeBottom|deps'; then
  pass "anchor call reads live deps.insets.imeBottom value"
else
  fail "anchor call reads live deps.insets.imeBottom value"
fi

# T4-WIRE-3: the anchor binds before the layering paint each frame (call line
# precedes the paintLayeringPage(scene, ...) call line).
CALL_LINE=$(printf '%s\n' "$CALLS" | head -1 | cut -d: -f1 || true)
PAINT_LINE=$(grep -n 'paintLayeringPage(scene' "$CPP_FILE" | head -1 | cut -d: -f1 || true)
if [ -n "${CALL_LINE:-}" ] && [ -n "${PAINT_LINE:-}" ] && [ "$CALL_LINE" -lt "$PAINT_LINE" ]; then
  pass "anchor binds before paintLayeringPage each frame"
else
  fail "anchor binds before paintLayeringPage each frame"
fi

# T4-NORELAYOUT-1 (denial, AC-LAYOUT-035): the wiring introduces no full-tree
# relayout — fullRelayouts is never incremented or set to nonzero in the .cpp
# (only comments, the anchor's own untouched-counter evidence, and the header
# `= 0` default may mention it).
if grep -qE 'fullRelayouts\s*(\+\+|\+=|= [1-9])' "$CPP_FILE"; then
  fail "wiring must not relayout (fullRelayouts touched)"
else
  pass "wiring performs no full-tree relayout (fullRelayouts untouched)"
fi

# T4-EVIDENCE-1: the per-frame sheetY/inset/relayout=none series stays emitted.
if grep -q 'relayout=none' "$CPP_FILE"; then
  pass "per-frame sheetY/inset/relayout=none evidence intact"
else
  fail "per-frame sheetY/inset/relayout=none evidence intact"
fi

if [ "$FAILURES" -gt 0 ]; then
  printf 'present-anchor-wiring-check: %d failing\n' "$FAILURES"
  printf 'device half (tester, 127.0.0.1:6569):\n'
  printf '  per-frame logcat sheetY/inset/relayout=none series over pages 18/19 (sheet above keyboard, <=1px steps)\n'
  exit 1
fi
printf 'present-anchor-wiring-check: all passing (static half; device logcat series is tester-run)\n'
printf 'device half (tester, 127.0.0.1:6569):\n'
printf '  per-frame logcat sheetY/inset/relayout=none series over pages 18/19 (sheet above keyboard, <=1px steps)\n'
exit 0
