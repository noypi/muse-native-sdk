'use strict';
// RULE-MUSE-007: shared Muse-component contract — preset-name-only, verified Muse IDs only, client holds no key.

const ERROR_CODES = ['auth', 'upstream', 'timeout', 'schema'];

const ALLOWED = {
  idle: ['loading', 'error'],
  loading: ['thinking', 'error'],
  thinking: ['ready', 'error'],
  ready: ['error'],
  error: ['error'],
};

class MuseError extends Error {
  constructor(code, detail) {
    super(detail ? `${code}: ${detail}` : code);
    this.name = 'MuseError';
    this.code = code;
    if (detail !== undefined) this.detail = detail;
  }
}

function createMuseState() {
  const state = {
    status: 'idle',
    route: null,
    reason: null,
    latencyMs: null,
    error: null,
    transition(next, opts) {
      const allowed = ALLOWED[state.status];
      if (typeof next !== 'string' || !allowed || !allowed.includes(next)) {
        const expected = allowed && allowed.length ? allowed.join('|') : '(none)';
        throw new RangeError(`expected: ${expected}, got: ${String(next)}`);
      }
      state.status = next;
      if (next === 'error') {
        let code = 'upstream';
        let detail = 'upstream error';
        if (opts !== undefined && opts !== null) {
          if (typeof opts === 'string') {
            code = opts;
          } else if (typeof opts === 'object') {
            if (opts.code !== undefined) code = opts.code;
            if (opts.detail !== undefined) detail = opts.detail;
          }
        }
        if (!ERROR_CODES.includes(code)) code = 'upstream';
        if (typeof detail !== 'string' || detail.length === 0) detail = `${code} error`;
        state.error = { code, detail };
      }
    },
  };
  return state;
}

function resolvePreset(preset, allowList) {
  if (preset === undefined) return null;
  if (typeof preset !== 'string' || !Array.isArray(allowList) || !allowList.includes(preset)) {
    throw new MuseError('unknown-preset', `unknown preset: ${String(preset)}`);
  }
  return preset;
}

module.exports = { createMuseState, resolvePreset, MuseError, ERROR_CODES };
