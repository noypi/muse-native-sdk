# MTE Debug/QA On-Device Procedure (TC-CORE-028 / AC-CORE-028)

 Manual QA procedure for NFR-CORE-011 memory-safety tooling
 (SPEC-CORE-ENGINE, active). Human-executed; no callable code.
 Covers AC-CORE-028: MTE is used on capable hardware in the debug/QA matrix.

Prerequisites: an `adb`-reachable device, and a debug/QA engine build
configured with `-DMUSE_SANITIZER=mte` (the exact T1 option name in
`cmake/Sanitizers.cmake`: the `MUSE_SANITIZER` cache variable with value
`mte`). MTE builds require Clang targeting AArch64 (Armv8.5-A+memtag);
any other toolchain/hardware combination fails at configure time with the
documented `MUSE_SANITIZER=mte requires ...` diagnostic instead of
silently building without tagging.

## Step 1 — Select an Arm MTE-capable device

1. Connect the candidate device and confirm it is reachable:
   `adb devices` — expect the device serial with state `device`.
2. Check the CPU feature flags for Memory Tagging:
   `adb shell grep -m1 -i mte /proc/cpuinfo` — expect a `Features` line
   that contains the `mte` token, e.g.:
   `Features : fp asimd evtstrm aes pmull sha1 sha2 crc32 ... mte ...`
3. If no `mte` token appears, the device cannot run this matrix — stop and
   pick another device. (ASSUMPTION A-T4-1: the exact flag list varies by
   SoC/kernel; only the presence of the `mte` token is required.)

## Step 2 — Install the debug/QA build configured with `-DMUSE_SANITIZER=mte`

1. From a clean tree, configure the engine with the MTE option:
   `cmake -S <src> -B <dir> -DMUSE_SANITIZER=mte && cmake --build <dir>`
   (use the same `<src>`/`<dir>` layout as the safety suites).
2. Confirm the option took effect before installing:
   `grep '^MUSE_SANITIZER' <dir>/CMakeCache.txt` — expect a line of the
   shape `MUSE_SANITIZER:STRING=mte`.
3. Install the resulting debug/QA build on the device from Step 1 and
   launch the engine process. (ASSUMPTION A-T4-2: the install/launch
   packaging is whatever the current debug/QA pipeline produces; this
   procedure only requires that the installed binary is the one built in
   the previous commands.)

## Step 3 — Confirm MTE is enabled for the engine process

1. Find the engine pid: `adb shell pidof <engine-process-name>` — expect a
   single numeric pid. (ASSUMPTION A-T4-3: substitute the actual debug/QA
   process name for `<engine-process-name>`.)
2. Re-check tagging support from the device side:
   `adb shell grep -m1 -i mte /proc/cpuinfo` — expect the same `mte`
   token as in Step 1 (proves the kernel exposes tagging to userspace).
3. Exercise the seeded tag-violation check build and watch logcat:
   `adb logcat -c && <run the seeded MTE check> && adb logcat -d | grep -i -m2 -E 'memtag|MTE|tag'`
   — expect at least one fault line naming the tagging extension, e.g. a
   line containing `memtag` (or `MTE`) with a `SIGSEGV`-class fault for the
   engine pid. (ASSUMPTION A-T4-4: the exact tombstone/logcat wording is
   kernel-version dependent; the required signal is a tag-check fault tied
   to the engine pid, not any particular string beyond the `memtag`/`MTE`
   marker.)
4. Negative control: the same seeded check on a non-MTE (`none`/ASan) build
   on the same device must NOT produce the tagging fault — proving the
   fault in the previous command comes from MTE, not from the seed alone.

## Pass criterion

TC-CORE-028 passes iff: (a) Step 1 shows the `mte` CPU flag, (b) Step 2
shows `MUSE_SANITIZER:STRING=mte` for the installed build, and (c) Step 3
shows a tag-check fault for the engine pid on the seeded check, absent on
the non-MTE control build. Record the device model, kernel version
(`adb shell uname -r`), and the observed fault line with the QA sign-off.
