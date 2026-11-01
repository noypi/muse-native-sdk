#!/bin/bash
# Per-probe LIVE device proof harness — fr-graphics-015 (FR-GRAPHICS-015, TC-GRAPHICS-045).
# Target device ALWAYS explicit: adb -s 127.0.0.1:6569 (never the default device).
# Run from the _run checkout: examples/m3-gallery/build/device-proof/probe-015.sh
# Artefacts (screencaps .png + .md5 + log excerpts .log) land in this dir (device-proof/),
# EXCLUDING build/goldens/ (never touched). ANDROID_SERIAL is exported for Make flows.
S=127.0.0.1:6569
export ANDROID_SERIAL=$S
D="$(cd "$(dirname "$0")" && pwd)"
shot() { # $1=name -> screencap, pull, md5
  adb -s $S shell screencap -p /sdcard/$1.png >/dev/null
  adb -s $S pull /sdcard/$1.png $D/$1.png >/dev/null 2>&1
  md5sum $D/$1.png | tee $D/$1.md5
}
logmark() { # $1=label $2=grep-pattern -> save matching logcat lines
  adb -s $S logcat -d | grep -E "$2" > $D/$1.log 2>&1; true
  echo "--- $1: $(wc -l < $D/$1.log) lines"
}
echo "== P1 tap dispatch =="
adb -s $S logcat -c
adb -s $S shell am start -n com.muse.host/.MainActivity >/dev/null 2>&1; sleep 3
shot dp_p1_base
adb -s $S shell input tap 840 120; sleep 1
shot dp_p1_next
adb -s $S shell input tap 200 300; sleep 0.8
shot dp_p1_claimed
logmark dp_p1_touch "MainActivity.*touch|SEAM-STEP:touch"
echo "== P2 state-layer wash (long-press on claimed node) =="
adb -s $S logcat -c
adb -s $S shell "input swipe 200 300 200 300 1200 &" >/dev/null 2>&1 &
sleep 0.5
shot dp_p2_wash_hold
sleep 1.2
shot dp_p2_wash_release
logmark dp_p2_touch "MainActivity.*touch|SEAM-STEP:touch"
echo "== P3 theme repaint (same-activity NATIVE_SET_THEME) =="
adb -s $S logcat -c
adb -s $S shell am broadcast -a muse.native.NATIVE_SET_THEME --ei night_mode 1 >/dev/null 2>&1; sleep 1
shot dp_p3_dark
adb -s $S shell am broadcast -a muse.native.NATIVE_SET_THEME --ei night_mode 0 >/dev/null 2>&1; sleep 1
shot dp_p3_light
logmark dp_p3_theme "SEAM-STEP:theme|nativeSetTheme|GalleryPage"
echo "== P5 layering overlay order (page to Dialog-open story, scrim-tap dismiss) =="
adb -s $S shell am force-stop com.muse.host >/dev/null 2>&1; sleep 1
adb -s $S shell am start -n com.muse.host/.MainActivity >/dev/null 2>&1; sleep 3
adb -s $S logcat -c
for i in $(seq 1 203); do adb -s $S shell am broadcast -a muse.native.PROOF_ADVANCE_PAGE >/dev/null 2>&1; sleep 0.45; done
sleep 1
shot dp_p5_overlay
logmark dp_p5_pages "PROOF_ADVANCE_PAGE page"
adb -s $S shell input tap 100 300; sleep 1
shot dp_p5_scrim_tap
logmark dp_p5_scrim "MainActivity.*touch|Dismiss|dismiss|scrim|Scrim"
echo "== P4 IME-inset sheet anchoring (claimed tap -> probe focus -> keyboard) =="
adb -s $S logcat -c
adb -s $S shell input tap 200 300; sleep 1.5
shot dp_p4_kb
adb -s $S shell "dumpsys input_method" | grep -iE "mInputShown|mIsInputMethodShowing|mCurrentFocus|mFocusedApp" > $D/dp_p4_ime.log 2>&1; true
cat $D/dp_p4_ime.log
logmark dp_p4_ime2 "ime|Ime|IME|keyboard|Keyboard|muse-host.*SEAM-STEP:ime"
echo "== stability scan =="
adb -s $S logcat -d | grep -iE "FATAL|AndroidRuntime.*E |muse.*(error|exception)|present.*fail|surface.*fail" | head -20 > $D/dp_stability.log 2>&1; true
echo "stability hits: $(wc -l < $D/dp_stability.log)"
adb -s $S shell pidof com.muse.host
echo DONE
