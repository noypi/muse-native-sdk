'use strict';
// @muse/abilities root entry: re-export the public surface so consumers
// can `require('@muse/abilities')` instead of deep-importing `lib/*`.

const { defineAbility } = require('./lib/abilities.js');
const { createRegistry } = require('./lib/registry.js');

module.exports = {
  defineAbility,
  createRegistry,
};
