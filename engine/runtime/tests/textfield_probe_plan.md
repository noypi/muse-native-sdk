# T1 baseline — textfield probe (scratch note, run-scoped)

Run: bug-m3-fidelity-003 · Date: 2026-10-31 · Worktree: `.worktrees/bug-m3-fidelity-003/_run`
Device gate: API-29-only on 127.0.0.1:6569 (BQ2 settled).

## Commands + exit codes (all run from `_run`)

1. `adb -s 127.0.0.1:6569 shell getprop ro.build.version.sdk`
   - Output: `29` — exit 0. Meets SDK 29 expectation.
2. `adb -s 127.0.0.1:6569 shell dumpsys activity activities | grep -i MainActivity | head -5`
   - Output: `intent={flg=0x10000000 cmp=com.muse.host/.MainActivity}`,
     `mActivityComponent=com.muse.host/.MainActivity`,
     `Activities=[ActivityRecord{... com.muse.host/.MainActivity t63}]` — exit 0.
     MainActivity present.
3. `adb -s 127.0.0.1:6569 logcat -d -t 30`
   - Exit 0. Slice shows emulator/system noise only (gralloc goldfish_pipe,
     MediaScanner scan of `/sdcard/bug-m3f3-probe.png`, ExifInterface warnings
     for the PNG, KeyguardClockSwitch ticks, WificondControl scans).
     No `muse` IME/composition/text tags firing — consistent with support.md:
     static-box device path, no IME bridge yet (pre-T2 baseline).

## Evidence layout

Created (directories only; `evidence/index.md` scaffold is T7's file, not created here):
- `workflow.local/runs/bug-m3-fidelity-003/evidence/T3/`
- `workflow.local/runs/bug-m3-fidelity-003/evidence/T4/`
- `workflow.local/runs/bug-m3-fidelity-003/evidence/T5/`
- `workflow.local/runs/bug-m3-fidelity-003/evidence/T6/`

Device legs T3–T6 will each drop PNGs/logs under their directory.

## Status guard

- `git status --porcelain` in `_run` checked after the above; see T1 report.
- No build/, dist/, keystore, or root-PNG artifacts added by T1.
- Only claim written by T1: this file (`engine/runtime/tests/textfield_probe_plan.md`).
