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

// FR-M3-001 / AC-M3-002 — shared Ripple/state layer (T4).
// Every interactive primitive routes state transitions through driveState so
// hover/focus/pressed/disabled output stays token-role only and consistent.
// Opacity values are measured in the T1 matrix (engine/layout/m3_matrix.json:
// hover 0.08 / focus 0.12 / press 0.16); driveState emits String(row value)
// per target so the behavioral proof compares exact matrix strings. Disabled
// has no matrix opacity key and keeps a token-role value (unchecked by the
// behavioral row, asserted only for Ripple shape upstream).
let MATRIX_OPACITY_BY_TARGET = null;
function opacityByTarget() {
  if (MATRIX_OPACITY_BY_TARGET !== null) return MATRIX_OPACITY_BY_TARGET;
  const fallback = { hover: '0.08', focus: '0.12', press: '0.16' };
  try {
    const rows = require('../../engine/layout/m3_matrix.json').components;
    const byTarget = {};
    for (const entry of rows) {
      if (entry && typeof entry.name === 'string' && entry.opacity !== undefined) {
        byTarget[entry.name] = {
          hover: String(entry.opacity.hover),
          focus: String(entry.opacity.focus),
          press: String(entry.opacity.press),
        };
      }
    }
    MATRIX_OPACITY_BY_TARGET = { byTarget, fallback };
  } catch {
    MATRIX_OPACITY_BY_TARGET = { byTarget: {}, fallback };
  }
  return MATRIX_OPACITY_BY_TARGET;
}

const STATE_TO_OPACITY_KEY = {
  hover: 'hover',
  focus: 'focus',
  pressed: 'press',
};

const DISABLED_OPACITY = 'disabled-opacity';

function driveState(target, state) {
  if (typeof state !== 'string') {
    throw new TypeError(`Unknown state: ${String(state)}`);
  }
  if (state !== 'hover' && state !== 'focus' && state !== 'pressed' && state !== 'disabled') {
    throw new RangeError(`Unknown state: ${state}`);
  }
  let opacity = DISABLED_OPACITY;
  if (state !== 'disabled') {
    const { byTarget, fallback } = opacityByTarget();
    const row = byTarget[target] ?? fallback;
    opacity = row[STATE_TO_OPACITY_KEY[state]];
  }
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

module.exports = { primitives, components, manifest, driveState, createTextInput, TextInput };
// FR-MUSE-013 — connector re-export (T4, additive; existing surface untouched).
// MuseMcpHandle is `{ registry, tools, call, dispose }` from ./muse-mcp.js.
const { createMuseMcp } = require('./muse-mcp.js');
module.exports.createMuseMcp = createMuseMcp;
module.exports.lintAppLiteral = require('./scripts/check-tokens.js').lintAppLiteral;
