'use strict';
// Ability definition: validates the JSON-serializable args map at define
// time so non-serializable kinds fail closed before registration.

const ALLOWED_ARG_KINDS = new Set([
  'string',
  'number',
  'boolean',
  'null',
  'array',
  'object',
]);

function defineAbility(spec) {
  const { name, capabilities, args, invoke } = spec || {};
  const kinds = args || {};
  for (const key of Object.keys(kinds)) {
    if (!ALLOWED_ARG_KINDS.has(kinds[key])) {
      const err = new Error(
        `non-serializable arg kind for "${key}": ${String(kinds[key])}`
      );
      err.code = 'ABILITY_NON_SERIALIZABLE_ARG';
      throw err;
    }
  }
  return {
    name,
    capabilities: Array.isArray(capabilities) ? capabilities.slice() : capabilities,
    args: Object.assign({}, kinds),
    invoke,
  };
}

module.exports = { defineAbility };
