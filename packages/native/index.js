'use strict';
// FR-M3-001 / AC-M3-001 — tier-1 primitive descriptors (T3).
// Each of the 10 tier-1 primitives is an explicit descriptor factory keeping
// the `{ type, props }` shape: `type` equals the primitive name and `props`
// defaults to `{}`. Runtime passes props through (P1); accepted-key equality
// with the manifest schema is enforced at the type layer in index.d.ts.
// Remaining manifest entries are all implemented above (tier-1 + tier-2),
// so the export is exactly the static tier maps and the T2 manifest gate
// compares against the real definitions with no stub fallback.

const manifest = require('./muse.manifest.json');

// FR-M3-006 — `TextInput` tier-2 component over the tier-1 text-field edit
// core (T1). The descriptor factory lives in textinput.js beside the
// `createTextInput` engine-adapter handle; it is re-exported here so the
// package surface keeps one registry map.
const { TextInput, createTextInput } = require('./textinput.js');

// FR-M3-013 — Button event-handle surface (T4, real adapter wiring).
// Required beside the other handle modules (not after the export) so the
// package public entry exposes the same fail-closed `createButton` graph
// the tests import; attachments below join the static tier-map export.
const { createButton, foldButtonFrames, onButtonPress, resetButtonTapState } = require('./button.js');

// FR-M3-008 / AC-M3-018 — numeric M3 state-layer opacities resolved from the
// M3 token table (T1). Each driveState spelling maps to the state-layer role
// token used by the engine/layout token path (`md.sys.state.<state>.opacity`,
// see engine/layout/m3_states.js STATE_LAYER_TOKENS), and each role token
// resolves to its numeric M3 value from the token table: hover 0.08,
// focus 0.12, pressed 0.16 per AC-M3-018. The disabled numeric is the
// conformance-baseline value (plan A1); the baseline records no numeric yet,
// so the documented M3 disabled state-layer value applies until T3 pins the
// exact row. Opacity stays a number — never a symbolic string.
const STATE_OPACITY_TOKEN = {
  hover: 'md.sys.state.hovered.opacity',
  focus: 'md.sys.state.focused.opacity',
  pressed: 'md.sys.state.pressed.opacity',
  disabled: 'md.sys.state.disabled.opacity',
};

const STATE_OPACITY = {
  'md.sys.state.hovered.opacity': 0.08,
  'md.sys.state.focused.opacity': 0.12,
  'md.sys.state.pressed.opacity': 0.16,
  'md.sys.state.disabled.opacity': 0.38,
};

function driveState(target, state) {
  if (typeof state !== 'string') {
    throw new TypeError(`Unknown state: ${String(state)}`);
  }
  const token = STATE_OPACITY_TOKEN[state];
  if (token === undefined) {
    throw new RangeError(`Unknown state: ${state}`);
  }
  const opacity = STATE_OPACITY[token];
  return {
    target,
    state,
    layer: {
      type: 'Ripple',
      props: { color: 'primary', opacity },
    },
  };
}

function withStateLayer(name, props) {
  const next = props === undefined ? {} : props;
  if (next !== null && typeof next === 'object' && 'state' in next && next.state !== undefined) {
    return { ...next, stateLayer: driveState(name, next.state) };
  }
  return next;
}

function Surface(props) {
  return { type: 'Surface', props: withStateLayer('Surface', props) };
}
function Text(props) {
  return { type: 'Text', props: props === undefined ? {} : props };
}
function Icon(props) {
  return { type: 'Icon', props: props === undefined ? {} : props };
}
function Image(props) {
  return { type: 'Image', props: props === undefined ? {} : props };
}
function Ripple(props) {
  return { type: 'Ripple', props: withStateLayer('Ripple', props) };
}
function Divider(props) {
  return { type: 'Divider', props: props === undefined ? {} : props };
}
function Scroll(props) {
  return { type: 'Scroll', props: props === undefined ? {} : props };
}
function Stack(props) {
  return { type: 'Stack', props: props === undefined ? {} : props };
}
function TextFieldEditCore(props) {
  return { type: 'TextFieldEditCore', props: withStateLayer('TextFieldEditCore', props) };
}
function OverlayHost(props) {
  return { type: 'OverlayHost', props: props === undefined ? {} : props };
}

