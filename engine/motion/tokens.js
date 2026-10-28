'use strict';
// FR-MOTION-013 / AC-MOTION-042/043/044 — M3 motion scheme token set.
// JS embodiment of the version-pinned token table: durations (short1-long4),
// easings (standard, emphasized, accelerate, decelerate), spatial springs
// (position, size, shape) and effects springs (colour, opacity).
// Version pin per AC-MOTION-044 / Q-LAYOUT-001 A3: changing a value below is a
// versioned change (bump VERSION and list affected components).

const SOURCE = 'm3-expressive';
const VERSION = '1.0.0';

const EASING_CURVES = {
  standard: 'cubic-bezier(0.2, 0, 0, 1)',
  emphasized: 'cubic-bezier(0.05, 0.7, 0.1, 1)',
  accelerate: 'cubic-bezier(0.3, 0, 0.8, 0.15)',
  decelerate: 'cubic-bezier(0.05, 0.7, 0.1, 1)',
};

const DURATIONS_MS = {
  'short1': 50,
  'short2': 100,
  'short3': 150,
  'short4': 200,
  'medium1': 250,
  'medium2': 300,
  'medium3': 350,
  'medium4': 400,
  'long1': 450,
  'long2': 500,
  'long3': 550,
  'long4': 600,
  // Generic alias used by components that do not pin a numbered slot.
  'medium': 300,
};

const SPATIAL_SPRINGS = {
  standard: { stiffness: 800, damping: 0.9, mass: 1, type: 'spatial' },
  position: { stiffness: 800, damping: 0.9, mass: 1, type: 'spatial' },
  size: { stiffness: 600, damping: 0.85, mass: 1, type: 'spatial' },
  shape: { stiffness: 500, damping: 0.9, mass: 1, type: 'spatial' },
};

const EFFECTS_SPRINGS = {
  colour: { stiffness: 300, damping: 1, mass: 1, type: 'effects' },
  opacity: { stiffness: 300, damping: 1, mass: 1, type: 'effects' },
};

// Full token table keyed by canonical token name. Every value carries only
// the motion schema keys (`curve`, `durationMs`, `spring?`).
const TOKENS = Object.create(null);

for (const [slot, durationMs] of Object.entries(DURATIONS_MS)) {
  TOKENS[`motion.durations.${slot}`] = {
    curve: EASING_CURVES.standard,
    durationMs,
  };
}

for (const [name, curve] of Object.entries(EASING_CURVES)) {
  TOKENS[`motion.expressive.easing.${name}`] = {
    curve,
    durationMs: DURATIONS_MS.medium2,
  };
}

for (const [name, spring] of Object.entries(SPATIAL_SPRINGS)) {
  TOKENS[`motion.expressive.spring.${name}`] = {
    curve: EASING_CURVES.emphasized,
    durationMs: DURATIONS_MS.medium3,
    spring: { ...spring },
  };
}

for (const [name, spring] of Object.entries(SPATIAL_SPRINGS)) {
  if (name === 'standard') continue;
  TOKENS[`motion.expressive.spring.spatial.${name}`] = {
    curve: EASING_CURVES.emphasized,
    durationMs: DURATIONS_MS.medium3,
    spring: { ...spring },
  };
}

for (const [name, spring] of Object.entries(EFFECTS_SPRINGS)) {
  TOKENS[`motion.expressive.spring.effects.${name}`] = {
    curve: EASING_CURVES.standard,
    durationMs: DURATIONS_MS.short4,
    spring: { ...spring },
  };
}

// NOTE: the queried token name deliberately shadows `Error.name` — the
// contract carries the token name as an own `name` field, so callers assert
// `err.constructor.name === 'UnknownMotionTokenError' && err.name === <input>`.
class UnknownMotionTokenError extends Error {
  constructor(name) {
    super(`Unknown motion token: ${String(name)}`);
    this.name = name;
  }
}

function getMotionToken(name) {
  if (typeof name !== 'string' || name === '' || !Object.hasOwn(TOKENS, name)) {
    throw new UnknownMotionTokenError(name);
  }
  const entry = TOKENS[name];
  const out = { curve: entry.curve, durationMs: entry.durationMs };
  if (entry.spring !== undefined) {
    out.spring = { ...entry.spring };
  }
  return out;
}

module.exports = {
  SOURCE,
  VERSION,
  TOKENS,
  getMotionToken,
  UnknownMotionTokenError,
};
