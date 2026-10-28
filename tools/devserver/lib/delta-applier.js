'use strict';
// Opaque delta payload handling for hot reload (FR-DX-001).
// Deltas arrive as Buffers carrying a JSON envelope {component, patch}.
// This module never inspects UI internals: it only validates and decodes
// the envelope so the caller can apply it while preserving state per D1.

const MAX_DELTA_BYTES = 1024 * 1024;

function parseDelta(delta) {
  if (!Buffer.isBuffer(delta) || delta.length === 0) {
    const err = new Error('Empty delta buffer: expected a Buffer with a JSON {component, patch} envelope.');
    err.code = 'E_DELTA_EMPTY';
    throw err;
  }
  if (delta.length > MAX_DELTA_BYTES) {
    const err = new Error(
      `Delta too large (${delta.length} bytes exceeds ${MAX_DELTA_BYTES} byte limit): expected a JSON {component, patch} envelope.`,
    );
    err.code = 'E_DELTA_TOO_LARGE';
    throw err;
  }
  let text;
  try {
    text = delta.toString('utf8');
  } catch (_) {
    const err = new Error('Unreadable delta buffer: expected utf8 JSON {component, patch}.');
    err.code = 'E_DELTA_MALFORMED';
    throw err;
  }
  let envelope;
  try {
    envelope = JSON.parse(text);
  } catch (_) {
    const err = new Error('Malformed delta: buffer is not JSON {component, patch}.');
    err.code = 'E_DELTA_MALFORMED';
    throw err;
  }
  if (envelope === null || typeof envelope !== 'object' || Array.isArray(envelope)) {
    const err = new Error('Malformed delta envelope: expected a JSON object {component, patch}.');
    err.code = 'E_DELTA_MALFORMED';
    throw err;
  }
  return envelope;
}

module.exports = { parseDelta, MAX_DELTA_BYTES };
