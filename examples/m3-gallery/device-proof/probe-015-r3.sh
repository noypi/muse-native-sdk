#!/bin/bash
# Round 3: on each of the 9 gallery pages, tap scrim-candidate (150,300) and shoot.
S=127.0.0.1:6569
export ANDROID_SERIAL=$S
D="$(cd "$(dirname "$0")" && pwd)"
adb -s $S logcat -c
for i in $(seq 1 9); do
  adb -s $S shell am broadcast -a muse.native.PROOF_ADVANCE_PAGE >/dev/null 2>&1; sleep 0.8
  adb -s $S shell input tap 150 300; sleep 0.8
  adb -s $S shell screencap -p /sdcard/dp_r3_pg$i.png >/dev/null
  adb -s $S pull /sdcard/dp_r3_pg$i.png $D/dp_r3_pg$i.png >/dev/null 2>&1
  md5sum $D/dp_r3_pg$i.png | tee $D/dp_r3_pg$i.md5
done
adb -s $S logcat -d | grep -E "PROOF_ADVANCE_PAGE page|gallery-page=|touch x=15|ismiss" > $D/dp_r3.log 2>&1; true
cat $D/dp_r3.log
echo DONE
