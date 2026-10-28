# Component motion token map (FR-MOTION-015 / AC-MOTION-048)

- Matrix reference: `manual-context.local/specs-m3-native.md` — SPEC-M3-NATIVE **v1.10**,
  Status active, Updated **2026-10-28** (FR-M3-001, read-only completeness reference).
- Token set reference: `engine/motion/src/tokens.cpp` table (T1, scheme version `1`):
  `duration-short1` (50ms), `duration-short2` (100ms), `duration-medium1` (200ms),
  `duration-medium2` (300ms), `duration-long1` (400ms), `duration-long2` (500ms),
  `duration-long3` (700ms), `duration-long4` (900ms); `easing-standard`,
  `easing-emphasized`, `easing-accelerate`, `easing-decelerate`; `spatial-default`
  (spatial spring, geometry only), `effects-default` (effects spring, opacity/colour only).
- Rule: every token named below MUST resolve via T1 `resolveToken`; any matrix component
  with no meaningful motion carries an explicit `none (static)` row with a reason —
  a missing row is a failure (AC-MOTION-049), never a silent default.
- State-change vocabulary (per FR-MOTION-015 Notes): pressed, selected, expanded,
  entered, dismissed. `expanded` covers expand/collapse/open/close of collapsible
  surfaces; `entered`/`dismissed` cover appear/disappear of transient surfaces.

