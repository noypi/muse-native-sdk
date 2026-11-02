#!/bin/sh
# T9 acceptance check (tester-owned, NOT a unit test).
# Re-asserts the T9 event-stream properties against the saved
# evidence/T8/ captures from the live-device drive on KDSBB22429250346.
# Usage (from the workspace run dir):
#   sh <this-file> workflow.local/runs/bug-m3-fidelity-003/evidence/T8
# Exit 0 only if every assertion below holds; prints PASS/FAIL per check.
EV="$1"
fail=0
check() { # $1=name $2=status(0=pass)
  if [ "$2" -eq 0 ]; then echo "PASS $1"; else echo "FAIL $1"; fail=1; fi
}
ids() { grep -o '"eventId":[0-9]*,"rx":[0-9]*' "$1" | grep -o '[0-9]*$'; }
continuous() { # $1=file: eventIds gap-free and rx==eventId
  prev=""
  while read -r line; do
    id=$(printf '%s' "$line" | grep -o '"eventId":[0-9]*' | grep -o '[0-9]*$')
    rx=$(printf '%s' "$line" | grep -o '"rx":[0-9]*' | grep -o '[0-9]*$')
    [ "$id" = "$rx" ] || return 1
    if [ -n "$prev" ]; then
      [ "$id" -eq $((prev + 1)) ] || return 1
    fi
    prev="$id"
  done < "$1"
  [ -n "$prev" ]
}
f="$EV/t9-composition-logcat.txt"
continuous "$f"; check "composition window continuous eventIds (74..91)" $?
grep -q '"type":"commit_text","text":"j"' "$f"; check "commit per-keystroke text+j cursor=1" $?
grep -q '"type":"commit_text","text":" ","cursor":6}' "$f"; check "space commit cursor=6" $?
f="$EV/t9-suggestion-logcat.txt"
continuous "$f"; check "suggestion window continuous eventIds (116..137)" $?
grep -q '"type":"finish_composing","text":"","cursor":5}' "$f"; check "suggestion-tap finish_composing cursor=5" $?
grep -q '"type":"commit_text","text":"Rihanna","cursor":7}' "$f"; check "suggestion commit Rihanna cursor=7" $?
f="$EV/t9-caret-logcat.txt"
continuous "$f"; check "caret window continuous eventIds (138..148)" $?
grep -q '"type":"set_selection","text":"","cursor":3}' "$f"; check "touch-caret left cursor=3" $?
grep -q '"type":"set_selection","text":"","cursor":7}' "$f"; check "touch-caret right cursor=7" $?
grep -q '"type":"text_updated","text":"Rihanhna"' "$f"; check "mid-string mutate Rihanhna" $?
grep -q '"type":"commit_text","text":"h","cursor":6}' "$f"; check "mutate caret preserved cursor=6" $?
f="$EV/t9-clip-logcat.txt"
continuous "$f"; check "clipboard window continuous eventIds (149..161)" $?
grep -q '"type":"clip_changed","text":"Rihanhna","cursor":8,"mime":"text/plain"}' "$f"; check "copy clip_changed content+MIME" $?
grep -q '"type":"text_updated","text":"RihanhnaRihanhna"' "$f"; check "paste round-trip text doubled" $?
grep -q '"type":"set_selection","text":"","cursor":16}' "$f"; check "paste caret cursor=16" $?
grep -q '"type":"clip_changed","text":"RihanhnaRihanhna","cursor":0,"mime":"text/plain"}' "$f"; check "cut clip_changed content+MIME" $?
grep -q '"type":"text_updated","text":"","cursor":-1}' "$f"; check "cut field cleared" $?
for p in t9-composing-committed t9-suggestion-strip t9-caret-mutate t9-clip-longpress t9-clip-selected t9-clip-copied t9-clip-paste-menu t9-clip-pasted t9-clip-cut-menu t9-clip-cut t9-inset-anim1 t9-inset-anim2 t9-inset-anim3; do
  [ -s "$EV/$p.png" ]; check "png $p present non-empty" $?
done
[ -s "$EV/t9-inset-frames.txt" ]; check "inset frames mInputShown samples" $?
grep -q 'mInputShown=false' "$EV/t9-inset-frames.txt"; check "anim frame0 keyboard hidden (transition)" $?
grep -q 'mInputShown=true' "$EV/t9-inset-frames.txt"; check "anim frames keyboard shown" $?
if [ "$fail" -eq 0 ]; then echo "T9-EVIDENCE-OK"; else echo "T9-EVIDENCE-FAIL"; fi
exit "$fail"
