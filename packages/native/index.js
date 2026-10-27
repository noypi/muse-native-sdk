'use strict';
// FR-M3-001 / AC-M3-001 — tier-1 primitive descriptors (T3).
// Each of the 10 tier-1 primitives is an explicit descriptor factory keeping
// the `{ type, props }` shape: `type` equals the primitive name and `props`
// defaults to `{}`. Runtime passes props through (P1); accepted-key equality
// with the manifest schema is enforced at the type layer in index.d.ts.
// Remaining manifest entries (tier-2 until T5) stay exported via the generic
// loop so the T2 manifest gate keeps matching exactly.

const manifest = require('./muse.manifest.json');

// FR-M3-001 / AC-M3-002 — shared Ripple/state layer (T4).
// Every interactive primitive routes state transitions through driveState so
// hover/focus/pressed/disabled output stays token-role only and consistent.
const STATE_OPACITY = {
  hover: 'hover-opacity',
  focus: 'focus-opacity',
  pressed: 'pressed-opacity',
  disabled: 'disabled-opacity',
};

function driveState(target, state) {
  return {
    target,
    state,
    layer: {
      type: 'Ripple',
      props: { color: 'primary', opacity: STATE_OPACITY[state] },
    },
  };
}

function withStateLayer(name, props) {
  const next = props === undefined ? {} : props;
  if (next !== null && typeof next === 'object' && typeof next.state === 'string') {
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
};

const components = { ...tier1, ...tier2 };
for (const entry of manifest.components) {
  const name = entry && entry.name;
  if (typeof name !== 'string' || components[name] !== undefined) continue;
  components[name] = (function (componentName) {
    return function stubComponent(props) {
      return { type: componentName, props: props === undefined ? {} : props };
    };
  })(name);
}

const primitives = manifest.components.map((entry) => entry.name);

module.exports = { primitives, components, manifest, driveState };
