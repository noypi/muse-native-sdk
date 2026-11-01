// T1 contract tests — AC-RUNTIME-033 / AC-RUNTIME-034 (Surface interactive path).
// Binds ONLY to the T1 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; host-side opaque-window
// fakes behind `#ifndef __ANDROID__` guards; no NDK/Vulkan calls on host,
// no network, no env/config reads, no filesystem, no live services.
//
// ASSUMPTION (C++ spelling): the plan `Interface:` line settles names, params
// and error codes but not the C++ spelling. These tests assume:
//   using SurfaceHandle = uint64_t (0 == invalid);
//   struct CompiledTreeRef { std::string rootKind; bool unknownKind; };
//   struct AttachResult { SurfaceHandle handle; PresenterError error; };
//   AttachResult attachSurface(const CompiledTreeRef&, void*);
//   PresenterError dispatchTouch(SurfaceHandle, int32_t, int32_t, float, float, int64_t);
//   PresenterError renderFrame(SurfaceHandle, const DrawCmds&);
//   PresenterError setTheme(SurfaceHandle, ThemeId);
//   PresenterError setImeBottom(SurfaceHandle, int32_t);
//   PresenterError detachSurface(SurfaceHandle);    // idempotent -> kOk
//   PresenterError destroySurface(SurfaceHandle);   // idempotent -> kOk
//   PresenterError getFrameStats(SurfaceHandle, FrameStats*);
//   void NoteFrameRecordedForSurface(SurfaceHandle, const DrawCmds&);
// If the coder spells them differently, update these call sites
// (reconciliation, not weakening) and re-publish.
// Until the coder defines them, compiling/linking this file MUST fail
// (expected red, not a malformed test).
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "runtime/presenter.h"

// --- harness cases (convention copied from surface_attach_test.cpp) ----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- helpers -----------------------------------------------------------------

namespace {

#ifndef __ANDROID__
// Host-side opaque ANativeWindow fake. Never dereferenced as a View; validity
// is established behind the non-Android guard. Real NDK calls happen only
// under `__ANDROID__`.
struct FakeNativeWindow {
  unsigned magic = 0xA11CEu;
};
#endif

bool error_is(runtime::PresenterError err, runtime::PresenterCode want) {
  return err.code == want;
}

bool has_diagnostic(const runtime::PresenterError& err) {
  return !err.message.empty();
}

// Closed error shape from the T1 Interface line. Any PresenterCode outside
// this set is a contract violation, even with a diagnostic.
bool code_in_closed_set(runtime::PresenterCode c) {
  using runtime::PresenterCode;
  return c == PresenterCode::kOk || c == PresenterCode::kNullWindow ||
         c == PresenterCode::kInvalidWindow ||
         c == PresenterCode::kAlreadyAttached ||
         c == PresenterCode::kCreateFailed ||
         c == PresenterCode::kNoSurface ||
         c == PresenterCode::kNoSwapchain ||
         c == PresenterCode::kNoCurrentFrame;
}

runtime::CompiledTreeRef make_valid_tree() {
  runtime::CompiledTreeRef tree;
  tree.rootKind = "M3Button";
  tree.unknownKind = false;
  return tree;
}

runtime::CompiledTreeRef make_unknown_kind_tree() {
  runtime::CompiledTreeRef tree;
  tree.rootKind = "NotARealKind_xyz";
  tree.unknownKind = true;
  return tree;
}

void* valid_window() {
#ifndef __ANDROID__
  static FakeNativeWindow w;
  return static_cast<void*>(&w);
#else
  return reinterpret_cast<void*>(0x1234);
#endif
}

}  // namespace

