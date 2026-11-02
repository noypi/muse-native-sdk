# Device proof — T4b leg, 127.0.0.1:6555 (2026-11-03, tester leg — LIVE, supersedes 2026-11-02 BLOCKED note)

APK: `examples/m3-gallery/build/m3-gallery-debug.apk` in this `_run` worktree
(md5 `0fd163a8d3f8219554511f83696c257e`, 62,538,673 bytes).
Fresh install: `adb -s 127.0.0.1:6555 uninstall com.muse.host` → Success, then
streamed install → Success. Installed base.apk pulled back and re-md5'd:
`0fd163a8d3f8219554511f83696c257e` — staged == installed MATCH.
Tree baked: `nativeLoadTree: bytes=50760 rows=121` (matches 6562 leg).

Display: physical 570x1230, override 1080x2280 → Next-region (x≥600, y≤200)
REACHABLE here (unlike 6562's 570-wide window). 121 rows / 9 pages.

| Check | Result |
|-------|--------|
| Launch, no crash | PASS — `am start` ok; 0 FATAL in full logcat; no ANR / Force close / has-died |
| Next advances exactly one page (touch) | PASS — tap (900,100): `gallery-page=1/9` → `2/9`, one step |
| Next advances exactly one page (broadcast) | PASS — `PROOF_ADVANCE_PAGE page=2` → `gallery-page=3/9`, one step |
| Prev never pages | PASS — tap (50,150) held `gallery-page=3/9` |
| Content tap repaints only | PASS — tap (500,500) held `gallery-page=3/9` (AC-SAMPLE-039) |
| Mechanics (logcat tree dumps) | PASS — layering slices for pages 0,1,2 with expected comps |
| Legibility (BUG-TEXT-008) | Text renders DARK glyphs on light cards, visible in p1/p3 — with known stub-composer distortions (`m8-gaIIery` for `m3-gallery`, `3`→`8`; T4a scope, pre-fix). NOT pure-black blocks; labels readable-through-garble. Reported separately; does NOT fail mechanics. |
| Shell-chrome band visual | Button container shapes visible (p3 purple blocks); Prev/Next chrome pair + counter not individually discernible — same standing note as 6562 G1 |

Files: `dp_t4b_6555_p1.png` (1/9), `dp_t4b_6555_p2.png` (2/9, after touch-Next),
`dp_t4b_6555_p3.png` (3/9, after broadcast), `dp_t4b_6555_p3_prevtap.png`
(Prev-tap, still 3/9), `dp_t4b_6555_p3_contenttap.png` (content-tap, still 3/9),
`dp_t4b_6555_logcat.txt` (zero FATAL). No code/tests/git touched.
`adb -s 127.0.0.1:6555` used throughout; 6562 untouched.

Prior 2026-11-02 BLOCKED record (no device; hermetic md5/test numbers) kept for
history: tree 2d538367… (50760 B) staged==delivery ×4 incl. APK asset; per-ABI
libRuntime.so matched; gallery-proof.test.ts 51/51; button-chrome suite 89/89;
gallery-proof-advance 10/11 with 1 pre-existing FAIL (NATIVE_ADVANCE_PAGE vs
PROOF_ADVANCE_PAGE naming, coder-owned).
