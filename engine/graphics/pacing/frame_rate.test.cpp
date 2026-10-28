// T1 hermetic unit test — intended frame-rate selection (FR-GRAPHICS-007, AC-GRAPHICS-017).
// Style: plain asserts, no dependencies, no I/O, no network.
#include <cassert>
#include <cstdio>

#include "engine/graphics/pacing/frame_rate.h"

int main() {
  using pacing::ContentClass;
  using pacing::DisplayModeRange;
  using pacing::Status;

  // 1. Idle + LTPO selects the low end of the range.
  {
    DisplayModeRange range{1, 120};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kIdle, range, true);
    assert(r.status == Status::kOk);
    assert(r.fps == 1);
    assert(r.lowPowerIdle == true);
    printf("PASS idle-ltpo-selects-min\n");
  }
  // 2. Scroll selects the high end.
  {
    DisplayModeRange range{1, 120};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kScroll, range, true);
    assert(r.status == Status::kOk);
    assert(r.fps == 120);
    printf("PASS scroll-selects-max\n");
  }
  // 3. Animation selects the high end.
  {
    DisplayModeRange range{30, 90};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kAnimation, range, true);
    assert(r.status == Status::kOk);
    assert(r.fps == 90);
    printf("PASS animation-selects-max\n");
  }
  // 4. Video selects the high end.
  {
    DisplayModeRange range{30, 90};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kVideo, range, false);
    assert(r.status == Status::kOk);
    assert(r.fps == 90);
    printf("PASS video-selects-max\n");
  }
  // 5. Fixed (non-LTPO) panel clamps idle to maxHz.
  {
    DisplayModeRange range{60, 60};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kIdle, range, false);
    assert(r.status == Status::kOk);
    assert(r.fps == 60);
    printf("PASS fixed-panel-idle-clamps-max\n");
  }
  // 6. Per-surface: two surfaces with different content classes differ.
  {
    DisplayModeRange range{1, 120};
    pacing::FrameRateIntent idle =
        pacing::selectIntendedFrameRate(ContentClass::kIdle, range, true);
    pacing::FrameRateIntent scroll =
        pacing::selectIntendedFrameRate(ContentClass::kScroll, range, true);
    assert(idle.status == Status::kOk && scroll.status == Status::kOk);
    assert(idle.fps != scroll.fps);
    printf("PASS per-surface-intent-differs\n");
  }
  // 7. Security: minHz<=0 yields kInvalidArg with fps=0.
  {
    DisplayModeRange range{0, 120};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kScroll, range, true);
    assert(r.status == Status::kInvalidArg);
    assert(r.fps == 0);
    printf("PASS zero-min-invalid-arg\n");
  }
  // 8. Security: maxHz<minHz yields kInvalidArg with fps=0.
  {
    DisplayModeRange range{120, 60};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kIdle, range, true);
    assert(r.status == Status::kInvalidArg);
    assert(r.fps == 0);
    printf("PASS inverted-range-invalid-arg\n");
  }
  // 9. Security: negative minHz yields kInvalidArg with fps=0.
  {
    DisplayModeRange range{-30, 120};
    pacing::FrameRateIntent r =
        pacing::selectIntendedFrameRate(ContentClass::kVideo, range, true);
    assert(r.status == Status::kInvalidArg);
    assert(r.fps == 0);
    printf("PASS negative-min-invalid-arg\n");
  }
  printf("ALL T1 TESTS PASSED\n");
  return 0;
}