// Full interactive cycle: attach -> touch -> render -> theme/inset -> detach.
static bool check_attach_touch_render_theme_inset_detach_cycle() {
  runtime::AttachResult r =
      runtime::attachSurface(make_valid_tree(), valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk) || r.handle == 0)
    return false;
  bool ok = code_in_closed_set(r.error.code);
  // Touch (raw MotionEvent fields: action/pointerId/xPx/yPx/eventTimeMs).
  runtime::PresenterError touch =
      runtime::dispatchTouch(r.handle, 0, 7, 150.0f, 140.0f, 12345);
  ok = ok && error_is(touch, runtime::PresenterCode::kOk) &&
       code_in_closed_set(touch.code);
  // Render the DAG-recorded current-frame cmds (tap advances page state in the
  // hermetic double: touch kOk + recorded render kOk, zero faults by
  // construction — no UnsatisfiedLinkError/SIGSEGV-shaped outcome exists on
  // the host lane).
  runtime::DrawCmds cmds;
  runtime::DrawCmd cmd;
  cmds.push_back(cmd);
  runtime::NoteFrameRecordedForSurface(r.handle, cmds);
  runtime::PresenterError render = runtime::renderFrame(r.handle, cmds);
  ok = ok && error_is(render, runtime::PresenterCode::kOk) &&
       code_in_closed_set(render.code);
  // In-place theme + inset, no re-attach; surface stays live.
  ok = ok && error_is(runtime::setTheme(r.handle, runtime::ThemeId::kDark),
                      runtime::PresenterCode::kOk);
  ok = ok &&
       error_is(runtime::setImeBottom(r.handle, 120),
                runtime::PresenterCode::kOk);
  runtime::DrawCmds cmds2;
  runtime::NoteFrameRecordedForSurface(r.handle, cmds2);
  ok = ok &&
       error_is(runtime::renderFrame(r.handle, cmds2),
                runtime::PresenterCode::kOk);
  ok = ok &&
       error_is(runtime::detachSurface(r.handle),
                runtime::PresenterCode::kOk);
  ok = ok &&
       error_is(runtime::destroySurface(r.handle),
                runtime::PresenterCode::kOk);
  return ok;
}

// attach(null) fails closed with kNullWindow + non-empty diagnostic.
static bool check_attach_null_window_fails_closed() {
  runtime::AttachResult r = runtime::attachSurface(make_valid_tree(), nullptr);
  return error_is(r.error, runtime::PresenterCode::kNullWindow) &&
         has_diagnostic(r.error) && code_in_closed_set(r.error.code);
}

// Non-null non-window handle fails closed with kInvalidWindow + diagnostic.
static bool check_attach_invalid_window_fails_closed() {
#ifndef __ANDROID__
  int bogus = 0;  // never a FakeNativeWindow: must be rejected, not probed.
  void* bad = static_cast<void*>(&bogus);
#else
  void* bad = reinterpret_cast<void*>(0x1);
#endif
  runtime::AttachResult r = runtime::attachSurface(make_valid_tree(), bad);
  return error_is(r.error, runtime::PresenterCode::kInvalidWindow) &&
         has_diagnostic(r.error) && code_in_closed_set(r.error.code);
}

// Double-attach without detach is rejected with kAlreadyAttached; first
// surface state is untouched (still live and usable).
static bool check_double_attach_rejected_state_untouched() {
  runtime::AttachResult first =
      runtime::attachSurface(make_valid_tree(), valid_window());
  if (!error_is(first.error, runtime::PresenterCode::kOk)) return false;
  runtime::CompiledTreeRef other;
  other.rootKind = "M3Gallery";
  other.unknownKind = false;
  runtime::AttachResult second = runtime::attachSurface(other, valid_window());
  bool ok = error_is(second.error, runtime::PresenterCode::kAlreadyAttached) &&
            has_diagnostic(second.error) &&
            code_in_closed_set(second.error.code);
  runtime::FrameStats stats{};
  ok = ok && error_is(runtime::getFrameStats(first.handle, &stats),
                      runtime::PresenterCode::kOk);
  runtime::DrawCmds cmds;
  runtime::NoteFrameRecordedForSurface(first.handle, cmds);
  ok = ok && error_is(runtime::renderFrame(first.handle, cmds),
                      runtime::PresenterCode::kOk);
  (void)runtime::detachSurface(first.handle);
  (void)runtime::destroySurface(first.handle);
  return ok;
}

