// T10 — Present-window sink for vector text (F1 fix).
// FR-TEXT-016 / AC-TEXT-045 (device text is shaped vector text from the
// engine stack, presented on the window-backed Swapchain, not the CPU
// oracle).
//
// Public boundary: the plan T10 `Interface:` line (see unit-tests/T10.md).
// Hermetic: C++17 + committed runtime headers only; in-memory GalleryRows,
// an in-process window-backed FakeSwapchain recording, no device, no
// ANativeWindow, no GPU, no network, no config, no filesystem, no live
// services, no JNI on the frame path.
//
// Binds to the plan T10 `Interface:` line (see unit-tests/T10.md).
// RED STATE (expected): `PresentDeviceGalleryPage` exists nowhere in the
// tree — `RenderLocked` in jni_bridge.cpp still calls the CPU
// `RasterGalleryPage` oracle (whose caption painter is a documented no-op
// post-T7) for the presented window, and the retained ShapedText ->
// RasterizeShapedText -> Swapchain path has no on-screen sink. This TU
// therefore fails to compile until the coder reconnects the display path
// (EmitGalleryWindowCmds over the T5 ShapedText emission through
// Renderer::draw into a window-attached Swapchain whose RasterizeShapedText
// rasterizes each run in bounds). `glyphRunId == 0 -> UnknownPrimitive`
// stays untouched (covered by render_smoke_test, 006).
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

namespace {

runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// Device caps the baseline check rejects, so Renderer::draw returns a
// non-None DrawError without touching the target. Hermetic stand-in for
// the "non-None draw result" fail-closed leg (ASSUMPTION: api 0.0 is
// below the supported baseline; see unit-tests/T10.md).
runtime::DeviceCaps unsupported_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 0;
  caps.apiMinor = 0;
  return caps;
}

// Window-backed fake swapchain: records every submitted cmd in order,
// like a presented surface would retain them. No GPU, no ANativeWindow.
class FakeWindowSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override {
    captured.push_back(cmd);
  }
  std::vector<runtime::DrawCmd> captured;
};

runtime::GalleryRow MakeRow(const std::string& label) {
  runtime::GalleryRow row;
  row.label = label;
  return row;
}

// Vector-text coverage on the presented surface: ShapedText cmds with a
// real (nonzero) run id and non-degenerate bounds. Bitmap Text cmds do
// not count (AC-TEXT-045: no bitmap fallback on gallery pages).
std::size_t TextCoverage(const FakeWindowSwapchain& target) {
  std::size_t n = 0;
  for (const auto& cmd : target.captured) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText &&
        cmd.payload.glyphRunId != 0 && cmd.bounds.w > 0.0f &&
        cmd.bounds.h > 0.0f) {
      ++n;
    }
  }
  return n;
}

std::vector<std::uint32_t> ShapedRunIds(
    const FakeWindowSwapchain& target) {
  std::vector<std::uint32_t> out;
  for (const auto& cmd : target.captured) {
    if (cmd.kind == runtime::PrimitiveKind::ShapedText) {
      out.push_back(cmd.payload.glyphRunId);
    }
  }
  return out;
}

std::vector<runtime::GalleryRow> ScriptRows() {
  return {MakeRow("Primary action"), MakeRow("M3 Gallery header"),
          MakeRow("\xe4\xb8\xad\xe6\x96\x87"),  // U+4E2D U+6587 (CJK)
          MakeRow("\xf0\x9f\x98\x80")};         // U+1F600 (emoji)
}

// AC-TEXT-045: Latin/CJK/emoji labels with T5-produced run ids deliver
// vector pixels to the presented surface (nonzero text coverage, every
// shaped run nonzero).
static bool check_scripts_deliver_vector_pixels() {
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  std::string error;
  const bool ok = runtime::PresentDeviceGalleryPage(
      ScriptRows(), 0, renderer, window, &error);
  if (!ok) return false;
  if (!error.empty()) return false;
  if (TextCoverage(window) == 0) return false;
  for (std::uint32_t id : ShapedRunIds(window)) {
    if (id == 0) return false;
  }
  return !ShapedRunIds(window).empty();
}

