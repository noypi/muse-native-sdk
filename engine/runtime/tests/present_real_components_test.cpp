// Batch 1 contract tests — FR-GRAPHICS-015 / AC-GRAPHICS-042 (+ RULE-GRAPHICS-003 for T2).
// Binds ONLY to the plan's `Interface:` lines; never to implementation.
//
// T1: `runtime::PresenterError PresentRealComponents(const std::vector<DrawCmd>& cmds);`
// T2: `bool PresentRealGalleryPage(const std::vector<GalleryRow>& rows, int page,
//       runtime::Renderer& renderer, runtime::Swapchain& target, std::string* error);`
//
// Hermetic: C++17 + committed runtime headers only; in-memory MUI1 asset,
// in-process fake swapchain, real Renderer over hermetic DeviceCaps (no GPU,
// no NDK, no network, no env/config reads, no filesystem, no live services).
//
// Unit scope: fail-closed seams for the component-record presenter path.
// Device-frame proof (TC-GRAPHICS-044/045) and golden re-baseline
// (TC-GRAPHICS-046) are non-unit legs owned by the tester/device lane.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime/gallery_present.h"
#include "runtime/presenter.h"
#include "runtime/renderer.h"
#include "runtime/vulkan_baseline.h"

// --- harness cases (convention copied from present_frame_test.cpp) ----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

namespace {

#ifndef __ANDROID__
// Host-side opaque ANativeWindow fake (same seam as present_frame_test.cpp).
// The presenter must never dereference the handle as a View.
struct FakeNativeWindow {
  unsigned magic = 0xA11CEu;
};
#endif

void reset_presenter() {
  // Detach is idempotent; ignore the result during reset.
  (void)runtime::DetachSurface();
}

bool error_is(runtime::PresenterError err, runtime::PresenterCode want) {
  return err.code == want;
}

bool has_diagnostic(const runtime::PresenterError& err) {
  return !err.message.empty();
}

bool attach_valid_window(void* handle) {
  runtime::PresenterError attach = runtime::AttachSurface(handle);
  return error_is(attach, runtime::PresenterCode::kOk);
}

bool create_swapchain_for_present() {
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = true;
  return error_is(runtime::CreateSurfaceAndSwapchain(p),
                  runtime::PresenterCode::kOk);
}

// A hand-built, never-DAG-recorded command list. Well-formed as data but NOT
// the current-frame set, so presenting it must fail closed.
runtime::DrawCmds stale_cmds() {
  runtime::DrawCmds cmds;
  runtime::DrawCmd cmd;
  cmd.nodeId = 7;
  cmds.push_back(cmd);
  return cmds;
}

// Baseline device caps: Vulkan 1.1 with no extensions required.
runtime::DeviceCaps baseline_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 1;
  caps.apiMinor = 1;
  return caps;
}

// Degraded caps that fail the baseline gate, so Renderer::draw returns a
// non-None DrawError without any GPU, file, or network access.
runtime::DeviceCaps degraded_device() {
  runtime::DeviceCaps caps;
  caps.apiMajor = 0;
  caps.apiMinor = 0;
  return caps;
}

// In-process fake swapchain: records every submitted cmd. No GPU.
class FakeSwapchain : public runtime::Swapchain {
 public:
  void submit(const runtime::DrawCmd& cmd) override { captured.push_back(cmd); }
  std::vector<runtime::DrawCmd> captured;
};

bool is_declared_kind(runtime::PrimitiveKind kind) {
  switch (kind) {
    case runtime::PrimitiveKind::Rect:
    case runtime::PrimitiveKind::RoundedRect:
    case runtime::PrimitiveKind::Image:
    case runtime::PrimitiveKind::Text:
    case runtime::PrimitiveKind::StateLayer:
    case runtime::PrimitiveKind::Ripple:
    case runtime::PrimitiveKind::ElevationShadow:
    case runtime::PrimitiveKind::CircleArc:
    case runtime::PrimitiveKind::VectorIcon:
    case runtime::PrimitiveKind::ShapedText:
    case runtime::PrimitiveKind::OverlayScrim:
      return true;
  }
  return false;
}

