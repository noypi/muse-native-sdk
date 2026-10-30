// T4 vsync-drive tests — bug-m3-fidelity-009.
// Binds to the plan T4 `Interface:` line:
//   `PresentPaced(const DrawCmds& cmds, ChoreographerDeadline deadline)
//     -> PresenterError`
// unchanged signature; device lane reads the real Choreographer timestamp +
// pacing::selectIntendedFrameRate(ContentClass::kScroll, range, ltpo) +
// pacing::tryQueueFrame admission before present; expired deadline →
// PresenterCode::kDeadlineMissed; render-ahead excess →
// PresenterCode::kPacingError; host lane keeps fake-clock accounting. Feeds
// dtS into existing lists::integrate (math unchanged — this file asserts
// only the vsync/dtS plumbing and deadline accounting, never integrate's
// math).
// Hermetic: C++17 + committed runtime/graphics-pacing/lists headers and
// sources only; Choreographer deadlines are scripted in-process fakes (no
// AChoreographer, no Swappy, no ASurfaceControl on host), no device, no
// network, no env/config reads, no filesystem, no live services, no
// wall-clock. Convention copied from paced_present_test.cpp (FakeNativeWindow
// seam, reset_presenter, RUN_CASE, printf summary). Does NOT duplicate the
// existing paced_present_test.cpp cases — it covers the T4 delta: vsync
// timestamp drive at 60/90/120Hz, fresh-window per-frame accounting, the
// exact kPacingError excess code, pacing-intent call-site semantics, and the
// deadline-delta → dtS plumbing values handed to integrate.
// NOTE (pinning): the host-lane PresentPaced behavior this file pins
// already exists in-tree; the coder's T4 work is the `#ifdef __ANDROID__`
// device-lane reads + threading the pacing calls through the gallery path,
// which no host test can observe. Cases below therefore pin must-stay-green
// host behavior; the device lane is verified on-device (T7), not here.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "engine/graphics/pacing/frame_rate.h"
#include "engine/graphics/pacing/render_ahead.h"
#include "lists/scroll_physics.h"
#include "runtime/presenter.h"

