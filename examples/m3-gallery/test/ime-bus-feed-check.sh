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

# T3-API29-1 (regression, bug-runtime-004 T5 device proof on API 29):
# the SDK_INT < 30 fallback inside attachImeInsetListener must NOT reference
# WindowInsets.Type.ime() (API-30+ method — NoSuchMethodError at
# MainActivity.java:301, crash on launch on the pinned API-29 image). The
# API-30+ branch above keeps Type.ime(); only the fallback block is scanned.
ELSE_LINE=$(awk '/attachImeInsetListener\(View root\)/{seen=1} seen && /\} else \{/{print NR; exit}' "$JAVA_FILE" || true)
FALLBACK_BLOCK=""
if [ -n "${ELSE_LINE:-}" ]; then
  FALLBACK_END=$((ELSE_LINE + 12))
  FALLBACK_BLOCK=$(sed -n "${ELSE_LINE},${FALLBACK_END}p" "$JAVA_FILE")
fi
if [ -z "$FALLBACK_BLOCK" ]; then
  fail "pre-30 fallback avoids WindowInsets.Type.ime() (API-29 safe)"
elif printf '%s\n' "$FALLBACK_BLOCK" | grep -q 'Type\.ime()'; then
  fail "pre-30 fallback avoids WindowInsets.Type.ime() (API-29 safe)"
else
  pass "pre-30 fallback avoids WindowInsets.Type.ime() (API-29 safe)"
fi

# T3-API29-2 (regression, bug-runtime-004 T5): the pre-30 fallback must use
# the API-21 compat shape WindowInsets.getSystemWindowInsetBottom()
# (or equivalent) instead of the API-30+ typed query.
if [ -n "$FALLBACK_BLOCK" ] && printf '%s\n' "$FALLBACK_BLOCK" | grep -q 'getSystemWindowInsetBottom()'; then
  pass "pre-30 fallback uses getSystemWindowInsetBottom() compat shape"
else
  fail "pre-30 fallback uses getSystemWindowInsetBottom() compat shape"
fi

# T3-POLL-1 (regression, bug-runtime-004 t5r3 device 127.0.0.1:6569 API 29,
# evidence t5r3_logcat_full.txt + t5r3_dumpsys_ime_shown.txt): keyboard shows
# but onInsetsFrame never delivers nonzero heights — the SDK<30 fallback reads
# getSystemWindowInsetBottom() once under ADJUST_PAN (always 0) with no
# per-frame sampler. The fallback must register a poll-based
# visible-display-frame sampler (ViewTreeObserver OnGlobalLayoutListener or
# equivalent: addOnGlobalLayoutListener / OnLayoutChangeListener /
# getViewTreeObserver). Scanned over the full fallback region (else-branch to
# method end) so a multi-line sampler is not cut off by the 12-line API29
# window above. FAILS until the sampler lands.
FALLBACK_FULL=""
if [ -n "${ELSE_LINE:-}" ]; then
  FALLBACK_FULL=$(awk -v start="$ELSE_LINE" 'NR>=start{print} NR>start && /^    \}/{exit}' "$JAVA_FILE")
fi
if [ -n "$FALLBACK_FULL" ] && printf '%s\n' "$FALLBACK_FULL" | grep -qE 'OnGlobalLayoutListener|addOnGlobalLayoutListener|OnLayoutChangeListener|addOnLayoutChangeListener|getViewTreeObserver'; then
  pass "pre-30 fallback registers a poll-based layout sampler"
else
  fail "pre-30 fallback registers a poll-based layout sampler"
fi

# T3-POLL-2 (regression, bug-runtime-004 t5r3): the sampler must compute a
# keyboard height from the visible display frame (getWindowVisibleDisplayFrame
# / root-view height delta) and forward it through the SAME onInsetsFrame
# entry (same channel, no parallel setter — single-feed denial stays: exactly
# one onInsetsFrame definition per T3-SINGLE-1). Both signals must appear in
# the fallback region; FAILS until the sampler lands.
if [ -n "$FALLBACK_FULL" ] \
  && printf '%s\n' "$FALLBACK_FULL" | grep -q 'getWindowVisibleDisplayFrame' \
  && printf '%s\n' "$FALLBACK_FULL" | grep -q 'onInsetsFrame'; then
  pass "pre-30 sampler forwards visible-display-frame height via onInsetsFrame"
else
  fail "pre-30 sampler forwards visible-display-frame height via onInsetsFrame"
fi

# T3-RESIZE-1 (regression, bug-runtime-004 t5r4 device 127.0.0.1:6569,
# evidence t5r4_logcat_full.txt): under ADJUST_PAN no layout pass runs
# mid-transition — only 2 discrete inset states (66.0/474.0), single 408px
# jump, violating L9's max-1px-steps clause. The gallery window must opt
# into SOFT_INPUT_ADJUST_RESIZE (getWindow().setSoftInputMode(...)) so the
# IME transition runs real layout passes and the sampler emits true
# per-frame intermediate values. FAILS until the fix lands.
if grep -q 'setSoftInputMode' "$JAVA_FILE" \
  && grep -q 'SOFT_INPUT_ADJUST_RESIZE' "$JAVA_FILE"; then
  pass "gallery window opts into SOFT_INPUT_ADJUST_RESIZE"
else
  fail "gallery window opts into SOFT_INPUT_ADJUST_RESIZE"
