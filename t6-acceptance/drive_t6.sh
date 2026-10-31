#!/bin/bash
# T6 device-lab acceptance drive for BUG-SAMPLE-003 (tester-owned, device-executed).
# Serial: 127.0.0.1:6569 (API 29 per FR-SAMPLE-014 support). App must already be
# installed; this script never installs (no engine SDK / MUSE_NATIVE_HOME here,
# and the scaffold app-fat.apk carries placeholder .so's — installing it would
# brick the working host).
#
# Covers TC-SAMPLE-039 (AC-SAMPLE-039 shell paging), TC-SAMPLE-040 (AC-SAMPLE-040
# in-place text interaction), and the on-device half of TC-SAMPLE-041
# (AC-SAMPLE-041 no static Text-label duplicates, default generated mode).
# Source/tree-level AC-041 is covered by test/showcase-no-static-labels.test.ts
# (12/12); this script is the live-device complement.
#
# Usage (run from the _run checkout root):
#   SERIAL=127.0.0.1:6569 bash t6-acceptance/drive_t6.sh
# Exit 0 only when every automatable device check holds; prints a per-check
# PASS/FAIL/UNVERIFIED table either way for the test report.
set -u
SERIAL="${SERIAL:-127.0.0.1:6569}"
OUT="${OUT:-/tmp/t6-evidence}"
mkdir -p "$OUT"
FAIL=0
check() { # check <name> <status> <detail>
  printf '%-42s %-12s %s\n' "$2" "$3" "$4"
  [ "$3" = "FAIL" ] && FAIL=1
}

echo "== T6 step 0: freshness gates (source + baked tree asset) =="
SRC_ROWS=$(grep -c GalleryRow examples/m3-gallery/src/Showcase.tsx || true)
[ "$SRC_ROWS" = "0" ] && S0=PASS || S0=FAIL
check x "src-Showcase-zero-GalleryRow" "$S0" "grep count=$SRC_ROWS (want 0)"
ASSET="examples/m3-gallery/dist/tree.mui.bin"
if [ ! -s "$ASSET" ]; then
  S0b=FAIL
  check x "dist-tree-zero-GalleryRow" "$S0b" "$ASSET missing or empty (want existing non-empty asset with 0 GalleryRow)"
else
  ASSET_ROWS=$(strings "$ASSET" 2>/dev/null | grep -c GalleryRow || true)
  [ "$ASSET_ROWS" = "0" ] && S0b=PASS || S0b=FAIL
  check x "dist-tree-zero-GalleryRow" "$S0b" "strings count=$ASSET_ROWS (want 0)"
fi

echo "== T6 step 1: device info =="
adb -s "$SERIAL" shell "getprop ro.build.version.sdk; getprop ro.product.model; getprop ro.product.cpu.abi" | tee "$OUT/t6-device.txt"

echo "== T6 step 2: launch + page inventory =="
adb -s "$SERIAL" logcat -c
adb -s "$SERIAL" shell monkey -p com.muse.host -c android.intent.category.LAUNCHER 1 >/dev/null 2>&1
sleep 4
adb -s "$SERIAL" shell logcat -d 2>/dev/null | grep -E "GalleryPage" | tail -4 | tee "$OUT/t6-launch.log"
adb -s "$SERIAL" shell uiautomator dump /sdcard/uidump-t6.xml >/dev/null 2>&1
adb -s "$SERIAL" pull /sdcard/uidump-t6.xml "$OUT/uidump-t6.xml" >/dev/null 2>&1
if grep -o -e 'Prev' -e 'Next' -e 'story index' -e '[0-9]*/22' "$OUT/uidump-t6.xml" | sort | uniq -c | tee "$OUT/t6-chrome.txt" | grep -q .; then
  S2=PASS
else
  S2=FAIL
fi
check x "shell-Prev-Next-index-present" "$S2" "see $OUT/t6-chrome.txt (AC-039 needs buttons+index)"

echo "== T6 step 3: outside-tap paging probe (AC-039 forbids surface paging) =="
adb -s "$SERIAL" logcat -c
adb -s "$SERIAL" shell input tap 285 1000
sleep 2
TAPLOG=$(adb -s "$SERIAL" shell logcat -d 2>/dev/null | grep -E "gallery-page=|slice page" | tail -2 | tee "$OUT/t6-tap.log")
echo "$TAPLOG"
if echo "$TAPLOG" | grep -q "gallery-page="; then S3=FAIL; else S3=PASS; fi
check x "surface-tap-never-paginates" "$S3" "tap advanced page log above = paging from surface (want none)"

echo "== T6 step 4: arrow-key parity probe =="
adb -s "$SERIAL" logcat -c
adb -s "$SERIAL" shell "input keyevent 22; sleep 1; input keyevent 21"
sleep 1
if adb -s "$SERIAL" shell logcat -d 2>/dev/null | grep -q "gallery-page="; then S4=PASS; else S4=FAIL; fi
check x "arrow-keys-page" "$S4" "DPAD right+left must move story (AC-039)"

echo "== T6 step 5: deep-link probe =="
if adb -s "$SERIAL" shell am start -a android.intent.action.VIEW -d 'gallery://story/Button' 2>&1 | tee "$OUT/t6-deeplink.txt" | grep -q "unable to resolve"; then S5=FAIL; else S5=PASS; fi
check x "gallery-deep-link-resolves" "$S5" "see $OUT/t6-deeplink.txt (AC-039)"

echo "== T6 step 6: text-story page (AC-040) + static-row scan (AC-041) =="
adb -s "$SERIAL" logcat -c
for _ in $(seq 1 14); do
  adb -s "$SERIAL" shell input tap 285 1000; sleep 1.2
done
adb -s "$SERIAL" shell logcat -d 2>/dev/null | grep -E "slice page" | tee "$OUT/t6-pages.log" | tail -6
if grep -q "GalleryRow" "$OUT/t6-pages.log"; then S6=FAIL; else S6=PASS; fi
check x "device-pages-no-GalleryRow" "$S6" "stale host tree still serves static rows (AC-041)"
adb -s "$SERIAL" exec-out screencap -p > "$OUT/t6-text-page.png"
echo "screenshot -> $OUT/t6-text-page.png (human: confirm focus/caret, no Text-label dupes; note API-29 glyph tofu)"
check x "text-focus-caret-visual" "UNVERIFIED" "manual read of $OUT/t6-text-page.png required"
if adb -s "$SERIAL" shell logcat -d 2>/dev/null | grep -qi "static-fallback\|StaticFallback"; then S6b=FAIL; else S6b=PASS; fi
check x "no-fallback-report-default" "$S6b" "default mode must not report static fallback (AC-041/032)"

echo "== T6 verdict: $([ $FAIL = 0 ] && echo PASS || echo FAIL) (evidence -> $OUT) =="
exit $FAIL
