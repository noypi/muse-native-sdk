'use strict';
// T1 — FR-DX-001 / AC-DX-004 / TC-DX-004 (Batch 1).
// Hermetic: node:test + node:assert/strict only. All collaborators faked
// in-process; no network, no secrets, no config, no live services.
// Interface under test (plan verbatim):
//   applyDelta(delta: Buffer) => ({status:'applied', preserved: string[]} |
//     {status:'rejected', code: string, cause: string, action: string})
//   exported from tools/devserver/lib/dev-channel-server.js;
//   errors are structured {command:"applyDelta", cause, action} per NFR-DX-002.
// ASSUMPTION (D1): preservation boundary per Q-DX-001/A2 — navigation stack,
// form input, and registered signal handles are preserved; only the edited
// component's own non-signal local state may reset with an overlay notice.
// ASSUMPTION: delta payloads are opaque Buffers carrying a JSON envelope
// {component, patch} for updates; malformed buffers are rejected, not thrown.
// ASSUMPTION: the applied result surfaces the edited-component local reset
// plus an overlay notice string (field `reset` + `overlay`) alongside
// `preserved`; rejection carries {status:'rejected', code, cause, action}
// AND a top-level {command:'applyDelta', cause, action} shape.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'dev-channel-server.js');

function load() {
  return require(LIB);
}

function deltaOf(obj) {
  return Buffer.from(JSON.stringify(obj), 'utf8');
}

describe('dev-channel applyDelta (T1)', () => {
  beforeEach(() => {
    delete require.cache[LIB];
  });

  it('delta updates UI: applied result reports status applied', () => {
    const { applyDelta } = load();
    assert.equal(typeof applyDelta, 'function');
    const res = applyDelta(deltaOf({ component: 'Greeting', patch: { text: 'hi2' } }));
    assert.equal(res.status, 'applied');
  });

  it('preserves navigation stack across hot reload (D1)', () => {
    const { applyDelta } = load();
    const res = applyDelta(deltaOf({ component: 'Greeting', patch: { text: 'x' } }));
    assert.ok(Array.isArray(res.preserved), 'preserved must be an array');
    assert.ok(res.preserved.includes('navigation'), 'navigation must be preserved');
  });

  it('preserves form input across hot reload (D1)', () => {
    const { applyDelta } = load();
    const res = applyDelta(deltaOf({ component: 'Form', patch: { label: 'y' } }));
    assert.ok(res.preserved.includes('form'), 'form input must be preserved');
  });

  it('preserves registered signal handles across hot reload (D1)', () => {
    const { applyDelta } = load();
    const res = applyDelta(deltaOf({ component: 'Counter', patch: { step: 2 } }));
    assert.ok(res.preserved.includes('signals'), 'signal handles must be preserved');
  });

  it('edited-component local reset surfaces with overlay notice (D1)', () => {
    const { applyDelta } = load();
    const res = applyDelta(deltaOf({ component: 'Counter', patch: { step: 3 } }));
    assert.ok(res.reset !== undefined || res.overlay !== undefined,
      'applied result must surface local reset + overlay notice');
    const overlay = String(res.overlay !== undefined ? res.overlay : res.reset);
    assert.match(overlay, /reset|overlay|notice/i);
  });

  it('rejection is structured {command, cause, action} with code, never throws', () => {
    const { applyDelta } = load();
    const res = applyDelta(Buffer.from('not-a-delta@@@'));
    assert.equal(res.status, 'rejected');
    assert.equal(res.command, 'applyDelta');
    assert.equal(typeof res.code, 'string');
    assert.equal(typeof res.cause, 'string');
    assert.equal(typeof res.action, 'string');
  });

  it('hugely oversized delta is rejected structurally, never throws or OOMs (security: NFR-DX-002 DoS hardening)', () => {
    const { applyDelta } = load();
    // 32 MiB of non-JSON bytes: far beyond any legitimate {component, patch}
    // envelope; must be rejected without unbounded parse cost escaping as a
    // throw or a process OOM.
    const huge = Buffer.alloc(32 * 1024 * 1024, 0x78);
    let res;
    assert.doesNotThrow(() => {
      res = applyDelta(huge);
    }, 'oversized delta must never throw');
    assert.equal(res.status, 'rejected');
    assert.equal(res.command, 'applyDelta');
    assert.equal(typeof res.cause, 'string');
    assert.ok(res.cause.length > 0, 'cause must name the failure');
    assert.equal(typeof res.action, 'string');
    assert.ok(res.action.length > 0, 'action must give a next step');
  });

  it('release channel is never required from this path (T10 owns refusal)', () => {
    const src = fs.readFileSync(LIB, 'utf8');
    assert.ok(!/release-stub|release channel|dev channel/i.test(src) || !/require\(['"]\..*release/.test(src),
      'dev-channel server must not wire a release/dev channel listener');
    assert.ok(!/require\(['"][^'"]*release/.test(src), 'must not import release modules');
  });
});