fi

# T3-RESIZE-2 (denial, bug-runtime-004 t5r4): the window must NOT opt into
# ADJUST_PAN / ADJUST_NOTHING (either reintroduces the no-layout-pass jump).
# Passes on current code (no soft-input mode set yet); guards the fix.
if grep -qE 'SOFT_INPUT_ADJUST_PAN|SOFT_INPUT_ADJUST_NOTHING' "$JAVA_FILE"; then
  fail "gallery window avoids ADJUST_PAN / ADJUST_NOTHING"
else
  pass "gallery window avoids ADJUST_PAN / ADJUST_NOTHING"
fi

# F1-PACE-1 (bug-runtime-004 F1, review MAJOR 1): the T4 call site runs only
# inside PresentDeviceGalleryPage, so bus writes without a present are silent
# (12/12 lines inset=0.0 on API 35, 2 steady states on API 29 despite running
# animations). The single onInsetsFrame feed must therefore request exactly
# one layering present per forwarded frame — either a direct
# nativeRenderFrame(engineHandle) call in the onInsetsFrame body, or a
# Choreographer frame callback (postFrameCallback) that forwards then renders.
# Scoped so the pre-existing surfaceCreated/surfaceChanged nativeRenderFrame
# calls do not satisfy it. FAILS until the pacing lands.
ONINSETS_BLOCK=$(awk '/void onInsetsFrame\(/{flag=1} flag{print} flag && /^    \}/{exit}' "$JAVA_FILE")
# Pass iff the onInsetsFrame body renders directly, or the file paces the
# feed via a Choreographer frame callback.
if printf '%s\n' "$ONINSETS_BLOCK" | grep -qE 'nativeRenderFrame|requestRender'; then
  pass "inset feed paces one layering present per frame (render/request path)"
elif grep -q 'Choreographer' "$JAVA_FILE" && grep -q 'postFrameCallback' "$JAVA_FILE"; then
  pass "inset feed paces one layering present per frame (render/request path)"
else
  fail "inset feed paces one layering present per frame (render/request path)"
fi

# F1-PACE-2 (bug-runtime-004 F1): the paced present is throttled — one
# present per animation frame, no-op when the value is unchanged. Satisfied
# either by a last-value/changed guard in the onInsetsFrame body
# (last|mLast|previous|unchanged — deliberately NOT bare `!=`, which the
# existing engineHandle null-guard already contains) or by Choreographer
# frame pacing (one callback per vsync by construction). FAILS until the
# throttle lands.
if printf '%s\n' "$ONINSETS_BLOCK" | grep -qE '[lL]ast|previous|Previous|unchanged'; then
  pass "paced present throttled (changed-guard or frame pacing)"
elif grep -q 'Choreographer' "$JAVA_FILE" && grep -q 'postFrameCallback' "$JAVA_FILE"; then
  pass "paced present throttled (changed-guard or frame pacing)"
else
  fail "paced present throttled (changed-guard or frame pacing)"
fi

# F2-SAMPLER-1 (bug-runtime-004 F2, review MAJOR 2): on API 30+ the code
# trusts WindowInsetsAnimation.Callback.onProgress exclusively, but t5r6
# proves onProgress never delivers nonzero heights on 127.0.0.1:6562 (bus
# pinned 0.0 across 3 PIDs / 3 show cycles, shown-steady also 0.0). The 30+
# branch of attachImeInsetListener must therefore register the API-29 sampler
# shape as a fallback (OnGlobalLayoutListener / OnLayoutChangeListener /
# getViewTreeObserver / getWindowVisibleDisplayFrame / OnApplyWindowInsets
# read). Scoped to the >=30 branch (up to the else) so the landed <30
# sampler does not satisfy it. FAILS until the fallback lands.
GE30_LINE=$(awk '/attachImeInsetListener\(View root\)/{seen=1} seen && /SDK_INT >= 30/{print NR; exit}' "$JAVA_FILE" || true)
BRANCH30=""
if [ -n "${GE30_LINE:-}" ] && [ -n "${ELSE_LINE:-}" ]; then
  BRANCH30=$(sed -n "${GE30_LINE},${ELSE_LINE}p" "$JAVA_FILE")
fi
if [ -n "$BRANCH30" ] && printf '%s\n' "$BRANCH30" | grep -qE 'OnGlobalLayoutListener|addOnGlobalLayoutListener|OnLayoutChangeListener|addOnLayoutChangeListener|getViewTreeObserver|getWindowVisibleDisplayFrame|OnApplyWindowInsetsListener|setOnApplyWindowInsetsListener'; then
  pass "30+ branch registers a fallback sampler"
else
  fail "30+ branch registers a fallback sampler"
fi

# F2-SAMPLER-2 (bug-runtime-004 F2 guard): the 30+ fallback sampler must feed
# the SAME single onInsetsFrame entry — no second setter, no parallel bus.
# Passes on current code (onProgress already forwards via onInsetsFrame);
# guards the fix against a parallel feed (T3-SINGLE-1 stays the denial).
if [ -n "$BRANCH30" ] && printf '%s\n' "$BRANCH30" | grep -q 'onInsetsFrame'; then
  pass "30+ fallback forwards via onInsetsFrame (no parallel feed)"
else
  fail "30+ fallback forwards via onInsetsFrame (no parallel feed)"
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
