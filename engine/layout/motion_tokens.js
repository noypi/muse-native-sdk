'use strict';
// FR-LAYOUT-010 / AC-LAYOUT-026 — shared motion-physics token passthrough.
// Reads through to the motion-subsystem token set (FR-MOTION-013,
// `engine/motion/tokens.js`). There is intentionally no local token table
// here: every value resolves from the motion source so layout can never
// redefine a spring, duration or easing value.

const { getMotionToken, UnknownMotionTokenError } = require('../motion/tokens.js');

function motionToken(name) {
  return getMotionToken(name);
}

module.exports = { motionToken, UnknownMotionTokenError };