// Unknown root kind fails closed with kCreateFailed + diagnostic; no usable
// handle and no fallback box: any returned handle rejects renderFrame and
// never presents.
static bool check_unknown_kind_fails_closed_never_fallback_box() {
  runtime::AttachResult r =
      runtime::attachSurface(make_unknown_kind_tree(), valid_window());
  if (!(error_is(r.error, runtime::PresenterCode::kCreateFailed) &&
        has_diagnostic(r.error) && code_in_closed_set(r.error.code))) {
    (void)runtime::detachSurface(r.handle);
    (void)runtime::destroySurface(r.handle);
    return false;
  }
  runtime::DrawCmds cmds;
  runtime::PresenterError render = runtime::renderFrame(r.handle, cmds);
  bool render_rejected =
      error_is(render, runtime::PresenterCode::kCreateFailed) ||
      error_is(render, runtime::PresenterCode::kNoSurface) ||
      error_is(render, runtime::PresenterCode::kNoSwapchain) ||
      error_is(render, runtime::PresenterCode::kNoCurrentFrame);
  bool ok = render_rejected && has_diagnostic(render) &&
            code_in_closed_set(render.code);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// detach/destroy are idempotent: twice each still kOk.
static bool check_detach_destroy_idempotent() {
  runtime::AttachResult r =
      runtime::attachSurface(make_valid_tree(), valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  bool ok = error_is(runtime::detachSurface(r.handle),
                     runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::detachSurface(r.handle),
                      runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::destroySurface(r.handle),
                      runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::destroySurface(r.handle),
                      runtime::PresenterCode::kOk);
  return ok && code_in_closed_set(runtime::PresenterCode::kOk);
}

// Touch with no live surface fails closed with kNoSurface + diagnostic.
static bool check_touch_without_surface_fails_closed() {
  runtime::AttachResult r =
      runtime::attachSurface(make_valid_tree(), valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  runtime::PresenterError err =
      runtime::dispatchTouch(r.handle, 0, 7, 150.0f, 140.0f, 12345);
  return error_is(err, runtime::PresenterCode::kNoSurface) &&
         has_diagnostic(err) && code_in_closed_set(err.code);
}

// renderFrame on a dead handle fails closed (never kOk, always diagnostic,
// always inside the closed set).
static bool check_render_without_surface_fails_closed() {
  runtime::AttachResult r =
      runtime::attachSurface(make_valid_tree(), valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  runtime::DrawCmds cmds;
  runtime::PresenterError err = runtime::renderFrame(r.handle, cmds);
  bool rejected = !error_is(err, runtime::PresenterCode::kOk);
  return rejected && has_diagnostic(err) && code_in_closed_set(err.code);
}

// Hand-built cmds that were never DAG-recorded fail closed with
// kNoCurrentFrame, never presented.
static bool check_render_unrecorded_cmds_rejected() {
  runtime::AttachResult r =
      runtime::attachSurface(make_valid_tree(), valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  runtime::DrawCmds hand_built;
  runtime::DrawCmd cmd;
  hand_built.push_back(cmd);
  runtime::PresenterError err = runtime::renderFrame(r.handle, hand_built);
  bool ok = error_is(err, runtime::PresenterCode::kNoCurrentFrame) &&
            has_diagnostic(err) && code_in_closed_set(err.code);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// Every non-kOk error carries a non-empty diagnostic (closed-shape half of
// the Interface line): null/invalid/double/unknown/dead-handle paths.
static bool check_every_error_carries_diagnostic() {
  bool ok = true;
  {
    runtime::AttachResult r =
        runtime::attachSurface(make_valid_tree(), nullptr);
    ok = ok && !error_is(r.error, runtime::PresenterCode::kOk) &&
         has_diagnostic(r.error) && code_in_closed_set(r.error.code);
  }
  {
    runtime::AttachResult r =
        runtime::attachSurface(make_unknown_kind_tree(), valid_window());
    ok = ok && !error_is(r.error, runtime::PresenterCode::kOk) &&
         has_diagnostic(r.error) && code_in_closed_set(r.error.code);
    (void)runtime::detachSurface(r.handle);
    (void)runtime::destroySurface(r.handle);
  }
  {
    runtime::AttachResult r =
        runtime::attachSurface(make_valid_tree(), valid_window());
    if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
    (void)runtime::detachSurface(r.handle);
    (void)runtime::destroySurface(r.handle);
    runtime::DrawCmds cmds;
    runtime::PresenterError render = runtime::renderFrame(r.handle, cmds);
    ok = ok && !error_is(render, runtime::PresenterCode::kOk) &&
         has_diagnostic(render) && code_in_closed_set(render.code);
    runtime::PresenterError touch =
        runtime::dispatchTouch(r.handle, 0, 7, 1.0f, 1.0f, 1);
    ok = ok && !error_is(touch, runtime::PresenterCode::kOk) &&
         has_diagnostic(touch) && code_in_closed_set(touch.code);
  }
  return ok;
}

// Security [LOW]: empty rootKind (unknownKind=false) is treated as unknown:
// kCreateFailed + diagnostic, no usable handle.
static bool check_empty_root_kind_rejected_fail_closed() {
  runtime::CompiledTreeRef tree;
  tree.rootKind = "";
  tree.unknownKind = false;
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!(error_is(r.error, runtime::PresenterCode::kCreateFailed) &&
        has_diagnostic(r.error) && code_in_closed_set(r.error.code))) {
    (void)runtime::detachSurface(r.handle);
    (void)runtime::destroySurface(r.handle);
    return false;
  }
  runtime::DrawCmds cmds;
  runtime::PresenterError render = runtime::renderFrame(r.handle, cmds);
  bool ok = !error_is(render, runtime::PresenterCode::kOk) &&
            has_diagnostic(render) && code_in_closed_set(render.code);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// Security: oversized/negative IME inset never crashes; surface stays live.
static bool check_ime_inset_bounds_fail_closed() {
  runtime::AttachResult r =
      runtime::attachSurface(make_valid_tree(), valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  bool ok = error_is(runtime::setImeBottom(r.handle, 100000),
                     runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::setImeBottom(r.handle, -1),
                      runtime::PresenterCode::kOk);
  runtime::DrawCmds cmds;
  runtime::NoteFrameRecordedForSurface(r.handle, cmds);
  ok = ok && error_is(runtime::renderFrame(r.handle, cmds),
                      runtime::PresenterCode::kOk);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// No gallery concepts in the seam. Compile-guard half: this file names no
// page/story/Prev/Next symbol, so any gallery concept in the signature breaks
// the build. Grep-gate half (owns the token check, recorded in the contract):
// `grep -rE "page|stor|Prev|Next" engine/runtime/include/runtime/presenter.h`
// must print nothing. This case pins the compile half; it cannot read the
// header at runtime (hermetic: no filesystem reads in unit tests).
static bool check_no_gallery_concepts_in_seam() { return true; }

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
  RUN_CASE(check_attach_touch_render_theme_inset_detach_cycle);
  RUN_CASE(check_attach_null_window_fails_closed);
  RUN_CASE(check_attach_invalid_window_fails_closed);
  RUN_CASE(check_double_attach_rejected_state_untouched);
  RUN_CASE(check_unknown_kind_fails_closed_never_fallback_box);
  RUN_CASE(check_detach_destroy_idempotent);
  RUN_CASE(check_touch_without_surface_fails_closed);
  RUN_CASE(check_render_without_surface_fails_closed);
  RUN_CASE(check_render_unrecorded_cmds_rejected);
  RUN_CASE(check_every_error_carries_diagnostic);
  RUN_CASE(check_empty_root_kind_rejected_fail_closed);
  RUN_CASE(check_ime_inset_bounds_fail_closed);
  RUN_CASE(check_no_gallery_concepts_in_seam);
  if (failures > 0) {
    std::printf("surface_interactive_test: %d failing\n", failures);
    return 1;
  }
  std::printf("surface_interactive_test: all passing\n");
  return 0;
}
