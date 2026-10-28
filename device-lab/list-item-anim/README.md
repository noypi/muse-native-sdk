# Device Lab — List Item Animations (TC-LISTS-022)

Manual procedure for FR-LISTS-004 item animations on a physical Android
device (API 29+). No callable code — human observation only.

Scope: AC-LISTS-022 / TC-LISTS-022. Depends on T1 (animator), T2
(drag-reorder + auto-scroll), T3 (swipe + undo), T4 (selection) behaviour
being present in the build under test.

## Setup

1. Install the `10k-item` demo build on the device.
2. Open the list screen; wait for the initial 10,000 rows to settle
   (no loading spinner, fling rests at the top).
3. Have a stopwatch and this checklist ready. Record each step PASS/FAIL
   with the observed behaviour in the run test report.

## Steps

### Step 1 — remove-100-of-10k animates with no unchanged rebind

1. Trigger the "delete 100" action (removes 100 consecutive rows from the
   middle of the 10,000-item list).
2. OBSERVE: the removed rows animate out (fade/slide) and neighbours
   slide into place; no full-list flash or jump.
3. PASS if: removals and neighbouring moves animate smoothly and the
   remaining rows keep their content/state (unchanged items not rebound —
   e.g. toggles/checkboxes in visible rows keep their values).
   FAIL otherwise.

### Step 2 — drag-to-top auto-scrolls stably

1. Long-press a row near the bottom until it lifts (drag handle engaged).
2. Drag it to the top edge and hold inside the edge auto-scroll zone.
3. OBSERVE: the list auto-scrolls toward the top and keeps scrolling
   stably while held in the zone; the dragged row stays under the finger.
4. Drop the row at the top.
5. PASS if: auto-scroll runs stably to the top, drop commits exactly one
   reorder (row lands where dropped, no duplicates, no snap-back).
   FAIL otherwise.

### Step 3 — cancelled swipe leaves the item clean

1. Swipe a row part-way (below the dismiss threshold) and release slowly.
2. OBSERVE: the row springs back to its resting position.
3. PASS if: the item stays in place with no visual residue (no offset,
   no background peek, no stuck elevation).
   FAIL otherwise.

### Step 4 — undo restores the original index

1. Swipe a row fully past the threshold to dismiss it; the undo bar
   appears.
2. Tap UNDO inside the undo window.
3. OBSERVE: the item returns to the list.
4. PASS if: the item is restored at its original index with its prior
   content/state intact.
   FAIL otherwise.

## Recording results

For each step record: PASS/FAIL, device model + API level, build id, and
one line describing what was observed. File failures as bug reports
against AC-LISTS-022.
