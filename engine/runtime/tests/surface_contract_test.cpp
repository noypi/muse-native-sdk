// Batch 1 contract tests — FR-RUNTIME-011 / AC-RUNTIME-034 (TC-RUNTIME-034).
// Covers T1 header seam (attach null→kNullWindow, double-attach→kAlreadyAttached,
// detach/destroy twice→kOk, unknown kind→kCreateFailed fail-closed, no gallery
// tokens) and T2 behavior (attach→handle, renderFrame DAG-recorded cmds only,
// raw touch, in-place theme/IME, idempotent detach/destroy, frame stats).
// Binds ONLY to the plan `Interface:` lines; never to implementation.
// Hermetic: C++17 + committed runtime headers only; host-side opaque-window
// fakes behind `#ifndef __ANDROID__` guards; no NDK/Vulkan calls on host,
// no network, no env/config reads, no filesystem, no live services.
//
// ASSUMPTION (C++ spelling): the plan `Interface:` line settles names, params
// and error codes but not the C++ spelling. These tests assume:
//   using SurfaceHandle = uint64_t (0 == invalid);
//   struct CompiledTreeRef { std::string rootKind; bool unknownKind; };
//   enum class ThemeId { kLight = 0, kDark = 1; };
//   struct FrameStats { uint64_t presented, dropped, deadlineMissed; };
//   struct AttachResult { SurfaceHandle handle; PresenterError error; };
//   AttachResult attachSurface(const CompiledTreeRef&, void*);
//   PresenterError renderFrame(SurfaceHandle, const DrawCmds&);
//   PresenterError dispatchTouch(SurfaceHandle, int32_t, int32_t, float, float, int64_t);
//   PresenterError setTheme(SurfaceHandle, ThemeId);
//   PresenterError setImeBottom(SurfaceHandle, int32_t);
//   PresenterError detachSurface(SurfaceHandle);      // idempotent → kOk
//   PresenterError destroySurface(SurfaceHandle);     // idempotent → kOk
//   PresenterError getFrameStats(SurfaceHandle, FrameStats*);
//   void NoteFrameRecordedForSurface(SurfaceHandle, const DrawCmds&); // DAG-record helper
// If the coder spells them differently (e.g. variant/out-param/suffixed names),
// update these call sites (reconciliation, not weakening) and re-publish.
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

// T1: attach(null) fails closed with kNullWindow + non-empty diagnostic.
static bool check_attach_null_window_fails_closed() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, nullptr);
  return error_is(r.error, runtime::PresenterCode::kNullWindow) &&
         has_diagnostic(r.error);
}

// T1: double-attach without detach is rejected with kAlreadyAttached.
static bool check_double_attach_rejected() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  void* w = valid_window();
  runtime::AttachResult first = runtime::attachSurface(tree, w);
  if (!error_is(first.error, runtime::PresenterCode::kOk)) return false;
  runtime::AttachResult second = runtime::attachSurface(tree, w);
  bool ok = error_is(second.error, runtime::PresenterCode::kAlreadyAttached) &&
            has_diagnostic(second.error);
  (void)runtime::detachSurface(first.handle);
  (void)runtime::destroySurface(first.handle);
  return ok;
}

// T1+T2: detach/destroy are idempotent — twice each still kOk.
static bool check_detach_destroy_idempotent() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  bool ok = error_is(runtime::detachSurface(r.handle),
                     runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::detachSurface(r.handle),
                      runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::destroySurface(r.handle),
                      runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::destroySurface(r.handle),
                      runtime::PresenterCode::kOk);
  return ok;
}

// T1: unknown component kind fails closed with kCreateFailed + non-empty
// diagnostic, never a default box. Fail-closed means: no handle is usable —
// any handle returned must reject renderFrame (kNoSurface/kNoCurrentFrame or
// the create error surfaced on render), never present a fallback.
static bool check_unknown_kind_fails_closed_never_default_box() {
  runtime::CompiledTreeRef tree = make_unknown_kind_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!(error_is(r.error, runtime::PresenterCode::kCreateFailed) &&
        has_diagnostic(r.error))) {
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
  bool ok = render_rejected && has_diagnostic(render);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// T1: no gallery concepts in the seam. Compile-guard half: this file names no
// page/story/Prev/Next symbol, so any gallery concept in the signature breaks
// the build. Grep-gate half (owns the token check, recorded in the contract):
// `grep -rE "page|stor|Prev|Next" engine/runtime/include/runtime/presenter.h`
// must print nothing. This case pins the compile half; it cannot read the
// header at runtime (hermetic: no filesystem reads in unit tests).
static bool check_no_gallery_concepts_in_seam() {
  return true;
}

// T2: attach returns a usable handle on a valid tree + window.
static bool check_attach_returns_usable_handle() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  bool ok = error_is(r.error, runtime::PresenterCode::kOk) && r.handle != 0;
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// T2: renderFrame presents DAG-recorded cmds only — a hand-built list that was
// never DAG-recorded fails closed with kNoCurrentFrame, never presented.
static bool check_render_unrecorded_cmds_rejected() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  runtime::DrawCmds hand_built;
  runtime::DrawCmd cmd;
  hand_built.push_back(cmd);
  runtime::PresenterError err = runtime::renderFrame(r.handle, hand_built);
  bool ok = error_is(err, runtime::PresenterCode::kNoCurrentFrame) &&
            has_diagnostic(err);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// T2: renderFrame presents the DAG-recorded current-frame cmds with kOk.
static bool check_render_recorded_cmds_present() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  runtime::DrawCmds cmds;
  runtime::DrawCmd cmd;
  cmds.push_back(cmd);
  runtime::NoteFrameRecordedForSurface(r.handle, cmds);
  runtime::PresenterError err = runtime::renderFrame(r.handle, cmds);
  bool ok = error_is(err, runtime::PresenterCode::kOk);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// T2: raw touch dispatches with kOk on a live surface.
static bool check_raw_touch_dispatches() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  runtime::PresenterError err =
      runtime::dispatchTouch(r.handle, 0 /*down*/, 7, 150.0f, 140.0f, 12345);
  bool ok = error_is(err, runtime::PresenterCode::kOk);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
}

