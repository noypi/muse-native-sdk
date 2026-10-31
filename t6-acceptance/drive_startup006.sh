#!/bin/bash
# TC-STARTUP-006 device acceptance drive for FR-STARTUP-003 (tester-owned).
# App (com.muse.host, m3-gallery) must already be installed; this script
# never installs or rebuilds — it only drives input, rotates, backgrounds,
# and reads logcat/screenshots.
#
# Covers AC-STARTUP-006: forced back-pressure (rapid frames via touch-UP
# nativeRenderFrame) + teardown (rotate/background/resume mid-present)
# keeps the last good frame (no white/tear) and logcat keeps showing live
# `gallery-rows=N gallery-pages=M` + `gallery-page=i/M` from the row model.
#
# Usage (run from the _run checkout root):
#   SERIAL=KDSBB22429250346 bash t6-acceptance/drive_startup006.sh
# Exit 0 only when every automatable device check holds; prints a per-check
# PASS/FAIL table either way for the test report. On-screen no-white/no-tear
# judgment stays human: screenshots land in $OUT for the reviewer to eyeball.
set -u
SERIAL="${SERIAL:-KDSBB22429250346}"
OUT="${OUT:-/tmp/startup006-evidence}"
mkdir -p "$OUT"
FAIL=0
check() { # check <name> <status> <detail>
  printf '%-42s %-12s %s\n' "$1" "$2" "$3"
  [ "$2" = "FAIL" ] && FAIL=1
}
adb_() { adb -s "$SERIAL" "$@" 2>&1; }

echo "== step 0: device + app present =="
adb_ get-state | grep -q device && S0=PASS || S0=FAIL
check "device-online" "$S0" "$SERIAL"
adb_ shell pm list packages 2>/dev/null | grep -q com.muse.host && S0b=PASS || S0b=FAIL
check "app-installed" "$S0b" "com.muse.host"

echo "== step 1: foreground + clear logcat =="
adb_ shell am start -n com.muse.host/.MainActivity >/dev/null 2>&1
sleep 2
adb_ logcat -c >/dev/null 2>&1
check "foreground" "PASS" "am start sent, logcat cleared"

echo "== step 2: back-pressure — 20 rapid taps (touch-UP nativeRenderFrame) =="
for i in $(seq 1 20); do adb_ shell input tap 360 800 >/dev/null 2>&1; done
sleep 3
MARKERS="$OUT/markers_backpressure.txt"
adb_ logcat -d 2>/dev/null | grep "gallery-rows\|gallery-page" | tail -8 > "$MARKERS"
N_ROWS=$(grep -c "gallery-rows=" "$MARKERS" || true)
[ "$N_ROWS" -ge 1 ] && S2=PASS || S2=FAIL
check "backpressure-live-markers" "$S2" "gallery marker lines=$N_ROWS (want >=1), see $MARKERS"
CRASH1=$(adb_ logcat -d 2>/dev/null | grep -c "FATAL EXCEPTION" || true)
[ "$CRASH1" = "0" ] && S2b=PASS || S2b=FAIL
check "backpressure-no-crash" "$S2b" "FATAL lines=$CRASH1 (want 0)"

echo "== step 3: teardown — rotate mid-present + taps + home/resume =="
adb_ shell screencap -p /sdcard/pre_teardown.png >/dev/null 2>&1
adb_ shell "content insert --uri content://settings/system --bind name:s:accelerometer_rotation --bind value:i:0" >/dev/null 2>&1
adb_ shell "content insert --uri content://settings/system --bind name:s:user_rotation --bind value:i:1" >/dev/null 2>&1
sleep 2
for i in 1 2 3; do adb_ shell input tap 360 800 >/dev/null 2>&1; done
sleep 2
adb_ shell "content insert --uri content://settings/system --bind name:s:user_rotation --bind value:i:0" >/dev/null 2>&1
sleep 1
adb_ shell input keyevent KEYCODE_HOME >/dev/null 2>&1
sleep 2
adb_ shell am start -n com.muse.host/.MainActivity >/dev/null 2>&1
sleep 3
adb_ shell screencap -p /sdcard/post_teardown.png >/dev/null 2>&1
adb_ pull /sdcard/pre_teardown.png "$OUT/pre_teardown.png" >/dev/null 2>&1
adb_ pull /sdcard/post_teardown.png "$OUT/post_teardown.png" >/dev/null 2>&1
MARKERS2="$OUT/markers_teardown.txt"
adb_ logcat -d 2>/dev/null | grep "gallery-rows\|gallery-page\|surfaceDestroyed\|window lock failed" | tail -10 > "$MARKERS2"
N_ROWS2=$(grep -c "gallery-rows=" "$MARKERS2" || true)
[ "$N_ROWS2" -ge 1 ] && S3=PASS || S3=FAIL
check "teardown-live-markers" "$S3" "gallery marker lines=$N_ROWS2 (want >=1), see $MARKERS2"
CRASH2=$(adb_ logcat -d 2>/dev/null | grep -c "FATAL EXCEPTION" || true)
[ "$CRASH2" = "0" ] && S3b=PASS || S3b=FAIL
check "teardown-no-crash" "$S3b" "FATAL lines=$CRASH2 (want 0)"
if [ -s "$OUT/post_teardown.png" ]; then
  STATS=$(python3 -c "
from PIL import Image
im = Image.open('$OUT/post_teardown.png').convert('RGB')
w,h = im.size
px = list(im.getdata()); n = len(px)
white = sum(1 for p in px if p[0]>245 and p[1]>245 and p[2]>245)/n
print('white-frac=%.3f distinct=%d' % (white, len(set(list(im.resize((180,400)).getdata())))))
" 2>&1)
  echo "   post-teardown screenshot: $STATS"
  S3c=PASS
else
  S3c=FAIL
fi
check "teardown-screenshot-kept" "$S3c" "$OUT/post_teardown.png (eyeball for white/tear)"

echo "== result: $([ $FAIL = 0 ] && echo ALL-PASS || echo FAILURES-PRESENT) =="
exit $FAIL
