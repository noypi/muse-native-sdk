'use strict';
// FR-M3-001 / AC-M3-001 — tier-1 primitive descriptors (T3).
// Each of the 10 tier-1 primitives is an explicit descriptor factory keeping
// the `{ type, props }` shape: `type` equals the primitive name and `props`
// defaults to `{}`. Runtime passes props through (P1); accepted-key equality
// with the manifest schema is enforced at the type layer in index.d.ts.
// Remaining manifest entries (tier-2 until T5) stay exported via the generic
// loop so the T2 manifest gate keeps matching exactly.

const manifest = require('./muse.manifest.json');

function Surface(props) {
  return { type: 'Surface', props: props === undefined ? {} : props };
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
  return { type: 'Ripple', props: props === undefined ? {} : props };
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
  return { type: 'TextFieldEditCore', props: props === undefined ? {} : props };
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

const components = { ...tier1 };
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

module.exports = { primitives, components, manifest };
