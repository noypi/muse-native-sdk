'use strict';
// T3 — demo3 hands-free domain abilities (FR-SAMPLE-013, AC-SAMPLE-038).
// Authors the five demo verbs via the merged `defineAbility()` framework;
// the single canonical name list lives here and is consumed by registration
// and routing. Minimal host harness only — no FR-SAMPLE-009 behaviour.

const { defineAbility } = require('../../../../packages/abilities/lib/abilities.js');

const ABILITY_NAMES = [
  'profile.open-settings',
  'gallery.open',
  'gallery.select',
  'gallery.image.select',
  'app.close',
];

const TOUCH_EQUIVALENTS = {
  'profile.open-settings': { screen: 'profile', action: 'tap-settings' },
  'gallery.open': { screen: 'gallery', action: 'tap-open' },
  'gallery.select': { screen: 'gallery', action: 'tap-select-gallery' },
  'gallery.image.select': { screen: 'gallery', action: 'tap-select-image' },
  'app.close': { screen: 'home', action: 'tap-close' },
  'reason-fallback': { screen: 'home', action: 'tap-assistant' },
};

// Per-ability capability names: each ability is granted by its own name;
// the default granted set is all five names. `invoke` (not `run`) performs
// the domain action; the router stamps the calling turnId itself.
const abilities = ABILITY_NAMES.map((name) =>
  defineAbility({ name, capabilities: [name], args: {}, invoke: () => `${name}-value` }),
);

module.exports = { ABILITY_NAMES, TOUCH_EQUIVALENTS, abilities };