const tier1 = {
  Surface,
  Text,
  Icon,
  Image,
  Ripple,
  Divider,
  Scroll,
  Stack,
  TextFieldEditCore,
  OverlayHost,
};

// FR-M3-002 / AC-M3-003 — tier-2 components over tier-1 primitives (T5).
// Each tier-2 factory keeps the `{ type, props }` descriptor shape with props
// passed through untouched, and composes tier-1 descriptors only (no new
// native primitive). Composition lives in sibling keys beside `props` so the
// echoed props stay exactly the caller's props; the C1 type-key walk observes
// the nested tier-1 types across the whole descriptor graph. All nested props
// reference token roles only (no literal colour/dimension). Overlay members
// compose OverlayHost per RULE-M3-006.
function Button(props) {
  const next = props === undefined ? {} : props;
  const tree = [
    Surface({ tone: 'primary' }),
    Text({ variant: 'label-large', color: 'on-primary' }),
    Ripple({ color: 'on-primary' }),
  ];
  if (next !== null && typeof next === 'object' && typeof next.icon === 'string') {
    tree.push(Icon({ name: next.icon, color: 'on-primary' }));
  }
  return { type: 'Button', props: next, content: tree };
}
function IconButton(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'IconButton',
    props: next,
    content: [Surface({ tone: 'surface' }), Icon({ name: 'star', color: 'primary' }), Ripple({ color: 'primary' })],
  };
}
function Fab(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Fab',
    props: next,
    content: [Surface({ tone: 'primary-container' }), Icon({ name: 'add', color: 'on-primary-container' }), Ripple({ color: 'on-primary-container' })],
  };
}
function ExtendedFab(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'ExtendedFab',
    props: next,
    content: [
      Surface({ tone: 'primary-container' }),
      Text({ variant: 'label-large', color: 'on-primary-container' }),
      Ripple({ color: 'on-primary-container' }),
    ],
  };
}
function Card(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Card',
    props: next,
    content: [Surface({ tone: 'surface' }), Stack({ direction: 'column' }), Text({ variant: 'body-medium', color: 'on-surface' })],
  };
}
function Chip(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Chip',
    props: next,
    content: [Surface({ tone: 'surface' }), Text({ variant: 'label-large', color: 'on-surface' }), Ripple({ color: 'primary' })],
  };
}
function Checkbox(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Checkbox',
    props: next,
    content: [Surface({ tone: 'surface' }), Icon({ name: 'check', color: 'primary' }), Ripple({ color: 'primary' })],
  };
}
function Radio(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Radio',
    props: next,
    content: [Surface({ tone: 'surface' }), Icon({ name: 'radio', color: 'primary' }), Ripple({ color: 'primary' })],
  };
}
function Switch(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Switch',
    props: next,
    content: [Surface({ tone: 'surface-variant' }), Icon({ name: 'switch', color: 'primary' }), Ripple({ color: 'primary' })],
  };
}
function Slider(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Slider',
    props: next,
    content: [Surface({ tone: 'surface' }), Divider({ orientation: 'horizontal', color: 'primary' }), Ripple({ color: 'primary' })],
  };
}
function Badge(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Badge',
    props: next,
    content: [Surface({ tone: 'error' }), Text({ variant: 'label-small', color: 'on-error' })],
  };
}
function LinearProgress(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'LinearProgress',
    props: next,
    content: [Surface({ tone: 'surface-variant' }), Ripple({ color: 'primary' })],
  };
}
function CircularProgress(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'CircularProgress',
    props: next,
    content: [Surface({ tone: 'surface-variant' }), Ripple({ color: 'primary' })],
  };
}
function ListItem(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'ListItem',
    props: next,
    content: [
      Stack({ direction: 'row' }),
      Text({ variant: 'title-medium', color: 'on-surface' }),
      Divider({ orientation: 'horizontal', color: 'outline' }),
    ],
  };
}
function Avatar(props) {
  const next = props === undefined ? {} : props;
  const tree = [Surface({ tone: 'primary-container' }), Text({ variant: 'title-medium', color: 'on-primary-container' })];
  if (next !== null && typeof next === 'object' && typeof next.source === 'string') {
    tree.push(Image({ source: next.source, contentFit: 'cover' }));
  }
  return { type: 'Avatar', props: next, content: tree };
}
function Tooltip(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Tooltip',
    props: next,
    layer: OverlayHost({ content: Text({ variant: 'body-small', color: 'inverse-on-surface' }) }),
    anchor: Text({ variant: 'body-small', color: 'on-surface' }),
  };
}

