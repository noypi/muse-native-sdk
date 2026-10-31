# Hosting Paths — WebView, Maps, Buffer-Export-Incapable (FR-PVIEWS-004)

Scope: feature `fr-pviews-011-fr-pviews-012-fr-pviews-004`, item T5 (docs-only, no callable surface).
Spec source: `manual-context.local/specs-platform-views-embedding.md`, FR-PVIEWS-004 (AC-PVIEWS-005), with a narrow doc reference to FR-PVIEWS-013 (TC-PVIEWS-020).

## 1. WebView path (FR-PVIEWS-011)

WebView hosts through the Hybrid View Embedding contract (hidden Activity/Window layer with HardwareRenderer export). The engine enforces bounds and clipping; Safe Browsing is on and file access is off by default; OAuth completes via Custom Tabs handoff.

Mapped tests:

- T1 — JS bridge allowlist gate (`invokeBridge`, AC-PVIEWS-016): `engine/platform-views/tests/webview_bridge_test.cpp`.
- T2 — cookie/cache/DOM-storage persistence with data-clear respect (AC-PVIEWS-017): `engine/platform-views/tests/webview_persistence_test.cpp`.
- T3 — WebView hosting: bounds/clip/scroll, evaluateJavascript, OAuth + Custom Tabs handoff (AC-PVIEWS-018): `engine/platform-views/tests/webview_hosting_test.cpp`.

## 2. Maps path (FR-PVIEWS-012)

Google and MapLibre SDKs sit behind one provider interface riding the same Hybrid stub. Camera state survives recreate; engine overlays composite at the correct z-order; gestures route via the spatial-hash routing stub.

Mapped test:

- T4 — Maps hybrid hosting: camera/marker/gesture bridge with engine overlays (AC-PVIEWS-019): `engine/platform-views/tests/maps_hosting_test.cpp`.

## 3. Buffer-export-incapable path (FR-PVIEWS-013, future)

SDKs that cannot export a buffer take the offscreen Vulkan compositing fallback. Behavior is out-of-scope for this run; this section traces the path and names its mapped test only.

Mapped test (not-built-here / future, not implemented in this run):

- FR-PVIEWS-013 TC-PVIEWS-020: `engine/platform-views/tests/fallback_composition_test.cpp` — mapped future test, not-built-here.
