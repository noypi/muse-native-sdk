#!/bin/sh
# T3 host-side IME bus-feed check — BUG-RUNTIME-004 / FR-LAYOUT-012 (typed inset
# bus) / AC-LAYOUT-033 (lockstep) / AC-RUNTIME-010 (field follows inset within
# one frame) / D2 (feed animated ime() heights per frame into the existing
# deps.insets.imeBottom px channel, no parallel channel).
#
# Hermetic static half (runs anywhere with sh+grep): asserts the T3 listener +
# forwarding form in examples/m3-gallery/src/MainActivity.java —
#   WindowInsetsAnimation.Callback (or ViewCompat / OnApplyWindowInsetsListener
#   equivalent) registered;
#   animated ime() heights read per frame;
#   onInsetsFrame(imeBottomPx: float): void forwards into the JNI setter feeding
#   deps.insets.imeBottom (same field T1's anchor consumes);
#   exactly one onInsetsFrame definition (no parallel channel).
# Device half (not run here; executed by the tester on 127.0.0.1:6569):
#   keyboard show/hide must produce animated imeBottom frames observable on the
#   FR-LAYOUT-012 bus (deps.insets) per frame; grep must show a single feed.
#
# Exit 0 when the static contract holds; non-zero (RED) otherwise.
# No network, no device, no env/config reads — reads only MainActivity.java.
set -u

JAVA_FILE="${1:-examples/m3-gallery/src/MainActivity.java}"

fail() {
  printf 'FAIL %s\n' "$1"
  FAILURES=$((FAILURES + 1))
}
pass() {
  printf 'PASS %s\n' "$1"
}

FAILURES=0

if [ ! -f "$JAVA_FILE" ]; then
  printf 'FAIL ime bus-feed source missing: %s\n' "$JAVA_FILE"
  exit 1
fi

# T3-FEED-1: an animated-insets listener is registered (Callback or compat).
if grep -qE 'WindowInsetsAnimation|setOnApplyWindowInsetsListener|OnApplyWindowInsetsListener|ViewCompat' "$JAVA_FILE"; then
  pass "mainactivity registers an animated WindowInsets listener"
else
  fail "mainactivity registers an animated WindowInsets listener"
fi

# T3-FEED-2: animated ime() heights are read per frame (a real inset query —
# Type.ime() / getInsets(, never a substring of an unrelated identifier).
if grep -qE 'Type\.ime\(\)|getInsets\(' "$JAVA_FILE"; then
  pass "mainactivity reads animated ime() heights per frame"
else
  fail "mainactivity reads animated ime() heights per frame"
fi

# T3-FEED-3: the plan Interface entry exists — onInsetsFrame forwards the px
# value toward the JNI setter / deps.insets.imeBottom channel.
if grep -q 'onInsetsFrame' "$JAVA_FILE"; then
  pass "mainactivity forwards via onInsetsFrame(imeBottomPx)"
else
  fail "mainactivity forwards via onInsetsFrame(imeBottomPx)"
fi

# T3-FEED-4: the feed targets the FR-LAYOUT-012 typed bus field the T1 anchor
# consumes (deps.insets.imeBottom), not an untyped side store.
if grep -q 'imeBottom' "$JAVA_FILE"; then
  pass "mainactivity feeds the imeBottom px channel"
else
  fail "mainactivity feeds the imeBottom px channel"
fi

# T3-SINGLE-1 (denial, D2): no parallel channel — exactly one onInsetsFrame
# definition. A second entry point would be a second feed and must fail.
DEFS=$(grep -c 'void onInsetsFrame' "$JAVA_FILE" 2>/dev/null || true)
if [ "$DEFS" -eq 1 ]; then
  pass "single inset feed (exactly one onInsetsFrame definition)"
else
  fail "single inset feed (exactly one onInsetsFrame definition; found $DEFS)"
fi

if [ "$FAILURES" -gt 0 ]; then
  printf 'ime-bus-feed-check: %d failing\n' "$FAILURES"
  printf 'device half (tester, 127.0.0.1:6569):\n'
  printf '  keyboard show/hide must emit animated imeBottom frames on deps.insets per frame (single feed)\n'
  exit 1
fi
printf 'ime-bus-feed-check: all passing (static half; device bus-observable half is tester-run)\n'
printf 'device half (tester, 127.0.0.1:6569):\n'
printf '  keyboard show/hide must emit animated imeBottom frames on deps.insets per frame (single feed)\n'
exit 0
