'use strict';
// T4 — Profile staleness gate: committed profile must not predate release sources
// (NFR-BUILD-006 / AC-BUILD-008).
// Hermetic: node:test + node:assert/strict (+ node:fs/node:path only to check
// the module header documents the regeneration command). Pure in-memory inputs
// otherwise; no network, no secrets, no live services.
// Red state: ./stalenessGate.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND / ENOENT via loadImpl) until the coder lands it.
//
// Interface under test (plan verbatim):
//   stalenessCheck(profile: {hash:string, sourceStamp:number}, sources: {stamp:number}): {ok:boolean, reason:string}
// ok:false when profile.sourceStamp < sources.stamp or hash missing/empty;
// single-command regeneration path documented in module header comment
// (regenerate(): string returning the one CLI command string).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

function loadImpl() {
  return require('./stalenessGate.js');
}

function readSource() {
  return fs.readFileSync(path.join(__dirname, 'stalenessGate.js'), 'utf8');
}

describe('T4 — contract shape (AC-BUILD-008)', () => {
  it('returns { ok: boolean, reason: string } for an up-to-date profile', () => {
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck(
      { hash: 'abc123', sourceStamp: 200 },
      { stamp: 100 }
    );
    assert.equal(typeof result.ok, 'boolean');
    assert.equal(typeof result.reason, 'string');
    assert.equal(result.ok, true);
  });
});

describe('T4 — freshness verdict (AC-BUILD-008)', () => {
  it('profile newer than sources passes', () => {
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck(
      { hash: 'abc123', sourceStamp: 200 },
      { stamp: 100 }
    );
    assert.equal(result.ok, true);
  });

  it('equal stamps pass (boundary: not predating means fresh)', () => {
    // ASSUMPTION: sourceStamp === stamp is fresh; only strictly older is stale.
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck(
      { hash: 'abc123', sourceStamp: 100 },
      { stamp: 100 }
    );
    assert.equal(result.ok, true);
  });

  it('older stamp fails with a reason naming staleness', () => {
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck(
      { hash: 'abc123', sourceStamp: 50 },
      { stamp: 100 }
    );
    assert.equal(result.ok, false);
    assert.match(result.reason, /stale|older|stamp/i);
  });

  it('empty hash fails even when stamps are fresh', () => {
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck(
      { hash: '', sourceStamp: 200 },
      { stamp: 100 }
    );
    assert.equal(result.ok, false);
    assert.match(result.reason, /hash/i);
  });

  it('missing hash fails', () => {
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck({ sourceStamp: 200 }, { stamp: 100 });
    assert.equal(result.ok, false);
    assert.match(result.reason, /hash/i);
  });

  it('whitespace-only hash fails', () => {
    // ASSUMPTION: blank hash is equivalent to empty — no verifiable profile.
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck(
      { hash: '   ', sourceStamp: 200 },
      { stamp: 100 }
    );
    assert.equal(result.ok, false);
    assert.match(result.reason, /hash/i);
  });

  it('missing source stamp fails closed', () => {
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck({ hash: 'abc123', sourceStamp: 200 }, {});
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('missing profile stamp fails closed', () => {
    const { stalenessCheck } = loadImpl();
    const result = stalenessCheck({ hash: 'abc123' }, { stamp: 100 });
    assert.equal(result.ok, false);
    assert.ok(result.reason.length > 0);
  });

  it('non-numeric stamps fail closed', () => {
    const { stalenessCheck } = loadImpl();
    assert.equal(
      stalenessCheck({ hash: 'abc123', sourceStamp: 'new', sources: 0 }, { stamp: 100 }).ok,
      false
    );
    assert.equal(
      stalenessCheck({ hash: 'abc123', sourceStamp: 200 }, { stamp: 'old' }).ok,
      false
    );
  });
});

describe('T4 — regeneration command (AC-BUILD-008)', () => {
  it('regenerate() returns a non-empty single command string', () => {
    const { regenerate } = loadImpl();
    assert.equal(typeof regenerate, 'function');
    const cmd = regenerate();
    assert.equal(typeof cmd, 'string');
    assert.ok(cmd.trim().length > 0, 'regeneration command must be non-empty');
    assert.ok(!cmd.includes('\n'), 'must be a single command, not a script');
  });

  it('module header documents the regeneration path', () => {
    const src = readSource();
    assert.match(src, /regenerate/);
  });
});
