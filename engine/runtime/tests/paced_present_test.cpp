// T5 contract tests — FR-RUNTIME-009 / AC-RUNTIME-031 (manual TC-031).
// Binds ONLY to the T5 `Interface:` line; never to implementation.
// Hermetic: C++17 + committed runtime headers only; host-side ANativeWindow
// fakes behind `#ifndef __ANDROID__` guards; choreographer deadlines are
// in-process fakes (no AChoreographer, no Swappy, no ASurfaceControl on host),
// no network, no env/config reads, no filesystem, no live services.
//
// Unit scope: the render-ahead bound clamp (kMaxRenderAhead), deadline
// plumbing (fresh vs expired), and fail-closed pacing errors
// (kDeadlineMissed / kPacingError + diagnostic). The kOk visible-frames path
// is proven manually via examples/m3-gallery TC-031 (see unit-tests/T5.md).
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "runtime/presenter.h"

// --- harness cases (convention copied from host_lifecycle_test.cpp) ----------

static bool check_harness_entry_reached() { return true; }

static bool check_all_green_returns_zero() {
  const int greenFailures = 0;
  return greenFailures == 0;
}

static bool check_failure_protocol_shape() {
  const char* failingName = "";
  return failingName != nullptr && std::strlen(failingName) == 0;
}

// --- T5 helpers --------------------------------------------------------------

namespace {

#ifndef __ANDROID__
// Host-side opaque ANativeWindow fake (same seam as T1/T2/T3/T4). The presenter
// must never dereference the handle as a View.
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

bool returns_only_known_codes(const runtime::PresenterError& err) {
  return err.code == runtime::PresenterCode::kOk ||
         err.code == runtime::PresenterCode::kDeadlineMissed ||
         err.code == runtime::PresenterCode::kPacingError;
}

bool attach_valid_window() {
#ifndef __ANDROID__
  FakeNativeWindow w;
  runtime::PresenterError attach =
      runtime::AttachSurface(static_cast<void*>(&w));
#else
  runtime::PresenterError attach =
      runtime::AttachSurface(reinterpret_cast<void*>(0x1234));
#endif
  return error_is(attach, runtime::PresenterCode::kOk);
}

bool create_swapchain_for_paced() {
  runtime::BackendProbe p;
  p.backend = runtime::ProbeBackend::kVulkan11Emulated;
  p.android_surface_ext_present = true;
  return error_is(runtime::CreateSurfaceAndSwapchain(p),
                  runtime::PresenterCode::kOk);
}

bool setup_live_swapchain() {
  return attach_valid_window() && create_swapchain_for_paced();
}

// Choreographer-deadline fakes (host lane; non-Android builds use a
// choreographer-deadline fake per the T5 interface note). The fake clock is
// explicit nanosecond values — the OS clock is never read.
// ASSUMPTION: `runtime::ChoreographerDeadline` carries a `deadline_ns`
// field (vsync deadline, ns); the coder owns the exact shape.
runtime::ChoreographerDeadline fresh_deadline() {
  runtime::ChoreographerDeadline d{};
  d.deadline_ns = static_cast<long long>(INT64_C(0x7FFFFFFFFFFFF000));
  return d;
}

runtime::ChoreographerDeadline expired_deadline() {
  runtime::ChoreographerDeadline d{};
  d.deadline_ns = 0;
  return d;
}

runtime::DrawCmds some_cmds() {
  runtime::DrawCmds cmds;
  runtime::DrawCmd cmd;
  cmd.nodeId = 7;
  cmds.push_back(cmd);
  return cmds;
}

}  // namespace

