'use strict';
// FR-M3 lane — @muse/native primitive skeleton (stub descriptors).
// FR-M3 fills in real primitives/components later without renaming these
// exports; this file only exposes inert { type, props } descriptor stubs.

const manifest = require('./muse.manifest.json');

const primitives = manifest.components.map((entry) => entry.name);

const components = {};
for (const name of primitives) {
  components[name] = function stubComponent(props) {
    return { type: name, props: props === undefined ? {} : props };
  };
}

module.exports = { primitives, components, manifest };