// AC-TEXT-045 (atlas reused across frames): presenting the same rows a
// second time reuses the shared run ids (second-frame hits, no new
// handles) with nonzero coverage on both frames.
static bool check_shared_run_ids_reused_across_frames() {
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain first;
  FakeWindowSwapchain second;
  std::string e1;
  std::string e2;
  if (!runtime::PresentDeviceGalleryPage(ScriptRows(), 0, renderer, first,
                                         &e1)) {
    return false;
  }
  if (!runtime::PresentDeviceGalleryPage(ScriptRows(), 0, renderer, second,
                                         &e2)) {
    return false;
  }
  if (!e1.empty() || !e2.empty()) return false;
  if (TextCoverage(first) == 0 || TextCoverage(second) == 0) return false;
  return ShapedRunIds(first) == ShapedRunIds(second);
}

// RULE-RUNTIME-001: an unshapable label (no covering face, dangling run)
// fails closed with UnknownPrimitive semantics — false + diagnostic,
// nothing recorded on the presented surface.
static bool check_unknown_run_fails_closed() {
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  // U+FFFF is a noncharacter no face covers: no run can resolve for it.
  const std::vector<runtime::GalleryRow> rows = {MakeRow("\xef\xbf\xbf")};
  std::string error;
  const bool ok = runtime::PresentDeviceGalleryPage(rows, 0, renderer,
                                                    window, &error);
  if (ok) return false;
  if (error.empty()) return false;
  return window.captured.empty();
}

// RULE-RUNTIME-001: bad input (page out of range) fails closed — false +
// diagnostic, nothing presented.
static bool check_bad_page_fails_closed() {
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  std::string error;
  const bool ok = runtime::PresentDeviceGalleryPage(ScriptRows(), -1,
                                                    renderer, window, &error);
  if (ok) return false;
  if (error.empty()) return false;
  return window.captured.empty();
}

// RULE-RUNTIME-001: zero rows fails closed — false + diagnostic, nothing
// presented (same error shape as PresentGallery).
static bool check_zero_rows_fails_closed() {
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  std::string error;
  const bool ok = runtime::PresentDeviceGalleryPage({}, 0, renderer, window,
                                                    &error);
  if (ok) return false;
  if (error.empty()) return false;
  return window.captured.empty();
}

// RULE-RUNTIME-001: a non-None draw result fails closed — false +
// diagnostic, nothing presented.
static bool check_non_none_draw_fails_closed() {
  runtime::Renderer renderer(unsupported_device());
  FakeWindowSwapchain window;
  std::string error;
  const bool ok = runtime::PresentDeviceGalleryPage(ScriptRows(), 0,
                                                    renderer, window, &error);
  if (ok) return false;
  if (error.empty()) return false;
  return window.captured.empty();
}

// Success tolerates error == nullptr (same convention as PresentGallery:
// error == nullptr is tolerated on every leg).
static bool check_null_error_tolerated_on_success() {
  runtime::Renderer renderer(baseline_device());
  FakeWindowSwapchain window;
  const bool ok = runtime::PresentDeviceGalleryPage(ScriptRows(), 0,
                                                    renderer, window, nullptr);
  if (!ok) return false;
  return TextCoverage(window) > 0;
}

}  // namespace

#define RUN_CASE(name)                      \
  do {                                      \
    if (!(name)()) {                        \
      std::printf("FAIL %s\n", #name);      \
      ++failures;                           \
    } else {                                \
      std::printf("PASS %s\n", #name);      \
    }                                       \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_scripts_deliver_vector_pixels);
  RUN_CASE(check_shared_run_ids_reused_across_frames);
  RUN_CASE(check_unknown_run_fails_closed);
  RUN_CASE(check_bad_page_fails_closed);
  RUN_CASE(check_zero_rows_fails_closed);
  RUN_CASE(check_non_none_draw_fails_closed);
  RUN_CASE(check_null_error_tolerated_on_success);
  if (failures > 0) {
    std::printf("present_window_text_test: %d failing\n", failures);
    return 1;
  }
  std::printf("present_window_text_test: all passing\n");
  return 0;
}