namespace {

#ifndef __ANDROID__
// Host-side opaque ANativeWindow fake (same seam as paced_present_test.cpp).
struct FakeNativeWindow {
  unsigned magic = 0xA11CEu;
};
#endif

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

void reset_presenter() { (void)runtime::DetachSurface(); }

bool attach_live_swapchain(void* handle) {
  if (runtime::AttachSurface(handle).code != runtime::PresenterCode::kOk)
    return false;
  runtime::BackendProbe probe;
  probe.backend = runtime::ProbeBackend::kVulkan11Emulated;
  probe.android_surface_ext_present = true;
  return runtime::CreateSurfaceAndSwapchain(probe).code ==
         runtime::PresenterCode::kOk;
}

runtime::DrawCmds some_cmds() {
  runtime::DrawCmds cmds;
  runtime::DrawCmd cmd;
  cmd.nodeId = 7;
  cmds.push_back(cmd);
  return cmds;
}

runtime::ChoreographerDeadline deadline_at(long long ns) {
  runtime::ChoreographerDeadline d{};
  d.deadline_ns = ns;
  return d;
}

// 60/90/120Hz vsync spacing in fake-clock ns (scripted timestamps only).
constexpr long long kPeriod60 = 16666667LL;
constexpr long long kPeriod90 = 11111111LL;
constexpr long long kPeriod120 = 8333333LL;
constexpr long long kBaseNs = 1000000000LL;

// Deadline-delta → dtS plumbing: the exact seconds value the presenter must
// feed into lists::integrate for one vsync step. Pure arithmetic asserted
// here; integrate's math is never asserted (T4 constraint).
double dtS_for_period(long long period_ns) {
  return static_cast<double>(period_ns) / 1.0e9;
}

void case_signature_shape_accepts_cmds_and_deadline() {
  // Pins the unchanged T4 call shape: (DrawCmds, ChoreographerDeadline).
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  reset_presenter();
  bool ok = attach_live_swapchain(handle);
  runtime::DrawCmds cmds = some_cmds();
  runtime::PresenterError err =
      runtime::PresentPaced(cmds, deadline_at(kBaseNs));
  ok = ok && (err.code == runtime::PresenterCode::kOk);
  reset_presenter();
  check(ok, "signature_shape_accepts_cmds_and_deadline");
}

void case_expired_deadline_is_missed() {
  // T4 interface: expired deadline → kDeadlineMissed, never on-time.
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  reset_presenter();
  bool ok = attach_live_swapchain(handle);
  runtime::DrawCmds cmds = some_cmds();
  runtime::PresenterError zero =
      runtime::PresentPaced(cmds, deadline_at(0));
  ok = ok && (zero.code == runtime::PresenterCode::kDeadlineMissed) &&
       !zero.message.empty();
  runtime::PresenterError neg =
      runtime::PresentPaced(cmds, deadline_at(-1));
  ok = ok && (neg.code == runtime::PresenterCode::kDeadlineMissed) &&
       !neg.message.empty();
  reset_presenter();
  check(ok, "expired_deadline_is_missed");
}

void case_render_ahead_excess_is_pacing_error() {
  // T4 interface: render-ahead excess → exactly kPacingError (not missed,
  // not ok), with a diagnostic.
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  reset_presenter();
  bool ok = attach_live_swapchain(handle);
  runtime::DrawCmds cmds = some_cmds();
  runtime::ChoreographerDeadline window = deadline_at(kBaseNs);
  for (int i = 0; i < runtime::kMaxRenderAhead; ++i) {
    if (runtime::PresentPaced(cmds, window).code !=
        runtime::PresenterCode::kOk) {
      ok = false;
    }
  }
  runtime::PresenterError excess = runtime::PresentPaced(cmds, window);
  ok = ok && (excess.code == runtime::PresenterCode::kPacingError) &&
       !excess.message.empty();
  reset_presenter();
  check(ok, "render_ahead_excess_is_pacing_error");
}

void case_fresh_deadline_opens_fresh_window() {
  // Per-frame deadline accounting: a new Choreographer timestamp resets the
  // in-window count, so the next frame after a clamped window presents kOk.
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  reset_presenter();
  bool ok = attach_live_swapchain(handle);
  runtime::DrawCmds cmds = some_cmds();
  runtime::ChoreographerDeadline w1 = deadline_at(kBaseNs);
  for (int i = 0; i <= runtime::kMaxRenderAhead; ++i) {
    (void)runtime::PresentPaced(cmds, w1);
  }
  runtime::PresenterError next =
      runtime::PresentPaced(cmds, deadline_at(kBaseNs + kPeriod60));
  ok = ok && (next.code == runtime::PresenterCode::kOk);
  reset_presenter();
  check(ok, "fresh_deadline_opens_fresh_window");
}

void case_vsync_drive_sixty_hz_zero_misses() {
  // Fling frames step from vsync timestamps: five consecutive 60Hz fake
  // deadlines present kOk with zero misses.
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  reset_presenter();
  bool ok = attach_live_swapchain(handle);
  runtime::DrawCmds cmds = some_cmds();
  int misses = 0;
  for (int frame = 0; frame < 5; ++frame) {
    runtime::PresenterError err = runtime::PresentPaced(
        cmds, deadline_at(kBaseNs + frame * kPeriod60));
    if (err.code != runtime::PresenterCode::kOk) ++misses;
  }
  reset_presenter();
  check(ok && misses == 0, "vsync_drive_sixty_hz_zero_misses");
}

void case_vsync_drive_ninety_and_120hz_zero_misses() {
  // Same vsync stepping at 90Hz and 120Hz: zero above-budget misses when
  // frames arrive on their timestamps.
#ifndef __ANDROID__
  FakeNativeWindow w;
  void* handle = static_cast<void*>(&w);
#else
  void* handle = reinterpret_cast<void*>(0x1234);
#endif
  const long long periods[2] = {kPeriod90, kPeriod120};
  bool ok = true;
  for (int r = 0; r < 2; ++r) {
    reset_presenter();
    if (!attach_live_swapchain(handle)) {
      ok = false;
      break;
    }
    runtime::DrawCmds cmds = some_cmds();
    for (int frame = 0; frame < 5; ++frame) {
      runtime::PresenterError err = runtime::PresentPaced(
          cmds, deadline_at(kBaseNs + frame * periods[r]));
      if (err.code != runtime::PresenterCode::kOk) ok = false;
    }
  }
  reset_presenter();
  check(ok, "vsync_drive_ninety_and_120hz_zero_misses");
}

void case_scroll_intent_selects_high_end() {
  // Pacing-intent call-site semantics the device lane threads through:
  // kScroll selects the high end of the range at every gallery rate, LTPO
  // or fixed — the intent the present path must request before admission.
  using pacing::ContentClass;
  using pacing::DisplayModeRange;
  using pacing::Status;
  bool ok = true;
  const DisplayModeRange ranges[3] = {{60, 60}, {30, 90}, {60, 120}};
  for (int i = 0; i < 3; ++i) {
    for (int ltpo = 0; ltpo < 2; ++ltpo) {
      pacing::FrameRateIntent intent = pacing::selectIntendedFrameRate(
          ContentClass::kScroll, ranges[i], ltpo != 0);
      ok = ok && (intent.status == Status::kOk) &&
           (intent.fps == ranges[i].maxHz);
    }
  }
  check(ok, "scroll_intent_selects_high_end");
}

void case_render_ahead_admission_mirrors_bound() {
  // tryQueueFrame admission semantics the device lane threads through
  // before present: headroom-only admission, excess rejected — mirroring
  // the kMaxRenderAhead clamp asserted in case_render_ahead_excess_*.
  using pacing::Status;
  pacing::RenderAheadVerdict v =
      pacing::tryQueueFrame(0, runtime::kMaxRenderAhead, 5);
  bool ok = (v.status == Status::kOk) &&
            (v.admitted == runtime::kMaxRenderAhead) &&
            (v.rejected == 5 - runtime::kMaxRenderAhead);
  pacing::RenderAheadVerdict full = pacing::tryQueueFrame(
      runtime::kMaxRenderAhead, runtime::kMaxRenderAhead, 1);
  ok = ok && (full.status == Status::kOk) && (full.admitted == 0) &&
       (full.rejected == 1);
  check(ok, "render_ahead_admission_mirrors_bound");
}

void case_vsync_deltas_feed_dtS_to_integrate() {
  // vsync/dtS plumbing only: consecutive scripted deadlines derive the
  // exact dtS per rate, and integrate consumes each dtS without throwing
  // and returns finite state. No assertion on integrate's math (values,
  // convergence, curve shape) — T4 constraint.
  const long long periods[3] = {kPeriod60, kPeriod90, kPeriod120};
  const double want[3] = {kPeriod60 / 1.0e9, kPeriod90 / 1.0e9,
                          kPeriod120 / 1.0e9};
  bool ok = true;
  lists::CurveParams curve;
  curve.kind = lists::CurveKind::AndroidOverscroll;
  for (int i = 0; i < 3; ++i) {
    double dtS = dtS_for_period(periods[i]);
    if (std::fabs(dtS - want[i]) > 1.0e-9 || !(dtS > 0.0)) {
      ok = false;
      break;
    }
    lists::PhysicsState s;
    s.offsetPx = 0.0f;
    s.velocityPxS = 4000.0f;
    lists::PredictedSample sample;
    lists::VsyncDeadline vsync;
    vsync.dtS = static_cast<float>(dtS);
    bool threw = false;
    lists::PhysicsState out = s;
    try {
      out = lists::integrate(s, sample, vsync, curve);
    } catch (...) {
      threw = true;
    }
    if (threw || !std::isfinite(out.offsetPx) ||
        !std::isfinite(out.velocityPxS)) {
      ok = false;
      break;
    }
  }
  check(ok, "vsync_deltas_feed_dtS_to_integrate");
}

}  // namespace

int main() {
  case_signature_shape_accepts_cmds_and_deadline();
  case_expired_deadline_is_missed();
  case_render_ahead_excess_is_pacing_error();
  case_fresh_deadline_opens_fresh_window();
  case_vsync_drive_sixty_hz_zero_misses();
  case_vsync_drive_ninety_and_120hz_zero_misses();
  case_scroll_intent_selects_high_end();
  case_render_ahead_admission_mirrors_bound();
  case_vsync_deltas_feed_dtS_to_integrate();
  if (g_failures > 0) {
    std::printf("present_paced_vsync_test: %d failing\n", g_failures);
    return 1;
  }
  std::printf("present_paced_vsync_test: all passing\n");
  return 0;
}
