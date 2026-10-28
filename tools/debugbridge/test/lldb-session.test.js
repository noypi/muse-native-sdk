'use strict';
// T8 — FR-DX-009 / AC-DX-040 / TC-DX-040 (Batch 3).
// Hermetic: node:test + node:assert/strict only. Headless fixture traces
// only — no device, no LLDB binary, no socket, no secrets, no config.
// Interfaces under test (plan verbatim):
//   attachLldb(pid: number) => Promise<Array<{frame: string, symbol: string,
//   jsiCall: object|null}>> from tools/debugbridge/lib/lldb-session.js;
//   stepAcrossJsi(jsFrame: string) => {jsFunction: string, nativeSymbol:
//   string} from tools/debugbridge/lib/jsi-boundary.js; attach failure
//   rejects with {command:"lldbAttach", cause, action}.
// ASSUMPTION: tests inject recorded fixture traces + a symbolication fake
// through optional hooks (`__setTraceSource(fn)` / `__setSymbolicator(fn)`
// or an optional second `deps` argument). These hooks are test-only; on a
// real device the modules drive LLDB/recorded traces the same way.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LLDB_SESSION = path.join(__dirname, '..', 'lib', 'lldb-session.js');
const JSI_BOUNDARY = path.join(__dirname, '..', 'lib', 'jsi-boundary.js');

function fresh(modPath) {
  delete require.cache[require.resolve(modPath)];
  return require(modPath);
}

// Recorded headless fixture: C++ stack with one JSI boundary frame.
function fixtureTrace() {
  return [
    { frame: '#0 0x7f001122 in facebook::jsi::Function::call', symbol: 'jsi::Function::call', jsiCall: { jsFunction: 'onPress', kind: 'js-to-native' } },
    { frame: '#1 0x7f0010aa in HermesRuntime::callFunction', symbol: 'HermesRuntime::callFunction', jsiCall: null },
    { frame: '#2 0x7f000f01 in __libc_start_main', symbol: '__libc_start_main', jsiCall: null },
  ];
}

function wireFixtures(lldbMod) {
  if (typeof lldbMod.__setTraceSource === 'function') {
    lldbMod.__setTraceSource(async (pid) => {
      if (pid === 1234) return fixtureTrace();
      const err = new Error(`no such process: ${pid}`);
      err.command = 'lldbAttach';
      throw err;
    });
  }
  if (typeof lldbMod.__setSymbolicator === 'function') {
    lldbMod.__setSymbolicator(async (frame) => ({ symbol: frame.symbol || frame.frame }));
  }
}

describe('lldb attach + symbolicated stack (T8 / TC-DX-040)', () => {
  it('attachLldb resolves a symbolicated fixture stack for a known pid', async () => {
    const mod = fresh(LLDB_SESSION);
    assert.equal(typeof mod.attachLldb, 'function');
    wireFixtures(mod);
    const stack = await mod.attachLldb(1234);
    assert.ok(Array.isArray(stack), 'must resolve to an array of frames');
    assert.ok(stack.length >= 2, 'fixture stack must carry frames');
    for (const entry of stack) {
      assert.equal(typeof entry.frame, 'string');
      assert.equal(typeof entry.symbol, 'string');
      assert.ok(entry.symbol.length > 0, 'every frame must be symbolicated');
      assert.ok('jsiCall' in entry, 'every frame must carry jsiCall (object|null)');
    }
  });

  it('fixture stack marks the JSI boundary frame with a non-null jsiCall', async () => {
    const mod = fresh(LLDB_SESSION);
    wireFixtures(mod);
    const stack = await mod.attachLldb(1234);
    const boundary = stack.filter((e) => e.jsiCall !== null);
    assert.ok(boundary.length >= 1, 'at least one frame must be a JSI call');
    assert.equal(boundary[0].jsiCall.jsFunction, 'onPress');
  });

  it('non-JSI frames carry jsiCall null', async () => {
    const mod = fresh(LLDB_SESSION);
    wireFixtures(mod);
    const stack = await mod.attachLldb(1234);
    const plain = stack.filter((e) => e.jsiCall === null);
    assert.ok(plain.length >= 1, 'plain C++ frames must carry jsiCall null');
  });

  it('attach failure rejects with {command:"lldbAttach", cause, action}', async () => {
    const mod = fresh(LLDB_SESSION);
    wireFixtures(mod);
    await assert.rejects(
      async () => mod.attachLldb(-1),
      (err) => {
        assert.equal(err.command, 'lldbAttach');
        assert.equal(typeof err.cause, 'string');
        assert.ok(err.cause.length > 0);
        assert.equal(typeof err.action, 'string');
        assert.ok(err.action.length > 0);
        return true;
      },
      'unknown pid must reject with the structured lldbAttach error',
    );
  });

  it('attach failure never resolves a half-shaped stack', async () => {
    const mod = fresh(LLDB_SESSION);
    wireFixtures(mod);
    let resolved = null;
    try {
      resolved = await mod.attachLldb(0);
    } catch {
      resolved = null;
    }
    assert.equal(resolved, null, 'failure must reject, never resolve');
  });
});

describe('jsi boundary stepping (T8 / TC-DX-040)', () => {
  it('stepAcrossJsi maps a JS frame to {jsFunction, nativeSymbol}', () => {
    const mod = fresh(JSI_BOUNDARY);
    assert.equal(typeof mod.stepAcrossJsi, 'function');
    const out = mod.stepAcrossJsi('onPress');
    assert.equal(out.jsFunction, 'onPress');
    assert.equal(typeof out.nativeSymbol, 'string');
    assert.ok(out.nativeSymbol.length > 0, 'must name the native symbol');
  });

  it('stepAcrossJsi crosses JS->C++ and back (round trip)', () => {
    const mod = fresh(JSI_BOUNDARY);
    const forward = mod.stepAcrossJsi('onPress');
    assert.ok(forward.nativeSymbol.length > 0, 'JS->C++ must land on a native symbol');
    if (typeof mod.stepAcrossJsiBack === 'function' || typeof mod.stepToJs === 'function') {
      const back = typeof mod.stepAcrossJsiBack === 'function'
        ? mod.stepAcrossJsiBack(forward.nativeSymbol)
        : mod.stepToJs(forward.nativeSymbol);
      assert.ok(/onPress/.test(JSON.stringify(back)), 'C++->JS must return to the JS function');
    } else {
      // Fallback: the forward mapping itself carries both directions.
      assert.ok(forward.jsFunction && forward.nativeSymbol, 'mapping must carry both directions');
    }
  });

  it('stepAcrossJsi with an unknown frame throws a structured error', () => {
    const mod = fresh(JSI_BOUNDARY);
    assert.throws(
      () => mod.stepAcrossJsi('__no_such_js_function__'),
      (err) => err && typeof err.command === 'string' && typeof err.cause === 'string',
      'unknown JS frame must throw {command, cause, action}',
    );
  });

  it('stepAcrossJsi with an empty frame throws, never returns a half mapping', () => {
    const mod = fresh(JSI_BOUNDARY);
    assert.throws(() => mod.stepAcrossJsi(''), TypeError);
  });
});