// Named case: PresentPaced with no swapchain fails closed with kPacingError
// + diagnostic — nothing is presented without a live surface.
static bool check_paced_without_swapchain_fails_closed() {
  reset_presenter();
  runtime::DrawCmds cmds = some_cmds();
  runtime::PresenterError err =
      runtime::PresentPaced(cmds, fresh_deadline());
  bool ok = error_is(err, runtime::PresenterCode::kPacingError) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: PresentPaced against an already-expired choreographer deadline
// fails closed with kDeadlineMissed + diagnostic — a missed vsync is never
// presented as on-time.
static bool check_paced_missed_deadline_fails_closed() {
  reset_presenter();
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::DrawCmds cmds = some_cmds();
  runtime::PresenterError err =
      runtime::PresentPaced(cmds, expired_deadline());
  bool ok = error_is(err, runtime::PresenterCode::kDeadlineMissed) &&
            has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: PresentPaced honors the kMaxRenderAhead bound imported from the
// FR-GRAPHICS-007 pacing policy — issuing kMaxRenderAhead + 1 presents inside
// one deadline window never yields all-kOk; the excess is clamped
// (kDeadlineMissed or kPacingError), so no unbounded render-ahead occurs.
static bool check_paced_render_ahead_bound_honored() {
  reset_presenter();
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::DrawCmds cmds = some_cmds();
  runtime::ChoreographerDeadline window = fresh_deadline();
  bool saw_clamp = false;
  for (int i = 0; i <= runtime::kMaxRenderAhead; ++i) {
    runtime::PresenterError err = runtime::PresentPaced(cmds, window);
    if (!returns_only_known_codes(err)) {
      reset_presenter();
      return false;
    }
    if (err.code != runtime::PresenterCode::kOk) {
      if (!has_diagnostic(err)) {
        reset_presenter();
        return false;
      }
      saw_clamp = true;
    }
  }
  reset_presenter();
  return saw_clamp;
}

// Named case: every PresentPaced error carries a diagnostic — kDeadlineMissed
// and kPacingError are never silent.
static bool check_paced_errors_carry_diagnostics() {
  reset_presenter();
  runtime::DrawCmds cmds = some_cmds();
  runtime::PresenterError no_surface =
      runtime::PresentPaced(cmds, fresh_deadline());
  bool ok = error_is(no_surface, runtime::PresenterCode::kPacingError) &&
            has_diagnostic(no_surface);
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError missed =
      runtime::PresentPaced(cmds, expired_deadline());
  ok = ok && error_is(missed, runtime::PresenterCode::kDeadlineMissed) &&
       has_diagnostic(missed);
  reset_presenter();
  return ok;
}

// Named case: PresentPaced returns only the interface-named codes
// (kOk | kDeadlineMissed | kPacingError) — no unknown code escapes, and the
// single-call shape never crashes on a fresh deadline.
static bool check_paced_return_set_is_closed() {
  reset_presenter();
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::DrawCmds cmds = some_cmds();
  runtime::PresenterError err =
      runtime::PresentPaced(cmds, fresh_deadline());
  bool ok = returns_only_known_codes(err);
  if (err.code != runtime::PresenterCode::kOk) ok = ok && has_diagnostic(err);
  reset_presenter();
  return ok;
}

// Named case: PresentPaced after the swapchain is gone (destroyed surface)
// fails closed with kPacingError + diagnostic — no dangling-surface present
// on a stale deadline (depends on T4 OnSurfaceDestroyed).
static bool check_paced_after_destroy_fails_closed() {
  reset_presenter();
  if (!setup_live_swapchain()) {
    reset_presenter();
    return false;
  }
  runtime::PresenterError destroyed = runtime::OnSurfaceDestroyed();
  if (!error_is(destroyed, runtime::PresenterCode::kOk)) {
    reset_presenter();
    return false;
  }
  runtime::DrawCmds cmds = some_cmds();
  runtime::PresenterError err =
      runtime::PresentPaced(cmds, fresh_deadline());
  bool ok = error_is(err, runtime::PresenterCode::kPacingError) &&
            has_diagnostic(err);
  reset_presenter();
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
  RUN_CASE(check_paced_without_swapchain_fails_closed);
  RUN_CASE(check_paced_missed_deadline_fails_closed);
  RUN_CASE(check_paced_render_ahead_bound_honored);
  RUN_CASE(check_paced_errors_carry_diagnostics);
  RUN_CASE(check_paced_return_set_is_closed);
  RUN_CASE(check_paced_after_destroy_fails_closed);
  if (failures > 0) {
    std::printf("paced_present_test: %d failing\n", failures);
    return 1;
  }
  std::printf("paced_present_test: all passing\n");
  return 0;
}