const tier2 = {
  Button,
  IconButton,
  Fab,
  ExtendedFab,
  Card,
  Chip,
  Checkbox,
  Radio,
  Switch,
  Slider,
  Badge,
  LinearProgress,
  CircularProgress,
  ListItem,
  Avatar,
  Tooltip,
  TextInput,
};

// FR-M3-003 / AC-M3-005 — tier-3 composites over tier-1/2 only (T2).
// Each tier-3 factory keeps the `{ type, props }` descriptor shape with props
// passed through untouched, and composes tier-1/2 descriptors only (no new
// native primitive). Composition lives in sibling keys beside `props` so the
// echoed props stay exactly the caller's props; the C1 type-key walk observes
// the nested tier-1/2 types across the whole descriptor graph. All nested
// props reference token roles only (no literal colour/dimension). Overlay
// members (Dialog, BottomSheet, Menu, SnackbarHost) compose OverlayHost per
// RULE-M3-006. TopAppBar renders small|medium|large distinctly via a
// size-selected title variant (ASSUMPTION C2). Mirrors composites/*.tsx.
const TOP_APP_BAR_TITLE_VARIANT = {
  small: 'title-large',
  medium: 'headline-small',
  large: 'headline-medium',
};

function TopAppBar(props) {
  const next = props === undefined ? {} : props;
  const size = next !== null && typeof next === 'object' && typeof next.size === 'string' ? next.size : 'small';
  return {
    type: 'TopAppBar',
    props: next,
    content: [
      Surface({ tone: 'surface' }),
      Text({ variant: Object.prototype.hasOwnProperty.call(TOP_APP_BAR_TITLE_VARIANT, size) ? TOP_APP_BAR_TITLE_VARIANT[size] : TOP_APP_BAR_TITLE_VARIANT.small, color: 'on-surface', text: next.title }),
      Icon({ name: 'menu', color: 'on-surface' }),
      Divider({ orientation: 'horizontal', color: 'outline' }),
    ],
  };
}
function NavigationBar(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'NavigationBar',
    props: next,
    content: [
      Surface({ tone: 'surface-container' }),
      Stack({ direction: 'row' }),
      Icon({ name: 'home', color: 'on-surface' }),
      Text({ variant: 'label-medium', color: 'on-surface', selected: next.selected }),
      Ripple({ color: 'primary' }),
    ],
  };
}
function NavigationRail(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'NavigationRail',
    props: next,
    content: [
      Surface({ tone: 'surface-container' }),
      Stack({ direction: 'column' }),
      Icon({ name: 'home', color: 'on-surface' }),
      Text({ variant: 'label-medium', color: 'on-surface', selected: next.selected }),
      Ripple({ color: 'primary' }),
    ],
  };
}
function NavigationDrawer(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'NavigationDrawer',
    props: next,
    content: [
      Surface({ tone: 'surface-container-low' }),
      ListItem({ tone: 'surface-container-low', selected: next.selected }),
      Icon({ name: 'home', color: 'on-surface' }),
      Text({ variant: 'label-large', color: 'on-surface' }),
      Divider({ orientation: 'horizontal', color: 'outline' }),
    ],
  };
}
function SegmentedButton(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SegmentedButton',
    props: next,
    content: [
      Chip({ tone: 'surface', selected: next.selected }),
      Surface({ tone: 'surface' }),
      Text({ variant: 'label-large', color: 'on-surface' }),
      Ripple({ color: 'primary' }),
    ],
  };
}
function Dialog(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Dialog',
    props: next,
    layer: OverlayHost({ content: Surface({ tone: 'surface-container-high' }) }),
    content: [
      Surface({ tone: 'surface-container-high' }),
      Text({ variant: 'headline-small', color: 'on-surface', text: next.title }),
      Button({ tone: 'primary' }),
    ],
  };
}
function BottomSheet(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'BottomSheet',
    props: next,
    layer: OverlayHost({ content: Surface({ tone: 'surface-container-low' }) }),
    content: [
      Surface({ tone: 'surface-container-low', open: next.open }),
      Divider({ orientation: 'horizontal', color: 'outline' }),
      Text({ variant: 'body-medium', color: 'on-surface' }),
    ],
  };
}
function Menu(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Menu',
    props: next,
    layer: OverlayHost({ content: Surface({ tone: 'surface-container' }) }),
    anchor: Text({ variant: 'body-medium', color: 'on-surface' }),
    content: [
      Surface({ tone: 'surface-container' }),
      ListItem({ tone: 'surface-container', open: next.open }),
      Text({ variant: 'body-large', color: 'on-surface' }),
    ],
  };
}
function SnackbarHost(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SnackbarHost',
    props: next,
    layer: OverlayHost({ content: Surface({ tone: 'inverse-surface' }) }),
    content: [
      Surface({ tone: 'inverse-surface' }),
      Text({ variant: 'body-medium', color: 'inverse-on-surface', text: next.message }),
      Button({ tone: 'inverse-surface' }),
    ],
  };
}
function SearchBar(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SearchBar',
    props: next,
    content: [
      TextInput({ tone: 'surface-container-high', value: next.value }),
      Surface({ tone: 'surface-container-high' }),
      Icon({ name: 'search', color: 'on-surface' }),
    ],
  };
}
function Carousel(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Carousel',
    props: next,
    content: [
      Scroll({ orientation: 'horizontal', index: next.index }),
      Stack({ direction: 'row' }),
      Image({ contentFit: 'cover' }),
      Surface({ tone: 'surface' }),
    ],
  };
}
function BottomAppBar(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'BottomAppBar',
    props: next,
    content: [
      Surface({ tone: 'surface-container' }),
      IconButton({ tone: 'surface-container' }),
      Icon({ name: 'menu', color: 'on-surface' }),
      Text({ variant: 'title-medium', color: 'on-surface', text: next.title }),
    ],
  };
}
function ButtonGroup(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'ButtonGroup',
    props: next,
    content: [
      Button({ tone: 'surface', selected: next.selected }),
      Surface({ tone: 'surface' }),
      Text({ variant: 'label-large', color: 'on-surface' }),
    ],
  };
}
function SplitButton(props) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SplitButton',
    props: next,
    content: [
      Button({ tone: 'primary', label: next.label }),
      IconButton({ tone: 'primary' }),
      Icon({ name: 'expand', color: 'on-primary' }),
      Divider({ orientation: 'vertical', color: 'on-primary' }),
    ],
  };
}

