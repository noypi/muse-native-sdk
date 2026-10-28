# Device-lab: TalkBack procedure (TC-LISTS-032 / AC-LISTS-032)

Manual procedure for FR-LISTS-006. No callable code — human with TalkBack
on an Android API 29+ device, demo build containing the T11–T14
implementation. Record results in the run test report.

## Setup

- Install the demo build with a 340-item collection screen and a grid/span
  collection screen.
- Enable TalkBack (Settings → Accessibility → TalkBack).
- Open the 340-item collection screen.

## Steps (spec TC-LISTS-032 verbatim)

### 1. With TalkBack, scroll a 340-item collection by accessibility action and confirm it moves and announces "item X of Y".

- Focus the collection, invoke the TalkBack scroll-forward / scroll-backward
  accessibility action.
- **Pass:** the collection visibly moves AND TalkBack announces
  "item X of Y" with the new leading position and the total (e.g. item 21
  of 340).
- **Fail:** no movement, no announcement, or a wrong/stale X or Y.

Observation: ____ (PASS / FAIL, announced text: ____)

### 2. Fling fast and confirm announcements are coalesced instead of one per item.

- Fling fast through hundreds of items with TalkBack on.
- **Pass:** O(1) announcements — a single position announcement with the
  final position, not one announcement per item.
- **Fail:** a burst of per-item live-region announcements.

Observation: ____ (PASS / FAIL, announcements heard: ____)

### 3. Confirm grid/span items expose correct row/column/span.

- Open the grid/span collection screen with TalkBack on; move focus across
  regular and spanning items.
- **Pass:** each item exposes the correct row, column, row-span and
  column-span matching its visual placement.
- **Fail:** any item reports a wrong row/column/span.

Observation: ____ (PASS / FAIL, sampled items: ____)

### 4. Run the automated a11y audit on sampled collection screens.

- Run the automated accessibility audit (testing TEST-PYRAMID a11y layer)
  against sampled collection screens (list, grid/span, loading/error slots).
- **Pass:** the audit passes on all sampled screens.
- **Fail:** any audit violation on a sampled screen.

Observation: ____ (PASS / FAIL, audit output: ____)

## Depends on

T11 (scroll actions), T12 (item info), T13 (announce throttle), T14
(logical order) — all four unit suites must be green before this run.
