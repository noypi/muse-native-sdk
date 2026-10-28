// T6 contract tests — FR-LISTS-002 / AC-LISTS-007 (TC-LISTS-007).
// Hermetic: C++17 + lists headers only; no network, no env/config reads,
// no filesystem, no live services, no wall-clock sleeps. Rate steps are
// injected as float Hz arguments plus VsyncDeadline values; LTPO jitter is
// a fixed array of injected dt values, never measured time.
// Binds to the T6 `Interface:` line:
//   `PhysicsState onRateChange(PhysicsState s, float oldHz, float newHz,
//    VsyncDeadline vsync);`
//   — identical offsetPx+velocityPxS, re-based dt, never resets to rest.
// Include path follows the engine convention:
//   #include "lists/refresh_rate.h" with engine/lists/include on the path.
// Consumes the T2 physics header for the shared PhysicsState/VsyncDeadline
// vocabulary (no redefinition): the test proposes that refresh_rate.h
// reuses `lists::PhysicsState` / `lists::VsyncDeadline` from
// lists/scroll_physics.h. If the coder spells the state type differently,
// update these call sites — not the expected behavior (ASSUMPTION).
//
// ASSUMPTION (vsync role): the Interface line does not settle what vsync
// carries into onRateChange. This test treats it as the new-rate frame
// step already injected (`VsyncDeadline{.dtS = 1/newHz}`); the behavior
// asserted is only offset/velocity preservation, which holds regardless.
#include <cmath>
#include <cstdio>

#include "lists/refresh_rate.h"
#include "lists/scroll_physics.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
  if (ok) {
    std::printf("PASS %s\n", name);
  } else {
    std::printf("FAIL %s\n", name);
    ++g_failures;
  }
}

lists::VsyncDeadline vsyncFor(float hz) {
  lists::VsyncDeadline v{};
  v.dtS = 1.0f / hz;
  return v;
}

lists::PhysicsState midFling() {
  lists::PhysicsState s{};
  s.offsetPx = 1234.0f;
  s.velocityPxS = 3500.0f;
  return s;
}

}  // namespace

int main() {
  // Single switch preserves offset and velocity exactly: no jump.
  {
    lists::PhysicsState out =
        lists::onRateChange(midFling(), 60.0f, 120.0f, vsyncFor(120.0f));
    check(out.offsetPx == 1234.0f, "rate_60_120_keeps_offset");
    check(out.velocityPxS == 3500.0f, "rate_60_120_keeps_velocity");
  }

  // Full chain from the acceptance criterion: mid-fling 60→120→144→90.
  {
    lists::PhysicsState s = midFling();
    s = lists::onRateChange(s, 60.0f, 120.0f, vsyncFor(120.0f));
    check(s.offsetPx == 1234.0f && s.velocityPxS == 3500.0f,
          "chain_step_60_120");
    s = lists::onRateChange(s, 120.0f, 144.0f, vsyncFor(144.0f));
    check(s.offsetPx == 1234.0f && s.velocityPxS == 3500.0f,
          "chain_step_120_144");
    s = lists::onRateChange(s, 144.0f, 90.0f, vsyncFor(90.0f));
    check(s.offsetPx == 1234.0f && s.velocityPxS == 3500.0f,
          "chain_step_144_90");
  }

  // LTPO jitter: a burst of variable steps preserves offset/velocity.
  {
    lists::PhysicsState s = midFling();
    const float kJitterHz[] = {120.0f, 90.0f,  144.0f, 60.0f,
                               120.0f, 144.0f, 90.0f,   120.0f};
    float prev = 60.0f;
    for (float hz : kJitterHz) {
      s = lists::onRateChange(s, prev, hz, vsyncFor(hz));
      prev = hz;
    }
    check(s.offsetPx == 1234.0f, "ltpo_jitter_keeps_offset");
    check(s.velocityPxS == 3500.0f, "ltpo_jitter_keeps_velocity");
  }

  // Never resets to rest: a live fling keeps its velocity.
  {
    lists::PhysicsState out =
        lists::onRateChange(midFling(), 120.0f, 90.0f, vsyncFor(90.0f));
    check(out.velocityPxS != 0.0f, "rate_change_never_rests");
    check(std::fabs(out.velocityPxS) == std::fabs(3500.0f),
          "rate_change_keeps_speed");
  }

  // Rest state survives a rate switch unchanged too (no phantom motion).
  {
    lists::PhysicsState rest{};
    rest.offsetPx = 500.0f;
    rest.velocityPxS = 0.0f;
    lists::PhysicsState out =
        lists::onRateChange(rest, 60.0f, 144.0f, vsyncFor(144.0f));
    check(out.offsetPx == 500.0f, "rest_keeps_offset");
    check(out.velocityPxS == 0.0f, "rest_keeps_zero_velocity");
  }

  // Identity switch (same rate) is a no-op.
  {
    lists::PhysicsState out =
        lists::onRateChange(midFling(), 120.0f, 120.0f, vsyncFor(120.0f));
    check(out.offsetPx == 1234.0f, "same_rate_keeps_offset");
    check(out.velocityPxS == 3500.0f, "same_rate_keeps_velocity");
  }

  // Boundary: negative (overscroll rebound) velocity is preserved as-is.
  {
    lists::PhysicsState s{};
    s.offsetPx = 80.0f;
    s.velocityPxS = -1500.0f;
    lists::PhysicsState out =
        lists::onRateChange(s, 90.0f, 144.0f, vsyncFor(144.0f));
    check(out.velocityPxS == -1500.0f, "negative_velocity_preserved");
    check(out.offsetPx == 80.0f, "negative_velocity_offset_kept");
  }

  if (g_failures == 0) std::printf("ALL TESTS PASSED\n");
  return g_failures == 0 ? 0 : 1;
}
