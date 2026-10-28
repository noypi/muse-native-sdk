'use strict';
// T11 — QuickJS variant smoke gate (NFR-BUILD-018 / AC-BUILD-068).
// Hermetic: node:test + node:assert/strict only. No real runtimes, no network,
// no live services — the gate asserts the entry source is runtime-agnostic
// (no runtime-conditional imports) and simulates the shared boot sequence.
// Red state: ./quickJsVariantSmoke.js does not exist yet, so every case below
// fails (MODULE_NOT_FOUND) until T11 lands.
//
// Interface under test (plan verbatim):
//   smokeVariant(entry: { source: string, runtime: "hermes" | "quickjs" }): { ok: boolean, log: string[] }
// Same source boots under either runtime with no app-code changes; log
// records boot steps.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./quickJsVariantSmoke.js');
}

const CLEAN_SOURCE = [
  "import { createApp } from './app.js';",
  'const app = createApp();',
  'app.start();',
  '',
].join('\n');

const CONDITIONAL_SOURCE = [
  "import { createApp } from './app.js';",
  "const debugTools = require('hermes-inspect');",
  'const app = createApp();',
  'app.start();',
  '',
].join('\n');

describe('T11 — smokeVariant contract shape', () => {
  it('returns { ok: boolean, log: string[] } for a clean source on hermes', () => {
    const { smokeVariant } = loadImpl();
    const result = smokeVariant({ source: CLEAN_SOURCE, runtime: 'hermes' });
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.log));
    assert.equal(result.ok, true);
  });
});

describe('T11 — same source boots under either runtime (AC-BUILD-068)', () => {
  it('clean source passes the hermes leg with boot steps logged', () => {
    const { smokeVariant } = loadImpl();
    const result = smokeVariant({ source: CLEAN_SOURCE, runtime: 'hermes' });
    assert.equal(result.ok, true);
    assert.ok(result.log.length > 0, 'log must record boot steps');
  });

  it('the same clean source passes the quickjs leg with no app-code changes', () => {
    const { smokeVariant } = loadImpl();
    const forHermes = smokeVariant({ source: CLEAN_SOURCE, runtime: 'hermes' });
    const forQuickjs = smokeVariant({ source: CLEAN_SOURCE, runtime: 'quickjs' });
    assert.equal(forHermes.ok, true);
    assert.equal(forQuickjs.ok, true);
    assert.ok(forQuickjs.log.length > 0, 'log must record boot steps');
  });
});

describe('T11 — runtime-conditional imports fail the QuickJS leg (AC-BUILD-068)', () => {
  it('source with a runtime-conditional import fails the quickjs leg naming the cause', () => {
    const { smokeVariant } = loadImpl();
    const result = smokeVariant({ source: CONDITIONAL_SOURCE, runtime: 'quickjs' });
    assert.equal(result.ok, false);
    const named = result.log.some((line) => /hermes-inspect/i.test(line) || /conditional/i.test(line));
    assert.ok(
      named,
      `the log must name the runtime-conditional cause, got: ${JSON.stringify(result.log)}`
    );
  });

  it('an inline runtime branch fails the quickjs leg', () => {
    // ASSUMPTION: an `if (runtime === ...)` branch counts as a
    // runtime-conditional change, pinned so the gate is implementable.
    const { smokeVariant } = loadImpl();
    const branched = `${CLEAN_SOURCE}if (runtime === 'hermes') { enableInspector(); }\n`;
    const result = smokeVariant({ source: branched, runtime: 'quickjs' });
    assert.equal(result.ok, false);
    assert.ok(result.log.length > 0, 'a failing leg must still explain itself in the log');
  });
});

describe('T11 — boundary and fail-closed behavior', () => {
  it('a missing entry fails closed, never throws', () => {
    // ASSUMPTION: a gate that cannot read the entry fails closed.
    const { smokeVariant } = loadImpl();
    let result;
    assert.doesNotThrow(() => {
      result = smokeVariant(undefined);
    });
    assert.equal(result.ok, false);
    assert.ok(Array.isArray(result.log));
  });

  it('an unknown runtime fails naming the runtime', () => {
    // ASSUMPTION: only "hermes" and "quickjs" are accepted; anything else
    // fails closed naming the offending value.
    const { smokeVariant } = loadImpl();
    const result = smokeVariant({ source: CLEAN_SOURCE, runtime: 'v8' });
    assert.equal(result.ok, false);
    const named = result.log.some((line) => line.includes('v8'));
    assert.ok(
      named,
      `the log must name the unknown runtime, got: ${JSON.stringify(result.log)}`
    );
  });
});
