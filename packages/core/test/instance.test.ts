'use strict';
// T3 — component instance model contract (Batch 1).
// Plan Interface (verbatim): `createInstance(body: () => unknown):
// InstanceHandle` — runs `body` exactly once at mount setup and returns the
// handle; `mount(handle: InstanceHandle): void` — fires `onBeforeMount` hooks,
// attaches subtree, fires `onMounted`; `unmount(handle: InstanceHandle): void`
// — fires `onBeforeUnmount`, disposes owned registrations, detaches, fires
// `onUnmounted`; `getCurrentInstance(): InstanceHandle | null` — the body
// currently executing (null outside). Importing
// `useState`/`useEffect`/`useMemo`/`useCallback`/`useRef` from `@muse/core`
// fails typecheck (`index.d.ts` omits them) and throws
// `MUSE_CORE_UNKNOWN_IMPORT` naming the banned import at require/bundle time.
// Spec refs: AC-MCORE-001, AC-MCORE-002, RULE-MCORE-001, RULE-MCORE-004
// (TC-MCORE-001/002 → AC-MCORE-001/002).
// Hermetic: node:test + node:assert/strict only (+ node:fs/path to read the
// committed index.d.ts for the typecheck-omission half); no network, no
// secrets, no live services.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const {
  createInstance,
  mount,
  unmount,
  getCurrentInstance,
} = require('../lib/instance.js');

const BANNED_HOOKS = ['useState', 'useEffect', 'useMemo', 'useCallback', 'useRef'];

describe('TC-MCORE-001 — body runs exactly once per instance (AC-MCORE-001)', () => {
  it('createInstance runs the body exactly once at mount setup', () => {
    let bodyRuns = 0;
    const handle = createInstance(() => { bodyRuns += 1; });
    assert.equal(bodyRuns, 1);
    assert.ok(handle);
  });

  it('mount does not re-execute the body', () => {
    let bodyRuns = 0;
    const handle = createInstance(() => { bodyRuns += 1; });
    mount(handle);
    mount(handle);
    assert.equal(bodyRuns, 1);
  });

  it('writes after mount do not re-execute the body (body-once; state rebinding is T4)', () => {
    // Scheduler/state-free analogue of AC-MCORE-001: the instance contract
    // alone guarantees the body is never re-entered after setup. Full
    // "state write updates only the bound node" is verified in T4/effects.
    let bodyRuns = 0;
    const handle = createInstance(() => { bodyRuns += 1; });
    mount(handle);
    unmount(handle);
    assert.equal(bodyRuns, 1);
  });

  it('two instances of the same body run it once each and own independent handles', () => {
    let bodyRuns = 0;
    const body = () => { bodyRuns += 1; };
    const h1 = createInstance(body);
    const h2 = createInstance(body);
    assert.equal(bodyRuns, 2);
    assert.notEqual(h1, h2);
  });

  it('getCurrentInstance returns the executing body inside and null outside', () => {
    let seenInside = 'unset';
    const handle = createInstance(() => {
      seenInside = getCurrentInstance();
    });
    assert.equal(seenInside, handle);
    assert.equal(getCurrentInstance(), null);
  });

  it('unmount of a never-mounted instance is safe and does not re-run the body (ASSUMPTION)', () => {
    // ASSUMPTION: unmount is idempotent and safe on a non-mounted handle;
    // plan/spec do not settle double-unmount. Minimal assertion: no throw,
    // body still ran exactly once.
    let bodyRuns = 0;
    const handle = createInstance(() => { bodyRuns += 1; });
    assert.doesNotThrow(() => { unmount(handle); });
    assert.doesNotThrow(() => { unmount(handle); });
    assert.equal(bodyRuns, 1);
  });
});

describe('TC-MCORE-002 — banned React-hook imports fail named (AC-MCORE-002)', () => {
  it('index.d.ts omits every banned hook name (typecheck half)', () => {
    const dts = fs.readFileSync(
      path.join(__dirname, '..', 'index.d.ts'),
      'utf8'
    );
    for (const name of BANNED_HOOKS) {
      assert.ok(
        !new RegExp(`\\b${name}\\b`).test(dts),
        `index.d.ts must not mention ${name}`
      );
    }
  });

  for (const name of BANNED_HOOKS) {
    it(`requiring ${name} from @muse/core throws MUSE_CORE_UNKNOWN_IMPORT naming it`, () => {
      const mod = require('../index.js');
      assert.throws(
        () => {
          // Coder's mechanism (getter/Proxy/named export stub) must throw on
          // access; acceptance is the observable failure, not the mechanism.
          // eslint-disable-next-line no-unused-expressions
          mod[name];
        },
        (err) => {
          assert.equal(err && err.name, 'MUSE_CORE_UNKNOWN_IMPORT');
          assert.match(String((err && err.message) || ''), new RegExp(name));
          assert.match(String((err && err.message) || ''), /@muse\/core/);
          return true;
        }
      );
    });
  }
});