const tier3 = {
  TopAppBar,
  NavigationBar,
  NavigationRail,
  NavigationDrawer,
  SegmentedButton,
  Dialog,
  BottomSheet,
  Menu,
  SnackbarHost,
  SearchBar,
  Carousel,
  BottomAppBar,
  ButtonGroup,
  SplitButton,
};

const components = { ...tier1, ...tier2, ...tier3 };

const primitives = manifest.components.map((entry) => entry.name);

// FR-M3-002 / AC-M3-023 — selection-control tap (T5).
// Pure transition over the FR-INPUT-011 tap chain (tap fires once per
// AC-INPUT-020, write-through repaints within one frame per AC-INPUT-021 —
// consumed, not reimplemented: this function carries no coordinates and
// performs no repaint itself). Enabled Checkbox flips `checked` strictly
// (only `=== true` counts as checked, mirroring selection.js
// `createCheckbox`); enabled Radio sets `selected: true` idempotently
// (mirroring `createRadio`); `enabled: false` means disabled and absent
// `enabled` means enabled. Disabled controls return state unchanged with the
// same strict-boolean read. The result carries only its own key and `prev`
// is never mutated. Values agree by construction with what the existing
// selection binding (`selection.js` handle `tap()` + `binding.set`)
// delivers for the same input (binding parity, no re-instantiation here).
function onSelectTap(control, prev) {
  if (control !== 'Checkbox' && control !== 'Radio') {
    throw new TypeError(`Unknown control: ${String(control)}`);
  }
  const safe = prev === undefined || prev === null ? {} : prev;
  const enabled = safe.enabled !== false;
  if (control === 'Checkbox') {
    const current = safe.checked === true;
    return { checked: enabled ? !current : current };
  }
  const current = safe.selected === true;
  return { selected: enabled ? true : current };
}

