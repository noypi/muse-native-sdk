# device-lab/motion-shared — manual procedure for TC-MOTION-037 (FR-MOTION-011 / AC-MOTION-037)

Owner: device lab (human operator). Method: manual. Status in spec table: missing.

Spec Verification column (verbatim — the procedure below executes exactly these steps):

> 1. Run a thumbnail-to-detail transition and a list-to-platform-view transition. 2. Interrupt each back gesture mid-flight and confirm continuous motion and reversal with no visible snap. 3. Deep-link into a nested destination whose source screen was never shown and confirm the shared element still matches.

Hermetic logic already proven (not re-proven here):
- `shared_identity_test.cpp` — stable-identity matching (AC-MOTION-034 half).
- `shared_clip_reverse_test.cpp` — clip/corner-radius continuity + clean reversal (AC-MOTION-035 half).
- `shared_pview_test.cpp` — surface-rect matching logic both directions (AC-MOTION-036 half).
- What those suites cannot observe (Q-MOTION-003): real gesture-compositor timing, sync-fence surface-rect delivery, and visible continuity on glass. That is what this script covers.

## Prerequisites

- Build with shared-element transitions enabled; thumbnail grid, detail screen, list-with-platform-view row, and nested deep-link destination all registered with the same stable shared-element id.
- A device (or emulator) with predictive-back / interruptible back gesture enabled.

## Procedure

### Step 1 — Thumbnail-to-detail + list-to-platform-view transitions

1. Navigate to the thumbnail grid and tap a thumbnail to open its detail screen.
2. Return to the grid, scroll to the list containing the platform-view row, and navigate into the platform-view destination.

Pass criteria (mirrors AC-MOTION-037):
- PASS: Both transitions animate (the shared element visibly moves/morphs from source rect to destination rect); no cut, no missing element, no fallback fade that replaces the shared transition.
- FAIL: Either transition cuts, drops the shared element, or substitutes a non-shared animation.

### Step 2 — Interrupted back gesture, continuous motion + reversal, no visible snap

1. From the detail screen, start the back gesture, drag to ~50% progress mid-flight, then reverse direction (drag back / release to cancel) so the transition returns to the detail screen.
2. Repeat the same interrupt-and-reverse on the list-to-platform-view transition.

Pass criteria (mirrors AC-MOTION-037):
- PASS: Motion stays continuous throughout the interrupt (position/clip track finger progress), reversal completes back to the start state, and no visible snap/jump occurs at the interrupt or reversal point.
- FAIL: Any freeze, jump, snap to an endpoint, or restarted-from-zero animation on interrupt or reversal.

### Step 3 — Deep-link into a nested destination with unseen source

1. Cold-start (or reset navigation state so the source grid/list screen was never shown), then deep-link directly into the nested destination that declares the shared element.
2. Confirm the shared element still matches (correct element, correct final rect/content).

Pass criteria (mirrors AC-MOTION-037):
- PASS: Shared element matches by stable identity despite the source screen never being shown; element appears in the correct place with correct content.
- FAIL: Element missing, mismatched (wrong content/id), or matched by stale tree position.

## Recording the result

Record per step PASS/FAIL with device model, OS build, and date in the run record. A FAIL is a device-lab finding routed to the planner — not a unit-test failure.
