#!/bin/bash
# T10 rich-clipboard round-trip acceptance drive (tester-owned, device-executed).
# Serial: KDSBB22429250346. App must already be installed; this script never
# installs (freshness is verified via uiautomator probe markers before running).
# Asserts are made by a human/tester reading the RECEIVED `muse_text` events,
# never by shell exit codes alone.
#
# Usage (run from the _run checkout root):
#   SERIAL=KDSBB22429250346 bash t10-acceptance/drive_t10.sh
set -u
SERIAL="${SERIAL:-KDSBB22429250346}"
OUT="${OUT:-$HOME/t10-evidence}"
mkdir -p "$OUT"

echo "== T10 step 0: probe markers (freshness gate, no reinstall) =="
adb -s "$SERIAL" shell uiautomator dump /sdcard/uidump-t10.xml
adb -s "$SERIAL" pull /sdcard/uidump-t10.xml "$OUT/uidump-t10.xml"
grep -o -e 'Stage rich clip' -e 'Probe live field' "$OUT/uidump-t10.xml" | sort | uniq -c

echo "== T10 step 1: clear logcat, stage rich clip =="
adb -s "$SERIAL" logcat -c
adb -s "$SERIAL" shell input tap 360 334
sleep 2
adb -s "$SERIAL" exec-out screencap -p > "$OUT/t10-staged.png"
echo "expect: clip_changed with mime=text/html (see log below)"

echo "== T10 step 2/3: long-press field, PASTE =="
adb -s "$SERIAL" shell input swipe 360 240 360 240 1200
sleep 2
adb -s "$SERIAL" exec-out screencap -p > "$OUT/t10-paste-menu.png"
adb -s "$SERIAL" shell input tap 105 167
sleep 2
adb -s "$SERIAL" exec-out screencap -p > "$OUT/t10-pasted.png"
echo "expect: text_updated containing rich, bound==visible"

echo "== T10 evidence: muse_text window =="
adb -s "$SERIAL" logcat -d -s muse_text | tee "$OUT/t10-muse-text.log"
echo "done -> $OUT"