| Component | State change | Token(s) | Notes |
|-----------|--------------|----------|-------|
| Surface | pressed | `duration-short1`, `easing-standard` | state-layer ripple via shared Ripple primitive |
| Surface | selected | `duration-short2`, `easing-standard` | tonal selection feedback |
| Text | — | `none (static)` | no state change; purely presentational glyph run |
| Icon | pressed | `duration-short1`, `easing-standard` | state-layer only when interactive (IconButton content) |
| Icon | — | `none (static)` | non-interactive icon has no motion of its own |
| Image | entered | `duration-medium1`, `easing-decelerate` | fade-in on load; placeholder cross-fade |
| Ripple/state layer | pressed | `duration-short1`, `easing-standard` | canonical press ripple; geometry-anchored, no spring |
| Divider | — | `none (static)` | no state change; purely presentational rule |
| Scroll | expanded | `duration-medium1`, `easing-standard`, `spatial-default` | fling/settle of scroll offset (geometry) |
| Scroll | dismissed | `duration-short2`, `easing-accelerate` | overscroll edge-effect release |
| Stack/Flex | — | `none (static)` | layout container only; child motion owned by child rows |
| Text-field edit core | pressed | `duration-short1`, `easing-standard` | focus/caret state layer |
| Text-field edit core | entered | `duration-short2`, `easing-decelerate` | caret/composition appearance |
| OverlayHost | entered | `duration-medium1`, `easing-emphasized` | overlay surface layering above content |
| OverlayHost | dismissed | `duration-medium1`, `easing-accelerate` | overlay surface removal |
| Text input | pressed | `duration-short1`, `easing-standard` | focus ring + state layer |
| Text input | entered | `duration-short2`, `easing-decelerate` | IME inset follow is layout-owned; field fade/slide only |
| Button (all variants) | pressed | `duration-short1`, `easing-standard` | ripple + elevation delta |
| Button (all variants) | selected | `duration-short2`, `easing-standard` | tonal/filled selection swap |
| IconButton | pressed | `duration-short1`, `easing-standard` | ripple; selection same row as Button when toggleable |
| IconButton | selected | `duration-short2`, `easing-standard` | toggle IconButton selected morph |
| FAB | pressed | `duration-short1`, `easing-standard` | ripple + elevation lift |
| FAB | entered | `duration-medium2`, `easing-emphasized`, `spatial-default` | expand/collapse scale+geometry morph |
| FAB | dismissed | `duration-medium1`, `easing-accelerate`, `spatial-default` | shrink-out on scroll-away |
| Extended FAB | pressed | `duration-short1`, `easing-standard` | ripple + elevation lift |
| Extended FAB | expanded | `duration-medium2`, `easing-emphasized`, `spatial-default` | label expand/collapse width morph |
| Extended FAB | dismissed | `duration-medium1`, `easing-accelerate`, `spatial-default` | shrink-out on scroll-away |
| Card (all variants) | pressed | `duration-short1`, `easing-standard` | state layer when clickable |
| Card (all variants) | selected | `duration-short2`, `easing-standard` | selection outline/tonal swap |
| Card (all variants) | entered | `duration-medium1`, `easing-decelerate` | container-transform entry (reuses T3 box path) |
| Chip (all variants) | pressed | `duration-short1`, `easing-standard` | ripple |
| Chip (all variants) | selected | `duration-short2`, `easing-standard` | filter/input checkmark + tonal swap |
| Chip (all variants) | dismissed | `duration-short2`, `easing-accelerate` | input-chip close-icon removal |
| Checkbox | pressed | `duration-short1`, `easing-standard` | state layer |
| Checkbox | selected | `duration-short2`, `easing-standard` | check draw-on/off (effects) |
| Radio | pressed | `duration-short1`, `easing-standard` | state layer |
| Radio | selected | `duration-short2`, `easing-standard` | dot scale-in (effects) |
| Switch | pressed | `duration-short1`, `easing-standard` | state layer |
| Switch | selected | `duration-medium1`, `easing-emphasized`, `effects-default` | thumb travel; effects-class spring (colour/opacity), never geometry |
| Slider | pressed | `duration-short1`, `easing-standard` | state layer on thumb press |
| Slider | selected | `duration-short2`, `easing-standard`, `spatial-default` | value-change thumb/track travel (geometry) |
| Badge | entered | `duration-short2`, `easing-decelerate` | count-appear scale/fade |
| Badge | dismissed | `duration-short2`, `easing-accelerate` | count-clear shrink-out |
| LinearProgress | entered | `duration-long2`, `easing-standard` | indeterminate sweep loop while active |
| LinearProgress | dismissed | `duration-short2`, `easing-accelerate` | determinate completion fade-out |
| CircularProgress | entered | `duration-long2`, `easing-standard` | indeterminate rotation loop while active |
| CircularProgress | dismissed | `duration-short2`, `easing-accelerate` | determinate completion fade-out |
| ListItem | pressed | `duration-short1`, `easing-standard` | ripple |
| ListItem | selected | `duration-short2`, `easing-standard` | selection tonal swap |
| ListItem | expanded | `duration-medium1`, `easing-emphasized`, `spatial-default` | three-line/illustration expand height morph |
| Avatar | — | `none (static)` | no state change; image slot only (group overflow count is Text) |
| Tooltip | entered | `duration-short2`, `easing-decelerate` | plain-tooltip fade/scale-in via OverlayHost |
| Tooltip | dismissed | `duration-short2`, `easing-accelerate` | fade-out |
| TopAppBar (all sizes) | expanded | `duration-medium2`, `easing-emphasized`, `spatial-default` | medium/large collapse on scroll (geometry) |
| TopAppBar (all sizes) | entered | `duration-medium1`, `easing-decelerate` | title fade during size transition |
| NavigationBar | selected | `duration-medium1`, `easing-emphasized` | active-indicator pill morph between destinations |
| NavigationRail | selected | `duration-medium1`, `easing-emphasized` | active-indicator morph (vertical) |
| NavigationDrawer | expanded | `duration-long1`, `easing-emphasized`, `spatial-default` | modal/standard drawer slide (geometry) |
| NavigationDrawer | dismissed | `duration-medium1`, `easing-accelerate`, `spatial-default` | drawer close slide |
| NavigationDrawer | selected | `duration-short2`, `easing-standard` | destination row selection |
| SegmentedButton | selected | `duration-short2`, `easing-standard` | segment check + shape morph |
| SegmentedButton | pressed | `duration-short1`, `easing-standard` | ripple |
| Dialog | entered | `duration-medium2`, `easing-emphasized`, `spatial-default` | container-transform entry via OverlayHost |
| Dialog | dismissed | `duration-medium1`, `easing-accelerate`, `spatial-default` | scale-down + fade exit |
| BottomSheet | expanded | `duration-long1`, `easing-emphasized`, `spatial-default` | sheet slide to half/full detent (geometry) |
| BottomSheet | dismissed | `duration-medium1`, `easing-accelerate`, `spatial-default` | swipe-down/scrim dismiss slide |
| Menu | expanded | `duration-medium1`, `easing-emphasized`, `spatial-default` | anchored open scale+fade via OverlayHost |
| Menu | dismissed | `duration-short2`, `easing-accelerate` | close fade |
| Menu | selected | `duration-short1`, `easing-standard` | item highlight |
| SnackbarHost | entered | `duration-medium1`, `easing-decelerate`, `spatial-default` | snackbar slide-up entry via OverlayHost |
| SnackbarHost | dismissed | `duration-medium1`, `easing-accelerate`, `spatial-default` | timeout/swipe-away exit |
| SearchBar | pressed | `duration-short1`, `easing-standard` | field press state layer |
| SearchBar | expanded | `duration-medium2`, `easing-emphasized`, `spatial-default` | bar-to-full-search container transform (T3 box path) |
| SearchBar | dismissed | `duration-medium1`, `easing-accelerate`, `spatial-default` | collapse back to bar |
| Carousel | expanded | `duration-medium2`, `easing-emphasized`, `spatial-default` | item snap travel + hero expand (geometry) |
| Carousel | selected | `duration-short2`, `easing-standard` | page-indicator swap |
| BottomAppBar | entered | `duration-medium1`, `easing-decelerate`, `spatial-default` | scroll-away hide / return slide |
| BottomAppBar | dismissed | `duration-short2`, `easing-accelerate`, `spatial-default` | scroll-away hide slide |
| ButtonGroup | pressed | `duration-short1`, `easing-standard` | member ripple (per-button row applies) |
| ButtonGroup | selected | `duration-short2`, `easing-standard` | connected-button selection swap |
| SplitButton | pressed | `duration-short1`, `easing-standard` | primary-face ripple |
| SplitButton | expanded | `duration-medium1`, `easing-emphasized`, `spatial-default` | dropdown-face menu open (see Menu rows) |

(End of map — every tier-1/2/3 component plus Text input from SPEC-M3-NATIVE v1.10
has at least one row; static-only components carry an explicit `none (static)` row.)