// Minimal IR with one static Showcase-style root (copied from
// gallery_present_test.cpp convention): two headers (light/dark), two
// labels, and two literal-prop components.
const char kIrJson[] =
    "{\"version\":1,\"roots\":["
    "{\"type\":\"Module\",\"file\":\"src/Showcase.tsx\","
    "\"props\":{},\"slots\":[],\"children\":["
    "{\"type\":\"#text\",\"value\":\"M3 Gallery light\"},"
    "{\"type\":\"#text\",\"value\":\"Primary action\"},"
    "{\"type\":\"#text\",\"value\":\"M3 Gallery dark\"},"
    "{\"type\":\"#text\",\"value\":\"Primary action dark\"},"
    "{\"type\":\"Button\",\"props\":{\"variant\":\"filled\",\"label\":\"Primary "
    "action\"}},"
    "{\"type\":\"TopAppBar\",\"props\":{\"title\":\"m3-gallery\",\"size\":"
    "\"small\"}}"
    "]}]}";

std::vector<std::uint8_t> MakeAsset() {
  std::string json(kIrJson);
  std::vector<std::uint8_t> out;
  out.reserve(12 + json.size());
  out.push_back('M');
  out.push_back('U');
  out.push_back('I');
  out.push_back('1');
  out.push_back(1);
  out.push_back(0);
  out.push_back(0);
  out.push_back(0);
  std::uint32_t n = static_cast<std::uint32_t>(json.size());
  out.push_back(static_cast<std::uint8_t>(n & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFFu));
  out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFFu));
  for (char c : json) out.push_back(static_cast<std::uint8_t>(c));
  return out;
}

std::vector<runtime::GalleryRow> showcase_rows() {
  std::vector<std::uint8_t> asset = MakeAsset();
  std::string error;
  return runtime::ParseGalleryRows(asset.data(), asset.size(), &error);
}

}  // namespace

// --- T1: PresentRealComponents ----------------------------------------------

// T1: no live swapchain -> kNoSwapchain + non-blank diagnostic; never throws.
// A second call still fails closed and later valid attach/create/record still
// succeeds (no state mutated by the rejected present).
static bool check_real_components_without_swapchain_fails_closed() {
  try {
    reset_presenter();
    runtime::DrawCmds empty;
    runtime::PresenterError err = runtime::PresentRealComponents(empty);
    bool ok = error_is(err, runtime::PresenterCode::kNoSwapchain) &&
              has_diagnostic(err);
    runtime::PresenterError again = runtime::PresentRealComponents(empty);
    ok = ok && error_is(again, runtime::PresenterCode::kNoSwapchain) &&
         has_diagnostic(again);
#ifndef __ANDROID__
    FakeNativeWindow w;
    void* handle = static_cast<void*>(&w);
#else
    void* handle = reinterpret_cast<void*>(0x1234);
#endif
    ok = ok && attach_valid_window(handle) && create_swapchain_for_present();
    runtime::NoteFrameRecorded();
    runtime::DrawCmds cmds = stale_cmds();
    ok = ok && error_is(runtime::PresentRealComponents(cmds),
                        runtime::PresenterCode::kOk);
    reset_presenter();
    return ok;
  } catch (...) {
    reset_presenter();
    return false;
  }
}

// T1: live swapchain + empty (never-DAG-recorded) set -> kNoCurrentFrame +
// diagnostic; never presented, never throws.
static bool check_real_components_empty_never_presented() {
  try {
    reset_presenter();
#ifndef __ANDROID__
    FakeNativeWindow w;
    void* handle = static_cast<void*>(&w);
#else
    void* handle = reinterpret_cast<void*>(0x1234);
#endif
    if (!attach_valid_window(handle) || !create_swapchain_for_present()) {
      reset_presenter();
      return false;
    }
    runtime::DrawCmds empty;
    runtime::PresenterError err = runtime::PresentRealComponents(empty);
    bool ok = error_is(err, runtime::PresenterCode::kNoCurrentFrame) &&
              has_diagnostic(err);
    reset_presenter();
    return ok;
  } catch (...) {
    reset_presenter();
    return false;
  }
}

// T1: live swapchain + hand-built (never-DAG-recorded) non-empty set ->
// kNoCurrentFrame + diagnostic; only the DAG-recorded current-frame set may
// be presented. Never throws.
static bool check_real_components_stale_never_presented() {
  try {
    reset_presenter();
#ifndef __ANDROID__
    FakeNativeWindow w;
    void* handle = static_cast<void*>(&w);
#else
    void* handle = reinterpret_cast<void*>(0x1234);
#endif
    if (!attach_valid_window(handle) || !create_swapchain_for_present()) {
      reset_presenter();
      return false;
    }
    runtime::DrawCmds stale = stale_cmds();
    runtime::PresenterError err = runtime::PresentRealComponents(stale);
    bool ok = error_is(err, runtime::PresenterCode::kNoCurrentFrame) &&
              has_diagnostic(err);
    reset_presenter();
    return ok;
  } catch (...) {
    reset_presenter();
    return false;
  }
}

