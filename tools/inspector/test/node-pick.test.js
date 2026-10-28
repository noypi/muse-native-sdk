'use strict';
// T5 — FR-DX-003 / AC-DX-014 / TC-DX-014 (Batch 2).
// Hermetic: node:test + node:assert/strict only. Layout + signal seams are
// in-process stubs ({ layoutSource, signalOf }); no device, no dev-channel
// server, no CDP bridge, no log instrumentation.
// Interface under test (plan verbatim):
//   pickNode(nodeId, deps) => { nodeId, computedBox, winningDeclaration,
//     lastInvalidation } | throws { code: 'E_INSPECTOR_UNKNOWN_NODE', cause, action }
//   where deps = { layoutSource, signalOf }
// Manual TC-DX-014 steps (run app, attach inspector, select node, confirm
// box + declaration + signal without log instrumentation) are represented
// here by the automated composition test plus this documented script.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'node-pick.js');

function load() {
  return require(LIB);
}

function stubDeps() {
  const layoutSource = {
    getComputedBox(nodeId) {
      if (nodeId === 'node-1') return { x: 0, y: 64, width: 390, height: 120 };
      return undefined;
    },
    getWinningDeclaration(nodeId) {
      if (nodeId === 'node-1') {
        return { declaration: '.card { height: 120px; }', origin: 'app/styles/card.css:12' };
      }
      return undefined;
    },
  };
  const signalOf = (nodeId) => {
    if (nodeId === 'node-1') return { handle: 'signal:theme', atSeq: 7 };
    return null;
  };
  return { layoutSource, signalOf };
}

let consoleSpies = [];
function silenceConsole() {
  for (const m of ['log', 'info', 'debug', 'warn', 'error']) {
    const orig = console[m];
    let calls = 0;
    console[m] = (...args) => {
      calls += 1;
      return undefined;
    };
    consoleSpies.push({ m, orig, getCalls: () => calls });
  }
}
afterEach(() => {
  for (const s of consoleSpies) console[s.m] = s.orig;
  consoleSpies = [];
});

describe('pickNode', () => {
  it('returns box, winning declaration and last invalidation for a rendered node', () => {
    const { pickNode } = load();
    const out = pickNode('node-1', stubDeps());
    assert.deepEqual(Object.keys(out).sort(), [
      'computedBox',
      'lastInvalidation',
      'nodeId',
      'winningDeclaration',
    ]);
    assert.equal(out.nodeId, 'node-1');
    assert.deepEqual(out.computedBox, { x: 0, y: 64, width: 390, height: 120 });
    assert.equal(out.winningDeclaration.declaration, '.card { height: 120px; }');
    assert.equal(out.winningDeclaration.origin, 'app/styles/card.css:12');
    assert.deepEqual(out.lastInvalidation, { handle: 'signal:theme', atSeq: 7 });
  });

  it('performs zero log-instrumentation calls', () => {
    const { pickNode } = load();
    silenceConsole();
    pickNode('node-1', stubDeps());
    for (const s of consoleSpies) {
      assert.equal(s.getCalls(), 0, `console.${s.m} must not be called`);
    }
  });

  it('throws E_INSPECTOR_UNKNOWN_NODE for an unknown node', () => {
    const { pickNode } = load();
    assert.throws(
      () => pickNode('no-such-node', stubDeps()),
      (err) => {
        assert.equal(err.code, 'E_INSPECTOR_UNKNOWN_NODE');
        assert.ok(err.cause && typeof err.cause === 'string', 'cause must be present');
        assert.ok(err.action && typeof err.action === 'string', 'action must be present');
        assert.match(String(err.cause) + String(err.action), /DX-003|AC-DX-014|inspector/i);
        return true;
      },
    );
  });

  it('throws E_INSPECTOR_UNKNOWN_NODE for a missing or empty node id (ASSUMPTION)', () => {
    const { pickNode } = load();
    for (const bad of [undefined, null, '']) {
      assert.throws(() => pickNode(bad, stubDeps()), (err) => err.code === 'E_INSPECTOR_UNKNOWN_NODE');
    }
  });

  it('reports null lastInvalidation when no signal maps to the node (ASSUMPTION)', () => {
    const { pickNode } = load();
    const deps = stubDeps();
    deps.signalOf = () => null;
    // Extend the layout stub so the node is known but has no signal.
    deps.layoutSource.getComputedBox = (id) =>
      (id === 'node-9' ? { x: 1, y: 2, width: 3, height: 4 } : stubDeps().layoutSource.getComputedBox(id));
    deps.layoutSource.getWinningDeclaration = (id) =>
      (id === 'node-9'
        ? { declaration: '.x {}', origin: 'app/styles/x.css:1' }
        : stubDeps().layoutSource.getWinningDeclaration(id));
    const out = pickNode('node-9', deps);
    assert.equal(out.lastInvalidation, null);
  });

  it('rejects a hostile node id without leaking internals (ASSUMPTION)', () => {
    const { pickNode } = load();
    assert.throws(
      () => pickNode('__proto__', stubDeps()),
      (err) => {
        assert.match(String(err.code), /^E_INSPECTOR_/);
        assert.doesNotMatch(String(err.cause ?? ''), /node_modules|at\s+Object\./);
        return true;
      },
    );
  });
});