// FR-M3-002 / AC-M3-024 — slider press-drag scrub (T6).
// Pure transition over the FR-INPUT-011 gesture chain (AC-INPUT-020
// drag-never-fires-tap, AC-INPUT-021 one-frame write-through — consumed, not
// reimplemented: this function carries no coordinate/delta and performs no
// repaint itself; it resolves synchronously). The gesture carries no
// coordinate/delta, so movement is observed as claim + clamp + echo: an
// on-axis non-tap gesture claims the control (`claimsGesture: true`) and
// echoes `prev.value` clamped to `[min, max]` (in-range echoes exactly, so
// bound value and visible position never desynchronize; degenerate
// `[min, max]` pins to `min`); `isTap` never claims and never fires a tap
// handler, though an out-of-range bound value is still reported clamped so
// sync holds even when the gesture is not claimed; off-axis past slop yields
// to scroll (`claimsGesture: false`, value unchanged); within-slop off-axis
// still claims and on-axis past slop still claims (slop disambiguates only
// the off-axis yield). `enabled: false` means disabled and absent `enabled`
// means enabled; disabled echoes `prev.value` and never claims. `prev` is
// never mutated. Missing `prev`/`gesture` or an unknown `axis` rejects with
// `TypeError`.
function onSliderScrub(prev, gesture) {
  if (prev === undefined || prev === null) {
    throw new TypeError(`Missing prev: ${String(prev)}`);
  }
  if (gesture === undefined || gesture === null) {
    throw new TypeError(`Missing gesture: ${String(gesture)}`);
  }
  if (gesture.axis !== 'control' && gesture.axis !== 'off-axis') {
    throw new TypeError(`Unknown axis: ${String(gesture && gesture.axis)}`);
  }
  if (prev.enabled === false) {
    return { value: prev.value, claimsGesture: false };
  }
  if (gesture.isTap === true) {
    return { value: Math.min(Math.max(prev.value, prev.min), prev.max), claimsGesture: false };
  }
  if (gesture.axis === 'off-axis' && gesture.slopPast === true) {
    return { value: prev.value, claimsGesture: false };
  }
  return { value: Math.min(Math.max(prev.value, prev.min), prev.max), claimsGesture: true };
}

