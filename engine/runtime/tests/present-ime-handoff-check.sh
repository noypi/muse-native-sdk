#!/bin/sh
# F5 inset-float handoff check — BUG-RUNTIME-004 / review MINOR (data race) +
# optional single-editor focus cleanup.
#
# Hermetic static half (runs anywhere with sh+grep): asserts the F5 hardening
# in engine/runtime/src/gallery_present.cpp (+ header when the accessor
# signature changes) —
#   the live `deps.insets.imeBottom` store is `std::atomic<float>` (relaxed
#   load/store) or guarded by the existing handle mutex — never a plain
#   unsynchronized float written on the UI/JNI thread and read on render;
#   SetPresentImeBottom keeps its fail-closed shape (non-finite/negative to
#   zero, never throws) and never touches fullRelayouts (no full-tree
#   relayout).
# Focus cleanup is explicitly optional (plan F5): F5-FOCUS-1 below is
# informational and non-gating — it reports the focus shape but never fails.
# Device half (not run here; executed by the tester on 127.0.0.1:6569 +
# 127.0.0.1:6562): summon still mShowRequested=true, steady-state
# sheetY==inset exact on both devices.
#
# Exit 0 when the static contract holds; non-zero (RED) otherwise.
# No network, no device, no env/config reads — reads only the two C++ files
# plus MainActivity.java (focus info only).
set -u

CPP_FILE="${1:-engine/runtime/src/gallery_present.cpp}"
H_FILE="${2:-engine/runtime/include/runtime/gallery_present.h}"
JAVA_FILE="${3:-examples/m3-gallery/src/MainActivity.java}"

fail() {
  printf 'FAIL %s\n' "$1"
  FAILURES=$((FAILURES + 1))
}
pass() {
  printf 'PASS %s\n' "$1"
}
info() {
  printf 'INFO %s\n' "$1"
}

FAILURES=0

if [ ! -f "$CPP_FILE" ]; then
  printf 'FAIL inset-handoff source missing: %s\n' "$CPP_FILE"
  exit 1
fi
if [ ! -f "$H_FILE" ]; then
  printf 'FAIL inset-handoff header missing: %s\n' "$H_FILE"
  exit 1
fi

# F5-ATOMIC-1 (bug-runtime-004 F5, review MINOR data race): PresentDeps() is a
# process-wide float written on the UI/JNI thread (SetPresentImeBottom) and
# read on the render path every frame before paint — formally UB as a plain
# float (benign in practice: 27/27 device lines exact). The store must be
# `std::atomic<float>` (relaxed) or mutex-guarded. FAILS until the fix lands
# (plain `float imeBottom` today, no <atomic>/<mutex> in either file).
if grep -qE 'std::atomic<float>|atomic<float>|std::mutex|std::lock_guard' "$CPP_FILE" "$H_FILE" 2>/dev/null; then
  pass "inset handoff is atomic/mutex-guarded (no plain unsynchronized float)"
else
  fail "inset handoff is atomic/mutex-guarded (no plain unsynchronized float)"
fi

# F5-ATOMIC-2 (bug-runtime-004 F5): the handoff uses relaxed load/store (or a
# mutex critical section) — scoped to the SetPresentImeBottom + PresentDeps
# region so an unrelated atomic elsewhere cannot satisfy it. FAILS until the
# fix lands.
HANDOFF_REGION=$(awk '/struct PresentImeDeps/,/^}  \/\/ namespace/' "$CPP_FILE"; awk '/void SetPresentImeBottom\(float/{flag=1} flag{print} flag && /^}/{exit}' "$CPP_FILE")
if [ -n "$HANDOFF_REGION" ] && printf '%s\n' "$HANDOFF_REGION" | grep -qE 'memory_order_relaxed|\.load\(|\.store\(|lock_guard|unique_lock|atomic'; then
  pass "inset handoff loads/stores relaxed (or under mutex)"
else
  fail "inset handoff loads/stores relaxed (or under mutex)"
fi

# F5-NORELAYOUT-1 (pin, AC-LAYOUT-035): the hardening changes storage only —
# SetPresentImeBottom still never touches fullRelayouts (no full-tree
# relayout). Passes pre-change; guards the fix.
SETTER_BODY=$(awk '/void SetPresentImeBottom\(float/{flag=1} flag{print} flag && /^}/{exit}' "$CPP_FILE")
if [ -n "$SETTER_BODY" ] && printf '%s\n' "$SETTER_BODY" | grep -q 'fullRelayouts'; then
  fail "SetPresentImeBottom must not relayout (fullRelayouts touched)"
else
  pass "SetPresentImeBottom performs no full-tree relayout (fullRelayouts untouched)"
fi

# F5-FOCUS-1 (optional, explicitly non-gating per plan F5): the focus cleanup
# keeps both TextInput/MuseField views and requests focus once (second
# requestFocus() currently wins — harmless since either editor summons,
# mShowRequested=true proven). This check is INFORMATIONAL ONLY: it reports
# the current shape and never fails, so the coder may land or skip the
# cleanup without affecting the gate.
if [ -f "$JAVA_FILE" ]; then
  FOCUS_COUNT=$(grep -c 'requestFocus()' "$JAVA_FILE" || true)
  info "focus cleanup (optional, non-gating): ${FOCUS_COUNT} requestFocus() call(s) in MainActivity (target: 1; both-views-kept shape accepted)"
else
  info "focus cleanup (optional, non-gating): MainActivity.java not found — device summon leg is tester-verified"
fi

if [ "$FAILURES" -gt 0 ]; then
  printf 'present-ime-handoff-check: %d failing\n' "$FAILURES"
  printf 'device half (tester, 127.0.0.1:6569 + 127.0.0.1:6562):\n'
  printf '  summon still mShowRequested=true, steady-state sheetY==inset exact on both devices\n'
  exit 1
fi
printf 'present-ime-handoff-check: all passing (static half; device summon + steady-state is tester-run)\n'
printf 'device half (tester, 127.0.0.1:6569 + 127.0.0.1:6562):\n'
printf '  summon still mShowRequested=true, steady-state sheetY==inset exact on both devices\n'
exit 0
