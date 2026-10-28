#pragma once

#include "lists/scroll_physics.h"

namespace lists {

// Refresh-rate / config-change handoff (FR-LISTS-002, AC-LISTS-007).
// Consumes the FR-LIFECYCLE-006 zero-restart config-change notification as
// the trigger: the caller forwards the old/new rates plus the new-rate
// VSYNC step, and the fling continues without a jump or restart.
//
// Returns `s` with identical `offsetPx` and `velocityPxS`. The time step is
// re-based implicitly — subsequent `integrate` calls consume the new
// `VsyncDeadline` (`dtS = 1/newHz`), so no per-state scaling is needed and
// the perceived fling distance is unchanged. Never resets to rest: a live
// fling keeps its velocity (including negative overscroll-rebound
// velocity), a rest state keeps zero velocity (no phantom motion), and an
// identity switch (oldHz == newHz) is a no-op.
PhysicsState onRateChange(PhysicsState s, float oldHz, float newHz,
                          VsyncDeadline vsync);

}  // namespace lists