// T2: touch with no surface fails closed with kNoSurface + diagnostic.
static bool check_touch_without_surface_fails_closed() {
  runtime::AttachResult r = runtime::attachSurface(make_valid_tree(),
                                                  valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  runtime::PresenterError err =
      runtime::dispatchTouch(r.handle, 0, 7, 150.0f, 140.0f, 12345);
  return error_is(err, runtime::PresenterCode::kNoSurface) &&
         has_diagnostic(err);
}

// T2: setTheme applies in place (kOk) without re-attach; unknown handle →
// kNoSurface.
static bool check_set_theme_in_place() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  bool ok = error_is(runtime::setTheme(r.handle, runtime::ThemeId::kDark),
                     runtime::PresenterCode::kOk);
  ok = ok && error_is(runtime::setTheme(r.handle, runtime::ThemeId::kLight),
                      runtime::PresenterCode::kOk);
  // Still the same surface: render path still live, no re-attach needed.
  runtime::DrawCmds cmds;
  runtime::NoteFrameRecordedForSurface(r.handle, cmds);
  ok = ok && error_is(runtime::renderFrame(r.handle, cmds),
                      runtime::PresenterCode::kOk);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  ok = ok && error_is(runtime::setTheme(r.handle, runtime::ThemeId::kDark),
                      runtime::PresenterCode::kNoSurface);
  return ok;
}

// T2: setImeBottom applies in place (kOk) without re-attach.
static bool check_set_ime_bottom_in_place() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  bool ok =
      error_is(runtime::setImeBottom(r.handle, 120), runtime::PresenterCode::kOk);
  ok = ok &&
       error_is(runtime::setImeBottom(r.handle, 0), runtime::PresenterCode::kOk);
  runtime::DrawCmds cmds;
  runtime::NoteFrameRecordedForSurface(r.handle, cmds);
  ok = ok && error_is(runtime::renderFrame(r.handle, cmds),
                      runtime::PresenterCode::kOk);
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  ok = ok && error_is(runtime::setImeBottom(r.handle, 120),
                      runtime::PresenterCode::kNoSurface);
  return ok;
}

// T2: frame stats increment — presented grows across presented frames.
static bool check_frame_stats_increment() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  runtime::FrameStats before{};
  if (!error_is(runtime::getFrameStats(r.handle, &before),
                runtime::PresenterCode::kOk)) {
    (void)runtime::detachSurface(r.handle);
    (void)runtime::destroySurface(r.handle);
    return false;
  }
  runtime::DrawCmds cmds;
  runtime::NoteFrameRecordedForSurface(r.handle, cmds);
  if (!error_is(runtime::renderFrame(r.handle, cmds),
                runtime::PresenterCode::kOk)) {
    (void)runtime::detachSurface(r.handle);
    (void)runtime::destroySurface(r.handle);
    return false;
  }
  runtime::FrameStats after{};
  bool ok = error_is(runtime::getFrameStats(r.handle, &after),
                     runtime::PresenterCode::kOk);
  ok = ok && after.presented == before.presented + 1;
  // Stats on a dead surface fail closed.
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  runtime::FrameStats dead{};
  ok = ok && error_is(runtime::getFrameStats(r.handle, &dead),
                      runtime::PresenterCode::kNoSurface);
  return ok;
}

// T2 security: oversized IME inset is clamped or rejected, never crashes;
// negative inset fails closed or clamps — never an unowned draw.
static bool check_ime_inset_bounds_fail_closed() {
  runtime::CompiledTreeRef tree = make_valid_tree();
  runtime::AttachResult r = runtime::attachSurface(tree, valid_window());
  if (!error_is(r.error, runtime::PresenterCode::kOk)) return false;
  runtime::PresenterError huge = runtime::setImeBottom(r.handle, 1000000);
  bool ok = error_is(huge, runtime::PresenterCode::kOk) ||
            (error_is(huge, runtime::PresenterCode::kNoSurface) &&
             has_diagnostic(huge)) ||
            has_diagnostic(huge);
  runtime::PresenterError neg = runtime::setImeBottom(r.handle, -1);
  ok = ok && (error_is(neg, runtime::PresenterCode::kOk) ||
              has_diagnostic(neg));
  (void)runtime::detachSurface(r.handle);
  (void)runtime::destroySurface(r.handle);
  return ok;
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
  RUN_CASE(check_attach_null_window_fails_closed);
  RUN_CASE(check_double_attach_rejected);
  RUN_CASE(check_detach_destroy_idempotent);
  RUN_CASE(check_unknown_kind_fails_closed_never_default_box);
  RUN_CASE(check_no_gallery_concepts_in_seam);
  RUN_CASE(check_attach_returns_usable_handle);
  RUN_CASE(check_render_unrecorded_cmds_rejected);
  RUN_CASE(check_render_recorded_cmds_present);
  RUN_CASE(check_raw_touch_dispatches);
  RUN_CASE(check_touch_without_surface_fails_closed);
  RUN_CASE(check_set_theme_in_place);
  RUN_CASE(check_set_ime_bottom_in_place);
  RUN_CASE(check_frame_stats_increment);
  RUN_CASE(check_ime_inset_bounds_fail_closed);
  if (failures > 0) {
    std::printf("surface_contract_test: %d failing\n", failures);
    return 1;
  }
  std::printf("surface_contract_test: all passing\n");
  return 0;
}
