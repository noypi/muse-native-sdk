'use strict';
// T9 — thin-host Batch-2 unblocker: JNI seam genericization + transport binding.
// Hermetic: node:test + node:assert/strict only. Static source pins over
// `engine/runtime/src/jni_bridge.cpp` + seam-level fail-closed behavior via
// `examples/muse-demo/src/demoNav.js` with no transport (unbound bridge).
// No device, no downloads, no secrets, no config, no env mutation.
// Specs: AC-NAV-041 (transport binding), AC-NAV-042 (generic seam + gate),
// TC-NAV-042 (gate, covered by engine/navigation/tests/router_facade_gate_test.cpp).
//
// RED expectation: seam still ungenericized (nativeSetDemoShellState present),
// unbound push throws instead of failing closed. The bound-at-boot pin is
// T1.3-STOPPED (no code sets globalThis.RouterBridge; pinning where the
// binding happens would invent the mechanism) — see unit-tests/T9.md.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join } = require('node:path');

const RUN_ROOT = join(__dirname, '..', '..', '..');
const JNI_PATH = join(RUN_ROOT, 'engine', 'runtime', 'src', 'jni_bridge.cpp');
const { createDemoRouter } = require('../src/demoNav.js');

function read(p) {
  return readFileSync(p, 'utf8');
}

describe('T9 — JNI seam is generic (AC-NAV-042, TC-NAV-047)', () => {
  it('jni_bridge.cpp exposes nativeSetShellState(shellRoute, shellNotice)', () => {
    const src = read(JNI_PATH);
    assert.match(
      src,
      /nativeSetShellState/,
      'seam must expose nativeSetShellState(shellRoute, shellNotice)',
    );
  });

  it('jni_bridge.cpp carries no demo seam (nativeSetDemoShellState/demoTab/demoDeniedLink absent)', () => {
    const src = read(JNI_PATH);
    assert.ok(
      !src.includes('nativeSetDemoShellState'),
      'demo-named JNI entry must be gone (found nativeSetDemoShellState)',
    );
    assert.ok(!src.includes('demoTab'), 'demoTab field must be gone');
    assert.ok(!src.includes('demoDeniedLink'), 'demoDeniedLink field must be gone');
  });
});

describe('T9 — unbound bridge fails closed (AC-NAV-041; T4 failure mode)', () => {
  it('push with no transport returns a named denial, never throws, page unchanged', async () => {
    const router = createDemoRouter({ transport: undefined });
    const before = router.current;
    let res;
    try {
      res = await router.push('lens');
    } catch (e) {
      assert.fail(`unbound push must not throw (got ${e && e.constructor && e.constructor.name})`);
    }
    assert.ok(res && typeof res === 'object', 'must return a NavResult object');
    assert.equal(res.ok, false, 'unbound push must fail closed, never {ok:true} silent no-op');
    assert.ok(typeof res.code === 'string' && res.code.length > 0, 'denial must carry a named code');
    assert.equal(router.current, before, 'denial leaves current unchanged');
  });

  it('forwardIntent with no transport fails closed, never throws, page unchanged', async () => {
    const router = createDemoRouter({ transport: undefined });
    const before = router.current;
    let res;
    try {
      res = await router.forwardIntent('muse-demo://tab/lens');
    } catch (e) {
      assert.fail(`unbound forwardIntent must not throw (got ${e && e.constructor && e.constructor.name})`);
    }
    assert.ok(res && typeof res === 'object', 'must return a NavResult object');
    assert.equal(res.ok, false, 'unbound forwardIntent must fail closed');
    assert.equal(router.current, before, 'denial leaves current unchanged');
  });
});
