// T2 realized-window tests — bug-m3-fidelity-009.
// Exercises the plan T2 Interface: line:
//   ComputeRealizedWindow(int scrollOffsetPx, int viewportPx, int rowCount,
//                         float velocityPxS)
//     -> RealizedWindow { int firstVisible; int lastVisible;
//                         int prefetchAhead; }
// Pure, never throws; out-of-range clamps to [0, rowCount), never blank
// (RULE-RUNTIME-001). Hermetic: C++17 + committed runtime headers only;
// no threads, no clock, no GPU, no filesystem. Mirrors
// gallery_present_test.cpp conventions (anonymous namespace, RUN_CASE,
// main with printf).
#include <cstdio>
#include <string>

#include "runtime/gallery_present.h"

namespace {

// Row geometry in these tests is uniform: tests assert window *indices* only,
// never pixel internals, so any row-height model the coder picks satisfies
// them as long as clamping / no-blank / prefetch hold.
constexpr int kRowCount = 200;
constexpr int kViewportPx = 1600;

// Sustained-fling emission bound: one frame may cover at most the viewport
// rows plus prefetch headroom — never the full list (AC-LISTS-033,
// NFR-LISTS-002 worker-pool dispatch happens off the UI thread; the unit
// boundary asserted here is the realized range itself).
static bool check_window_at_rest_covers_viewport_head() {
  const runtime::RealizedWindow w =
      runtime::ComputeRealizedWindow(0, kViewportPx, kRowCount, 0.0f);
  if (w.firstVisible != 0) return false;
  if (w.lastVisible < w.firstVisible) return false;  // never blank
  if (w.lastVisible >= kRowCount) return false;
  if (w.prefetchAhead < 0) return false;
  return true;
}

static bool check_scrolled_window_advances_first_visible() {
  const runtime::RealizedWindow top =
      runtime::ComputeRealizedWindow(0, kViewportPx, kRowCount, 0.0f);
  const runtime::RealizedWindow down =
      runtime::ComputeRealizedWindow(3200, kViewportPx, kRowCount, 0.0f);
  if (down.firstVisible <= top.firstVisible) return false;
  if (down.lastVisible < down.firstVisible) return false;  // never blank
  if (down.lastVisible >= kRowCount) return false;
  return true;
}

static bool check_negative_offset_clamps_to_zero() {
  const runtime::RealizedWindow w =
      runtime::ComputeRealizedWindow(-5000, kViewportPx, kRowCount, 0.0f);
  if (w.firstVisible != 0) return false;
  if (w.lastVisible < w.firstVisible) return false;  // never blank
  return true;
}

static bool check_past_end_offset_clamps_never_blank() {
  const runtime::RealizedWindow w =
      runtime::ComputeRealizedWindow(1 << 30, kViewportPx, kRowCount, 0.0f);
  if (w.firstVisible < 0 || w.firstVisible >= kRowCount) return false;
  if (w.lastVisible != kRowCount - 1) return false;
  if (w.lastVisible < w.firstVisible) return false;  // never blank
  return true;
}

static bool check_viewport_larger_than_content_clamps() {
  const runtime::RealizedWindow w =
      runtime::ComputeRealizedWindow(0, 100000, kRowCount, 0.0f);
  if (w.firstVisible != 0) return false;
  if (w.lastVisible != kRowCount - 1) return false;
  return true;
}

static bool check_empty_list_never_throws_and_stays_empty() {
  try {
    const runtime::RealizedWindow w =
        runtime::ComputeRealizedWindow(0, kViewportPx, 0, 0.0f);
    if (w.lastVisible >= w.firstVisible && w.firstVisible == 0 &&
        w.lastVisible >= 0) {
      return false;  // must not claim visible rows when there are none
    }
    if (w.prefetchAhead < 0) return false;
  } catch (...) {
    return false;  // never throws
  }
  return true;
}

static bool check_fling_velocity_extends_prefetch_ahead() {
  const runtime::RealizedWindow rest =
      runtime::ComputeRealizedWindow(3200, kViewportPx, kRowCount, 0.0f);
  const runtime::RealizedWindow fling =
      runtime::ComputeRealizedWindow(3200, kViewportPx, kRowCount, 4000.0f);
  if (fling.prefetchAhead < rest.prefetchAhead) return false;
  if (fling.prefetchAhead < 0) return false;
  if (fling.firstVisible != rest.firstVisible) return false;
  if (fling.lastVisible < fling.firstVisible) return false;  // never blank
  if (fling.lastVisible >= kRowCount) return false;
  return true;
}

static bool check_sustained_fling_frames_stay_windowed() {
  // A sustained fling stepping through the whole list: every frame covers
  // only its realized window — frame row count stays a small fraction of
  // the list, so no full-list layout/paint can ride the UI thread.
  float offset = 0.0f;
  for (int frame = 0; frame < 120; ++frame) {
    offset += 133.0f;  // ~8000 px/s at 60 Hz vsync
    runtime::RealizedWindow w;
    try {
      w = runtime::ComputeRealizedWindow(static_cast<int>(offset),
                                         kViewportPx, kRowCount, 8000.0f);
    } catch (...) {
      return false;  // never throws, even mid-fling
    }
    if (w.lastVisible < w.firstVisible) return false;  // never blank
    if (w.firstVisible < 0 || w.lastVisible >= kRowCount) return false;
    const int frameRows = w.lastVisible - w.firstVisible + 1;
    if (frameRows <= 0 || frameRows >= kRowCount) return false;
  }
  return true;
}

static bool check_extreme_inputs_never_throw() {
  try {
    runtime::RealizedWindow a =
        runtime::ComputeRealizedWindow(-(1 << 30), -800, -5, 1e30f);
    (void)a;
    runtime::RealizedWindow b =
        runtime::ComputeRealizedWindow(1 << 30, 1 << 30, 1 << 30, -1e30f);
    (void)b;
  } catch (...) {
    return false;  // never throws
  }
  return true;
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
  RUN_CASE(check_window_at_rest_covers_viewport_head);
  RUN_CASE(check_scrolled_window_advances_first_visible);
  RUN_CASE(check_negative_offset_clamps_to_zero);
  RUN_CASE(check_past_end_offset_clamps_never_blank);
  RUN_CASE(check_viewport_larger_than_content_clamps);
  RUN_CASE(check_empty_list_never_throws_and_stays_empty);
  RUN_CASE(check_fling_velocity_extends_prefetch_ahead);
  RUN_CASE(check_sustained_fling_frames_stay_windowed);
  RUN_CASE(check_extreme_inputs_never_throw);
  if (failures > 0) {
    std::printf("realized_window_test: %d failing\n", failures);
    return 1;
  }
  std::printf("realized_window_test: all passing\n");
  return 0;
}