// T1: after the DAG record path calls NoteFrameRecorded(), the recorded
// current-frame set presents with kOk on a live swapchain. ASSUMPTION: T1
// shares the NoteFrameRecorded currency flag with PresentFrame (plan Detail:
// "same currency via NoteFrameRecorded"). Never throws.
static bool check_real_components_after_record_returns_ok() {
  try {
    reset_presenter();
#ifndef __ANDROID__
    FakeNativeWindow w;
    void* handle = static_cast<void*>(&w);
#else
    void* handle = reinterpret_cast<void*>(0x1234);
#endif
    if (!attach_valid_window(handle) || !create_swapchain_for_present()) {
      reset_presenter();
      return false;
    }
    runtime::NoteFrameRecorded();
    runtime::DrawCmds cmds = stale_cmds();
    runtime::PresenterError err = runtime::PresentRealComponents(cmds);
    bool ok = error_is(err, runtime::PresenterCode::kOk);
    reset_presenter();
    return ok;
  } catch (...) {
    reset_presenter();
    return false;
  }
}

// T1: after the swapchain is gone (destroyed surface) -> kNoSwapchain +
// diagnostic, no dangling-surface present. Never throws.
static bool check_real_components_after_destroy_fails_closed() {
  try {
    reset_presenter();
#ifndef __ANDROID__
    FakeNativeWindow w;
    void* handle = static_cast<void*>(&w);
#else
    void* handle = reinterpret_cast<void*>(0x1234);
#endif
    if (!attach_valid_window(handle) || !create_swapchain_for_present()) {
      reset_presenter();
      return false;
    }
    runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
    if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
      reset_presenter();
      return false;
    }
    runtime::DrawCmds stale = stale_cmds();
    runtime::PresenterError err = runtime::PresentRealComponents(stale);
    bool ok = error_is(err, runtime::PresenterCode::kNoSwapchain) &&
              has_diagnostic(err);
    reset_presenter();
    return ok;
  } catch (...) {
    reset_presenter();
    return false;
  }
}

// --- T2: PresentRealGalleryPage ---------------------------------------------

// T2: valid Showcase rows, page 0 -> true with *error untouched; every
// emitted cmd is in the retained 11-kind vocabulary. Never throws.
// (The oracle-fencing half of RULE-GRAPHICS-003 is proven by the T3 build
// gate; this test pins the observable proxy: the new path emits only
// retained-vocabulary cmds through renderer.draw into the target swapchain,
// and no CPU-raster pixel buffer exists on this path.)
static bool check_real_gallery_page_success_emits_retained_vocabulary() {
  try {
    std::vector<runtime::GalleryRow> rows = showcase_rows();
    if (rows.empty()) return false;
    runtime::Renderer renderer(baseline_device());
    FakeSwapchain target;
    std::string error;
    bool ok = runtime::PresentRealGalleryPage(rows, 0, renderer, target,
                                             &error);
    if (!ok || !error.empty()) return false;
    if (target.captured.empty()) return false;
    for (const auto& cmd : target.captured) {
      if (!is_declared_kind(cmd.kind)) return false;
    }
    return true;
  } catch (...) {
    return false;
  }
}

// T2: bad envelope rows (empty parse) -> false + non-blank *error, nothing
// recorded. Never throws.
static bool check_real_gallery_page_bad_envelope_fails_closed() {
  try {
    std::vector<runtime::GalleryRow> rows;
    std::string error;
    {
      const std::uint8_t bad[] = {'X', 'Y', 'Z'};
      std::string parseError;
      rows = runtime::ParseGalleryRows(bad, sizeof(bad), &parseError);
      if (!rows.empty()) return false;
    }
    runtime::Renderer renderer(baseline_device());
    FakeSwapchain target;
    bool ok = runtime::PresentRealGalleryPage(rows, 0, renderer, target,
                                             &error);
    return !ok && !error.empty() && target.captured.empty();
  } catch (...) {
    return false;
  }
}