// FR-M3-005/AC-M3-009, FR-M3-008/AC-M3-018 (BUG-M3-FIDELITY-014 T2/T3) —
// per-family preview-fill assertion for the gallery-proof gate (TC-M3-021)
// and the golden-diff uniform-fill regression gate (TC-M3-022 shadow).
// T1.3 home settlement: lives on the package public entry beside
// driveState; the plan names this surface only via the test file + CLI bin.
// Mirrors the C++ present path in engine/runtime/src/gallery_present.cpp:
// FamilyKind maps each component to its retained kind and emitFamilyFill
// maps each kind to its token-resolved card fill, so the expected values
// below are the same M3 role tokens the device path submits to
// renderer.draw (T2 names the InkFor owner: payload-0 Rect/RoundedRect/Image
// resolves to the surface-variant tonal default; T3 wires the per-family
// payload).
// Values use the sampler AARRGGBB packing of the T1 pins (0xFFE7E0EC for the
// uniform surface-variant baseline), matching the observed device sample,
// not the C++ LE word.
// TextFieldEditCore/TextInput pin the static-box Image fill; the live
// retained-kind override (liveKindOverrideActive) is device state the static
// gate cannot observe. Unknown/empty families and non-u32 samples fail
// closed (false, never throw) per AC-M3-018.
const FAMILY_PREVIEW_FILL = {
  // StateLayer kind → secondary-container token.
  Button: 0xffe8def8,
  ExtendedFab: 0xffe8def8,
  Chip: 0xffe8def8,
  Checkbox: 0xffe8def8,
  Switch: 0xffe8def8,
  Slider: 0xffe8def8,
  LinearProgress: 0xffe8def8,
  GalleryRow: 0xffe8def8,
  Surface: 0xffe8def8,
  // Ripple kind → tertiary-container token.
  IconButton: 0xffffd8e4,
  Fab: 0xffffd8e4,
  Ripple: 0xffffd8e4,
  // OverlayScrim kind → inverse-surface token.
  Dialog: 0xff322f35,
  Menu: 0xff322f35,
  Tooltip: 0xff322f35,
  // VectorIcon kind → on-primary-container token.
  Icon: 0xff4f378b,
  Avatar: 0xff4f378b,
  Image: 0xff4f378b,
  // CircleArc kind → primary token.
  CircularProgress: 0xff6750a4,
  Radio: 0xff6750a4,
  Badge: 0xff6750a4,
  // Image kind (static box) → white token.
  TextFieldEditCore: 0xffffffff,
  TextInput: 0xffffffff,
  SearchBar: 0xffffffff,
  Carousel: 0xffffffff,
  // Rect kind → default surface-variant token (unchanged T2 default).
  Card: 0xffe7e0ec,
  Divider: 0xffe7e0ec,
  BottomSheet: 0xffe7e0ec,
  NavigationBar: 0xffe7e0ec,
  SegmentedButton: 0xffe7e0ec,
  ButtonGroup: 0xffe7e0ec,
  SplitButton: 0xffe7e0ec,
  NavigationRail: 0xffe7e0ec,
  NavigationDrawer: 0xffe7e0ec,
  BottomAppBar: 0xffe7e0ec,
  SnackbarHost: 0xffe7e0ec,
};

function assertPreviewFill(family, sampledArgb) {
  if (typeof family !== 'string' || family.length === 0) return false;
  const expected = FAMILY_PREVIEW_FILL[family];
  if (expected === undefined) return false;
  if (
    typeof sampledArgb !== 'number' ||
    !Number.isInteger(sampledArgb) ||
    sampledArgb < 0 ||
    sampledArgb > 0xffffffff
  ) {
    return false;
  }
  return (sampledArgb >>> 0) === expected;
}

module.exports = { primitives, components, manifest, driveState, createTextInput, TextInput, onSelectTap, onSliderScrub, assertPreviewFill };
// FR-M3-013 — Button event-handle surface attached here (requires hoisted
// beside the other handle modules above): the canonical delivery-lane copy
// lives in ./button.js beside the selection.js handles.
module.exports.createButton = createButton;
module.exports.foldButtonFrames = foldButtonFrames;
module.exports.onButtonPress = onButtonPress;
module.exports.resetButtonTapState = resetButtonTapState;
// FR-MUSE-013 — connector re-export (T4, additive; existing surface untouched).
// MuseMcpHandle is `{ registry, tools, call, dispose }` from ./muse-mcp.js.
const { createMuseMcp } = require('./muse-mcp.js');
module.exports.createMuseMcp = createMuseMcp;
module.exports.lintAppLiteral = require('./scripts/check-tokens.js').lintAppLiteral;
