# Device proof — T4b leg, 127.0.0.1:6562 only (2026-11-02)

APK: `examples/m3-gallery/build/m3-gallery-debug.apk` (6,353,841 bytes, built 02:43).
Fresh install: `adb -s 127.0.0.1:6562 uninstall com.muse.host` → Success, then
streamed install → Success. Tree baked: `nativeLoadTree: bytes=50760 rows=121`,
matches `dist/tree.mui.bin` + `assets/tree.mui.bin` (50760 bytes, 39× `Button`).

uix: window 570x1131. `gallery-rows=121 gallery-pages=18` on every frame.

| Check | Result |
|-------|--------|
| Launch, no crash | PASS — `Displayed ... +135ms`, zero FATAL in full logcat (`dp_t4b_6562_logcat.txt`, `grep -c FATAL` = 0) |
| Next advances exactly one page | PASS via proof entry — `PROOF_ADVANCE_PAGE page=1` → `gallery-page=2/18`; `page=2` → `gallery-page=3/18`, one step each, no skip |
| Prev never pages | PASS — left-chrome tap (50,150) held `gallery-page=2/18` |
| Content taps repaint only | PASS — tap (500,150) held `gallery-page=2/18` (AC-SAMPLE-039 by design) |
| No-blank viewports | PASS — p1/p2/p3 show rendered cards on all three pages |
| Vector text present | PASS — outline glyphs render (stub-composer garble unchanged, T4a scope); purple Button container shapes visible on p3 with font failure present (graphic-shape rule holds at component level) |
| Layering z-order | PASS — per-page `[layering] slice` lines for pages 0,1,2 with expected comps |
| Touch Next-region | N/A ON 6562 — `isShellChromeAdvance` needs x≥600 (720px reference); this window is 570 wide so the region is unreachable; broadcast is the designated equivalent path (same tap entry per MainActivity:153) |
| Shell chrome band visual | UNVERIFIED — Prev/Next Button chrome + counter not discernible in 570x1230 shots (possible off-viewport or zero-size with `label=""`); component-level Button proof (p3) stands in |

Files: `dp_t4b_6562_p1.png` (page 1/18), `dp_t4b_6562_p2.png` (2/18),
`dp_t4b_6562_p3_tapnext.png` (content-tap repaint, still 2/18),
`dp_t4b_6562_p2_prevtap.png` (Prev-tap, still 2/18), `dp_t4b_6562_p3.png`
(3/18, Button shapes), `dp_t4b_6562_logcat.txt` (zero FATAL).

Security: non-shell origins never page (content + Prev taps verified no-advance);
backward broadcast refused without crash (`PROOF_ADVANCE_PAGE backward unsupported`
by design, forward-only); no new permissions, no ANR (`dumpsys window` clean).
6555 untouched (this leg used `-s 127.0.0.1:6562` throughout).
