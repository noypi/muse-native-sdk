'use strict';
// TC-TSX-025 (AC-TSX-025) — Batch 1 (T2) of FR-TSX-009.
// Hermetic: node:test + node:assert/strict only. No fs, no network, no
// secrets, no live services, no child processes. The log is a pure string and
// both validators are injected fns, so every case below runs in-process.
// Style mirrors packages/cli/test/*.test.js (describe/it + strict assert).
// T2 Interface (plan verbatim):
//   `appendVerdict(log: string, entry: {validator: "tsc"|"aot", file: string,
//   line: number, column: number, nodePath: string, message: string}): string;
//   runBothValidators(tscFn: () => Diag[], aotFn: () => Diag[]):
//   {log: string, exitCode: 0|1}` — both fns always execute; any diagnostic
//   yields exit 1 and a log line naming validator + location.
// Validator resolution: `../lib/build-log.js` once it lands, else an inline
// stub whose behavior assertions fail, so every behavior case below is red
// until T2 lands — and the harness itself never crashes.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

let buildLog = null;
try {
  buildLog = require('../lib/build-log.js');
} catch {
  // Fallback stub: present so the harness loads; behaviorally wrong so every
  // behavior assertion below fails (red) until lib/build-log.js lands.
  buildLog = {
    appendVerdict: () => '',
    runBothValidators: () => ({ log: '', exitCode: 0 }),
  };
}
const { appendVerdict, runBothValidators } = buildLog;

function tscEntry(overrides = {}) {
  return {
    validator: 'tsc',
    file: 'App.tsx',
    line: 3,
    column: 7,
    nodePath: 'App > View[0]',
    message: "Type 'string' is not assignable to type 'boolean'.",
    ...overrides,
  };
}

function aotEntry(overrides = {}) {
  return {
    validator: 'aot',
    file: 'Card.tsx',
    line: 11,
    column: 5,
    nodePath: 'Card > Text[0]',
    message: 'Illegal expression form: assignment.',
    ...overrides,
  };
}

// ---------------------------------------------------------------------------
// appendVerdict — one log line per verdict, naming validator + location.
// ---------------------------------------------------------------------------
describe('TC-TSX-025 — appendVerdict names validator + file:line:column (AC-TSX-025)', () => {
  it('appends a tsc entry naming the validator and file:line:column', () => {
    const log = appendVerdict('', tscEntry());
    assert.ok(log.includes('tsc'), 'log must name the tsc validator');
    assert.ok(log.includes('App.tsx:3:7'), 'log must carry the file:line:column location');
  });

  it('appends an aot entry naming the validator and file:line:column', () => {
    const log = appendVerdict('', aotEntry());
    assert.ok(log.includes('aot'), 'log must name the aot validator');
    assert.ok(log.includes('Card.tsx:11:5'), 'log must carry the file:line:column location');
  });

  it('preserves the existing log content (appends, never overwrites)', () => {
    const log = appendVerdict('first line\n', tscEntry());
    assert.ok(log.includes('first line'), 'existing log content must be preserved');
    assert.ok(log.includes('tsc'), 'new verdict must still name its validator');
    assert.ok(log.includes('App.tsx:3:7'), 'new verdict must still carry its location');
  });

  it('returns a string carrying the diagnostic message', () => {
    const log = appendVerdict('', aotEntry());
    assert.equal(typeof log, 'string', 'appendVerdict must return a string');
    assert.ok(
      log.includes('Illegal expression form'),
      'log line must carry the diagnostic message',
    );
  });
});

// ---------------------------------------------------------------------------
// runBothValidators — collect-then-exit: both fns always run, either failure
// exits 1, one combined log names every failing validator + location.
// ---------------------------------------------------------------------------
describe('TC-TSX-025 — runBothValidators runs both, exits 1 on either failure (AC-TSX-025)', () => {
  it('runs the aot validator even when the tsc validator returns diagnostics', () => {
    let tscCalls = 0;
    let aotCalls = 0;
    runBothValidators(
      () => { tscCalls += 1; return [tscEntry()]; },
      () => { aotCalls += 1; return []; },
    );
    assert.equal(tscCalls, 1, 'tsc validator must run exactly once');
    assert.equal(aotCalls, 1, 'aot validator must still run after a tsc failure');
  });

  it('runs the tsc validator even when the aot validator would fail (both always execute)', () => {
    let tscCalls = 0;
    let aotCalls = 0;
    const result = runBothValidators(
      () => { tscCalls += 1; return []; },
      () => { aotCalls += 1; return [aotEntry()]; },
    );
    assert.equal(tscCalls, 1, 'tsc validator must run exactly once');
    assert.equal(aotCalls, 1, 'aot validator must run exactly once');
    assert.equal(result.exitCode, 1, 'an aot-only failure must exit 1');
  });

  it('exits 1 with a tsc-named log line on a tsc-only failure', () => {
    const result = runBothValidators(() => [tscEntry()], () => []);
    assert.equal(result.exitCode, 1, 'a tsc-only failure must exit 1');
    assert.ok(result.log.includes('tsc'), 'combined log must name the tsc validator');
    assert.ok(result.log.includes('App.tsx:3:7'), 'combined log must carry the tsc location');
  });

  it('exits 1 with an aot-named log line on an aot-only failure', () => {
    const result = runBothValidators(() => [], () => [aotEntry()]);
    assert.equal(result.exitCode, 1, 'an aot-only failure must exit 1');
    assert.ok(result.log.includes('aot'), 'combined log must name the aot validator');
    assert.ok(result.log.includes('Card.tsx:11:5'), 'combined log must carry the aot location');
  });

  it('exits 1 with ONE combined log naming both validators when both fail', () => {
    const result = runBothValidators(() => [tscEntry()], () => [aotEntry()]);
    assert.equal(result.exitCode, 1, 'a both-fail build must exit 1');
    assert.equal(typeof result.log, 'string', 'combined log must be a single string');
    assert.ok(result.log.includes('tsc'), 'combined log must name the tsc validator');
    assert.ok(result.log.includes('aot'), 'combined log must name the aot validator');
    assert.ok(result.log.includes('App.tsx:3:7'), 'combined log must carry the tsc location');
    assert.ok(result.log.includes('Card.tsx:11:5'), 'combined log must carry the aot location');
  });

  it('exits 0 when neither validator returns diagnostics', () => {
    let tscCalls = 0;
    let aotCalls = 0;
    const result = runBothValidators(
      () => { tscCalls += 1; return []; },
      () => { aotCalls += 1; return []; },
    );
    assert.equal(tscCalls, 1, 'tsc validator must still run on a clean build');
    assert.equal(aotCalls, 1, 'aot validator must still run on a clean build');
    assert.equal(result.exitCode, 0, 'a clean build must exit 0');
  });

  it('exits 1 and logs every location when one validator returns multiple diagnostics', () => {
    const result = runBothValidators(
      () => [tscEntry(), tscEntry({ file: 'Home.tsx', line: 9, column: 2 })],
      () => [],
    );
    assert.equal(result.exitCode, 1, 'multiple diagnostics must exit 1');
    assert.ok(result.log.includes('App.tsx:3:7'), 'combined log must carry the first location');
    assert.ok(result.log.includes('Home.tsx:9:2'), 'combined log must carry the second location');
  });

  it('exits 1 and names the crashed validator when a validator throws', () => {
    const result = runBothValidators(() => { throw new Error('boom'); }, () => []);
    assert.equal(result.exitCode, 1, 'a throwing validator must exit 1');
    assert.ok(result.log.includes('tsc'), 'combined log must name the crashed validator');
  });
});
