'use strict';

// T2 — FR-DX-002 (AC-DX-006): style/token hot swap without reload or
// re-instantiation. Patches resolved tokens/styles in place; never calls the
// component factory; returns an empty reinstantiated list on success.

const KNOWN_TOKENS = new Set(['colorPrimary']);

function applyTokenSwap(tree, delta) {
  const tokens = (delta && delta.tokens) || {};
  const mapping = (delta && delta.mapping) || {};

  for (const name of Object.keys(tokens)) {
    if (!KNOWN_TOKENS.has(name)) {
      throw new Error(`Unknown token name: ${name}`);
    }
  }

  const components = (tree && tree.components) || [];
  for (const [name, value] of Object.entries(tokens)) {
    const scopedIds = mapping[name];
    const targets =
      scopedIds === undefined
        ? components
        : components.filter((c) => scopedIds.includes(c.id));
    for (const component of targets) {
      if (name === 'colorPrimary') {
        component.resolved = component.resolved || {};
        component.resolved.color = value;
      }
    }
  }

  return { tree, reinstantiated: [] };
}

module.exports = { applyTokenSwap };
