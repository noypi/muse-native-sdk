#!/bin/bash
# Round 2: reach layering overlay page, scrim dismiss, IME retry, wash retry.
S=127.0.0.1:6569
export ANDROID_SERIAL=$S
D="$(cd "$(dirname "$0")" && pwd)"
shot() {
  adb -s $S shell screencap -p /sdcard/$1.png >/dev/null
  adb -s $S pull /sdcard/$1.png $D/$1.png >/dev/null 2>&1
  md5sum $D/$1.png | tee $D/$1.md5
}
echo "== IME settings =="
adb -s $S shell settings get secure show_ime_with_hard_keyboard
adb -s $S shell ime list -a 2>/dev/null | grep -E "mId|mSettingsActivityName" | head -6
adb -s $S shell settings put secure show_ime_with_hard_keyboard 1
echo "== walk 9 pages from current =="
adb -s $S logcat -c
for i in $(seq 1 9); do
  adb -s $S shell am broadcast -a muse.native.PROOF_ADVANCE_PAGE >/dev/null 2>&1; sleep 0.8
  shot dp_r2_pg$i
done
adb -s $S logcat -d | grep -E "PROOF_ADVANCE_PAGE page|gallery-page=" | tail -20 > $D/dp_r2_walk.log 2>&1; true
cat $D/dp_r2_walk.log
echo "== wash retry: short-hold + immediate shots =="
adb -s $S logcat -c
adb -s $S shell "input swipe 200 300 200 300 250"; shot dp_r2_wash1
adb -s $S shell "input swipe 200 300 200 300 250"; shot dp_r2_wash2
adb -s $S logcat -d | grep -E "MainActivity.*touch" | grep -cE "claimed=20"
echo "== IME retry =="
adb -s $S shell input tap 200 300; sleep 2
shot dp_r2_kb
adb -s $S shell "dumpsys input_method" | grep -iE "mInputShown|mIsInputMethodShowing" | head -3
adb -s $S logcat -d | grep -E "ImeTracker|showSoftInput|SEAM-STEP:ime" | head -6
echo DONE
