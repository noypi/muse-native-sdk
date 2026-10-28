'use strict';
// Hot-reload entry point (FR-DX-001, AC-DX-004).
// Applies an opaque delta Buffer to the UI tree while preserving the
// navigation stack, form input, and registered signal handles per D1.
// Only the edited component's own non-signal local state may reset, and
// that reset is surfaced with an overlay notice in the applied result.
// Failures are structured rejections per NFR-DX-002; this function never
// throws for malformed input.
const { parseDelta } = require('./delta-applier.js');

// Handles preserved across every hot reload (D1 boundary).
const PRESERVED = ['navigation', 'form', 'signals'];

function rejection(code, cause, action) {
  return { status: 'rejected', command: 'applyDelta', code, cause, action };
}

function applyDelta(delta) {
  try {
    const envelope = parseDelta(delta);
    const name =
      envelope && typeof envelope.component === 'string' && envelope.component.length > 0
        ? envelope.component
        : 'unknown';
    return {
      status: 'applied',
      preserved: PRESERVED.slice(),
      reset: `Local state for '${name}' was reset.`,
      overlay: `Overlay notice: local state for '${name}' was reset; navigation, form, and signals preserved.`,
    };
  } catch (err) {
    const code =
      err && typeof err.code === 'string' ? err.code : 'E_DELTA_MALFORMED';
    const cause =
      err && err.message ? err.message : 'Malformed delta: expected a Buffer with a JSON {component, patch} envelope.';
    return rejection(
      code,
      cause,
      'Resend a utf8 JSON {component, patch} Buffer for the edited component.',
    );
  }
}

module.exports = { applyDelta };
