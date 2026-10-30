#!/bin/sh
# T2 host-side input-view check — BUG-RUNTIME-004 / AC-RUNTIME-009 (IME summon
# leg) / AC-M3-022 (source leg) / D1 (TextInput + MuseField form).
#
# Hermetic static half (runs anywhere with sh+grep): asserts the D1 input-view
# form in examples/m3-gallery/src/MainActivity.java —
#   focusable input view realized as TextInput + MuseField widgets/views,
#   backed by a non-visible EditText editor path (D1 amended, T5-retry),
#   no bare hidden EditText, no SurfaceView/EditText-subclass editor.
# Device half (not run here; executed by the tester on 127.0.0.1:6569):
#   adb -s 127.0.0.1:6569 shell "dumpsys input_method | grep -E 'mInputShown|mShowRequested|mCurrentFocus'"
# must show a focusable com.muse.host target after tapping the L7 probe row.
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
  printf 'FAIL input-view source missing: %s\n' "$JAVA_FILE"
  exit 1
fi

# T2-D1-1: a TextInput-backed input view is referenced.
if grep -q 'TextInput' "$JAVA_FILE"; then
  pass "mainactivity references TextInput widget/view"
else
  fail "mainactivity references TextInput widget/view"
fi

# T2-D1-2: a MuseField-backed input view is referenced.
if grep -q 'MuseField' "$JAVA_FILE"; then
  pass "mainactivity references MuseField widget/view"
else
  fail "mainactivity references MuseField widget/view"
fi

# T2-D1-3 (denial, re-scoped per D1-amended / T5-retry): no *bare* hidden
# EditText editor. The authorized fix backs the input view with a
# non-visible EditText, so `new EditText(` is allowed ONLY when tied to the
# TextInput/MuseField refs (both present in this file). A bare
# `new EditText(` with no TextInput/MuseField reference alongside remains
# the forbidden form per D1.
if grep -q 'new EditText(' "$JAVA_FILE"; then
  if grep -q 'TextInput' "$JAVA_FILE" && grep -q 'MuseField' "$JAVA_FILE"; then
    pass "EditText backing is tied to TextInput/MuseField refs (authorized D1-amended form)"
  else
    fail "mainactivity must not use a bare hidden EditText editor (no TextInput/MuseField refs)"
  fi
else
  pass "mainactivity has no bare hidden EditText editor"
fi

# T2-D1-4 (denial, re-scoped per D1-amended / T5-retry): no
# SurfaceView/EditText-subclass editor (the SurfaceView present in the file
# must remain the render surface only, never an input editor; the editor
# must be reached by composition, never by subclassing). NOTE:
# `onCreateInputConnection` is deliberately NOT denied here — the
# authorized fix REQUIRES an onCheckIsTextEditor/onCreateInputConnection
# path (see T2-D1-5); only subclassing is forbidden.
if grep -qE 'class .* extends SurfaceView|extends SurfaceView|extends .*EditText' "$JAVA_FILE"; then
  fail "mainactivity must not subclass SurfaceView/EditText as an editor"
else
  pass "mainactivity has no SurfaceView-subclass editor"
fi

# T2-D1-5 (regression, T5-retry device 127.0.0.1:6569): editor-capable input
# path. Evidence workflow.local/runs/bug-runtime-004/evidence/t5r_dumpsys_ime.txt
# shows the 1x1 plain Views take focus but expose no InputConnection
# (`mServedInputConnectionWrapper=null`), so `showSoftInput` is a no-op and
# LatinIME never shows. MainActivity must therefore back the TextInput /
# MuseField refs with a non-visible EditText (same TextInput/MuseField
# tags/refs, still no SurfaceView-subclass editor, touch routing unchanged)
# so the IME gets an onCheckIsTextEditor/onCreateInputConnection path.
if grep -q 'EditText' "$JAVA_FILE" || grep -q 'onCreateInputConnection' "$JAVA_FILE"; then
  if grep -q 'TextInput' "$JAVA_FILE" && grep -q 'MuseField' "$JAVA_FILE"; then
    pass "mainactivity provides an editor-capable input path tied to TextInput/MuseField refs"
  else
    fail "editor-capable input path must stay tied to TextInput/MuseField refs"
  fi
else
  fail "mainactivity provides an editor-capable input path (EditText or onCreateInputConnection) tied to TextInput/MuseField refs"
fi

# T2-routing guard (A3): native-first touch routing and page-flip fallback
# survive the input-view addition — the existing dispatch + fallback lines
# must still be present.
if grep -q 'nativeDispatchTouch' "$JAVA_FILE" && grep -q 'nativeAdvancePage' "$JAVA_FILE"; then
  pass "native-first touch routing + page-flip fallback intact"
else
  fail "native-first touch routing + page-flip fallback intact"
fi

if [ "$FAILURES" -gt 0 ]; then
  printf 'input-view-host-check: %d failing\n' "$FAILURES"
  printf 'device half (tester, 127.0.0.1:6569):\n'
  printf '  adb -s 127.0.0.1:6569 shell "dumpsys input_method | grep -E %s"\n' "'mInputShown|mShowRequested|mCurrentFocus'"
  exit 1
fi
printf 'input-view-host-check: all passing (static half; device dumpsys half is tester-run)\n'
printf 'device half (tester, 127.0.0.1:6569):\n'
printf '  adb -s 127.0.0.1:6569 shell "dumpsys input_method | grep -E %s"\n' "'mInputShown|mShowRequested|mCurrentFocus'"
exit 0
