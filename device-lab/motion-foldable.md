# device-lab/motion-foldable — external procedure for TC-MOTION-041 (FR-MOTION-012 / AC-MOTION-041)

Owner: device lab (human operator). Method: external. Status in spec table: external.

NOTE: TC-MOTION-041's `external` status is expected-by-construction. This
procedure can only be observed on a foldable test profile / foldable hardware
(posture signals, hinge occlusion, real frame budget) and is therefore
UNVERIFIABLE by hermetic unit tests. `external` is the correct terminal status,
not a gap — the same class as TC-MOTION-013 (`device-lab/motion-120hz`).

Hermetic logic already proven (not re-proven here):
- `hinge_motion_test.cpp` — no animation crosses a FULL occlusion hinge; tabletop uses pane-move variants (AC-MOTION-038 half).
- `size_scaled_motion_test.cpp` — duration/distance scale monotonically with size class within token bounds (AC-MOTION-039 half).
- `container_transform_test.cpp` — large-grid path emits one container box; children untouched/count unchanged (AC-MOTION-040 half).
- What those suites cannot observe: real posture transitions mid-flight and real frame timing on glass. That is what this script covers.

## Prerequisites

- Foldable test profile (per [testing](specs-testing.md) foldable device profiles) or foldable hardware exposing posture signals (flat / tabletop / half-opened) and hinge occlusion boundary from [layout](specs-layout-styling.md) (FoldingFeature/WindowManager).
- An expanded-width window configuration for the large-grid check.

## Procedure

### Step 1 — Fold to tabletop during a navigation transition

1. Start a navigation transition on the unfolded (flat) posture.
2. Mid-flight, fold the device to tabletop posture.

Pass criteria (mirrors AC-MOTION-041):
- PASS: The transition completes as a pane-appropriate, hinge-respecting animation — content moves between panes rather than scaling across the hinge; no animated content crosses the hinge occlusion boundary.
- FAIL: Any content animates across the hinge, or the transition keeps the flat-posture full-span variant after the fold.

### Step 2 — Unfold during a navigation transition

1. Start a navigation transition in tabletop posture.
2. Mid-flight, unfold back to flat.

Pass criteria (mirrors AC-MOTION-041):
- PASS: The transition completes as a pane-appropriate, hinge-respecting animation for the new posture; no content crosses the hinge occlusion boundary during the posture change.
- FAIL: Any content crosses the hinge, or the animation freezes/restarts instead of continuing into the posture-appropriate variant.

### Step 3 — Large-grid transition on expanded-width window (frame budget + single container transform)

1. On an expanded-width window, trigger a large-grid transition.
2. Observe frame timing and the animated layer structure.

Pass criteria (mirrors AC-MOTION-041):
- PASS: The transition stays within frame budget (no sustained jank/dropped-frame run visible in the lab's frame-time capture) AND animates as a single container transform (one moving container; grid children do not animate individually).
- FAIL: Frame budget exceeded (sustained jank) OR per-child animation observed instead of one container transform.

## Recording the result

Record per step PASS/FAIL with device profile/model, OS build, window size class, and date in the run record. A FAIL is a device-lab finding routed to the planner — not a unit-test failure. The `external` Method/Status stands regardless of outcome.