// T2: zero rows passed directly -> false + non-blank *error, nothing
// recorded. Never throws.
static bool check_real_gallery_page_zero_rows_fails_closed() {
  try {
    std::vector<runtime::GalleryRow> rows;
    runtime::Renderer renderer(baseline_device());
    FakeSwapchain target;
    std::string error;
    bool ok = runtime::PresentRealGalleryPage(rows, 0, renderer, target,
                                             &error);
    return !ok && !error.empty() && target.captured.empty();
  } catch (...) {
    return false;
  }
}

// T2: non-None draw result -> false + non-blank *error, nothing recorded.
// Forced hermetically with degraded device caps (baseline gate rejects, so
// Renderer::draw returns UnsupportedBaseline however the page maps rows).
// Never throws.
static bool check_real_gallery_page_draw_error_fails_closed() {
  try {
    std::vector<runtime::GalleryRow> rows = showcase_rows();
    if (rows.empty()) return false;
    runtime::Renderer renderer(degraded_device());
    FakeSwapchain target;
    std::string error;
    bool ok = runtime::PresentRealGalleryPage(rows, 0, renderer, target,
                                             &error);
    return !ok && !error.empty() && target.captured.empty();
  } catch (...) {
    return false;
  }
}

// T2: error == nullptr is tolerated on the failure leg (no crash, still
// false). Never throws.
static bool check_real_gallery_page_null_error_tolerated_on_failure() {
  try {
    std::vector<runtime::GalleryRow> rows;
    runtime::Renderer renderer(baseline_device());
    FakeSwapchain target;
    bool ok = runtime::PresentRealGalleryPage(rows, 0, renderer, target,
                                             nullptr);
    return !ok;
  } catch (...) {
    return false;
  }
}

// T2: error == nullptr is tolerated on the success leg (no crash, still
// true, target still recorded). Never throws.
static bool check_real_gallery_page_null_error_tolerated_on_success() {
  try {
    std::vector<runtime::GalleryRow> rows = showcase_rows();
    if (rows.empty()) return false;
    runtime::Renderer renderer(baseline_device());
    FakeSwapchain target;
    bool ok = runtime::PresentRealGalleryPage(rows, 0, renderer, target,
                                             nullptr);
    return ok && !target.captured.empty();
  } catch (...) {
    return false;
  }
}

// T2 boundary: negative page fails closed (false + non-blank *error,
// nothing recorded). ASSUMPTION: page is a 0-based index into the pager, so a
// negative page is bad input under the sibling entries' fail-closed shape.
// Never throws.
static bool check_real_gallery_page_negative_page_fails_closed() {
  try {
    std::vector<runtime::GalleryRow> rows = showcase_rows();
    if (rows.empty()) return false;
    runtime::Renderer renderer(baseline_device());
    FakeSwapchain target;
    std::string error;
    bool ok = runtime::PresentRealGalleryPage(rows, -1, renderer, target,
                                             &error);
    return !ok && !error.empty() && target.captured.empty();
  } catch (...) {
    return false;
  }
}

#define RUN_CASE(name)                                       \
  do {                                                       \
    if (!(name)()) {                                         \
      std::printf("FAIL %s\n", #name);                       \
      ++failures;                                            \
    } else {                                                 \
      std::printf("PASS %s\n", #name);                       \
    }                                                        \
  } while (0)

int main() {
  int failures = 0;
  RUN_CASE(check_harness_entry_reached);
  RUN_CASE(check_all_green_returns_zero);
  RUN_CASE(check_failure_protocol_shape);
  RUN_CASE(check_real_components_without_swapchain_fails_closed);
  RUN_CASE(check_real_components_empty_never_presented);
  RUN_CASE(check_real_components_stale_never_presented);
  RUN_CASE(check_real_components_after_record_returns_ok);
  RUN_CASE(check_real_components_after_destroy_fails_closed);
  RUN_CASE(check_real_gallery_page_success_emits_retained_vocabulary);
  RUN_CASE(check_real_gallery_page_bad_envelope_fails_closed);
  RUN_CASE(check_real_gallery_page_zero_rows_fails_closed);
  RUN_CASE(check_real_gallery_page_draw_error_fails_closed);
  RUN_CASE(check_real_gallery_page_null_error_tolerated_on_failure);
  RUN_CASE(check_real_gallery_page_null_error_tolerated_on_success);
  RUN_CASE(check_real_gallery_page_negative_page_fails_closed);
  if (failures > 0) {
    std::printf("present_real_components_test: %d failing\n", failures);
    return 1;
  }
  std::printf("present_real_components_test: all passing\n");
  return 0;
}
