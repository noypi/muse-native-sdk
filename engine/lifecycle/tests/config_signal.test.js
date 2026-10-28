'use strict';
// T4 — O(1) per-subscriber config propagation via signal store.
// Plan Interface (verbatim):
// `createConfigSignalStore(): { subscribe(fn: (event: object) => void): function, publish(change: string, invalidation: object): { notified: number } }` — `subscribe` returns an `unsubscribe(): void` (double-unsubscribe safe); crashing subscriber is isolated (counted as notified, error recorded, others still called); `publish` validates against the frozen 13 and throws `CONFIG_UNKNOWN_CHANGE` otherwise.
// Spec refs: FR-LIFECYCLE-006, AC-LIFECYCLE-015 (TC-LIFECYCLE-015 hermetic mirror).
// Hermetic: node:test + node:assert/strict only; no network/fs/secrets/live services.
// ASSUMPTIONs: (a) the store exposes an `errors` array recording isolated
// subscriber errors in publish order ("error recorded" per the plan); (b) the
// published `invalidation` object is delivered by reference to each subscriber
// (no cloning — O(1) fan-out); (c) subscribers added during a publish do not run
// until the next publish (array-snapshot fan-out per the plan Detail), and
// unsubscribing during a publish does not stop the in-flight delivery;
// (d) non-function subscribe arguments and non-string publish changes throw
// (subscribe: any error; publish: CONFIG_UNKNOWN_CHANGE for unknown strings).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createConfigSignalStore } = require('../src/config_signals.js');

const ALL_13 = [
  'locale',
  'layoutDirection',
  'density',
  'fontScale',
  'orientation',
  'screenSize',
  'smallestScreenSize',
  'uiMode',
  'colorMode',
  'keyboard',
  'keyboardHidden',
  'navigation',
  'screenLayout',
];

function dummyInvalidation() {
  return { targets: ['layout'] };
}

describe('TC-LIFECYCLE-015 config signals — module shape', () => {
  it('exports createConfigSignalStore', () => {
    assert.equal(typeof createConfigSignalStore, 'function');
  });

  it('store exposes subscribe and publish', () => {
    const store = createConfigSignalStore();
    assert.equal(typeof store.subscribe, 'function');
    assert.equal(typeof store.publish, 'function');
  });
});

describe('TC-LIFECYCLE-015 config signals — fan-out', () => {
  it('N subscribers each called exactly once per publish with {change, invalidation}', () => {
    const store = createConfigSignalStore();
    const seen = [];
    const invalidation = dummyInvalidation();
    for (let i = 0; i < 5; i += 1) {
      store.subscribe((event) => seen.push(event));
    }
    const result = store.publish('orientation', invalidation);
    assert.equal(result.notified, 5);
    assert.equal(seen.length, 5);
    for (const event of seen) {
      assert.equal(event.change, 'orientation');
      assert.equal(event.invalidation, invalidation);
    }
  });

  it('publish with zero subscribers returns {notified: 0}', () => {
    const store = createConfigSignalStore();
    assert.deepEqual(store.publish('locale', dummyInvalidation()), { notified: 0 });
  });

  it('delivery is synchronous — subscribers ran by the time publish returns', () => {
    const store = createConfigSignalStore();
    let calls = 0;
    store.subscribe(() => {
      calls += 1;
    });
    store.publish('uiMode', dummyInvalidation());
    assert.equal(calls, 1);
  });

  it('all 13 frozen changes publish without throwing', () => {
    const store = createConfigSignalStore();
    let calls = 0;
    store.subscribe(() => {
      calls += 1;
    });
    for (const change of ALL_13) {
      const result = store.publish(change, dummyInvalidation());
      assert.equal(result.notified, 1, `change ${change} must notify the subscriber`);
    }
    assert.equal(calls, ALL_13.length);
  });

  it('call counts scale linearly with subscribers (one call per subscriber)', () => {
    const store = createConfigSignalStore();
    let calls = 0;
    const N = 50;
    for (let i = 0; i < N; i += 1) {
      store.subscribe(() => {
        calls += 1;
      });
    }
    const result = store.publish('fontScale', dummyInvalidation());
    assert.equal(result.notified, N);
    assert.equal(calls, N);
  });
});

describe('TC-LIFECYCLE-015 config signals — unsubscribe', () => {
  it('unsubscribe stops delivery and notified counts active subscribers only', () => {
    const store = createConfigSignalStore();
    let callsA = 0;
    let callsB = 0;
    const unsubA = store.subscribe(() => {
      callsA += 1;
    });
    store.subscribe(() => {
      callsB += 1;
    });
    unsubA();
    const result = store.publish('keyboard', dummyInvalidation());
    assert.equal(result.notified, 1);
    assert.equal(callsA, 0);
    assert.equal(callsB, 1);
  });

  it('double-unsubscribe is safe and does not affect others', () => {
    const store = createConfigSignalStore();
    let calls = 0;
    store.subscribe(() => {
      calls += 1;
    });
    const unsub = store.subscribe(() => {});
    unsub();
    assert.doesNotThrow(() => unsub());
    const result = store.publish('density', dummyInvalidation());
    assert.equal(result.notified, 1);
    assert.equal(calls, 1);
  });

  it('subscribe returns a function', () => {
    const store = createConfigSignalStore();
    const unsub = store.subscribe(() => {});
    assert.equal(typeof unsub, 'function');
    unsub();
  });
});

describe('TC-LIFECYCLE-015 config signals — crashing subscriber isolation', () => {
  it('throwing subscriber is counted, error recorded, others still called', () => {
    const store = createConfigSignalStore();
    const order = [];
    store.subscribe(() => {
      order.push('before');
    });
    store.subscribe(() => {
      throw new Error('boom-not-real');
    });
    store.subscribe(() => {
      order.push('after');
    });
    let result;
    assert.doesNotThrow(() => {
      result = store.publish('screenSize', dummyInvalidation());
    });
    assert.equal(result.notified, 3);
    assert.deepEqual(order, ['before', 'after']);
    assert.ok(Array.isArray(store.errors), 'store must expose a recorded-errors array (ASSUMPTION a)');
    assert.equal(store.errors.length, 1);
  });

  it('two crashing subscribers each recorded, healthy one still called', () => {
    const store = createConfigSignalStore();
    let healthy = 0;
    store.subscribe(() => {
      throw new Error('first-not-real');
    });
    store.subscribe(() => {
      healthy += 1;
    });
    store.subscribe(() => {
      throw new Error('second-not-real');
    });
    const result = store.publish('navigation', dummyInvalidation());
    assert.equal(result.notified, 3);
    assert.equal(healthy, 1);
    assert.equal(store.errors.length, 2);
  });
});

describe('TC-LIFECYCLE-015 config signals — validation', () => {
  it('publish of an unknown change throws CONFIG_UNKNOWN_CHANGE', () => {
    const store = createConfigSignalStore();
    assert.throws(
      () => store.publish('rotation', dummyInvalidation()),
      (err) => err && err.code === 'CONFIG_UNKNOWN_CHANGE'
    );
  });

  it('publish of empty/prototype-pollution strings throws CONFIG_UNKNOWN_CHANGE', () => {
    // Security: untrusted change names must fail loudly, never fan out.
    const store = createConfigSignalStore();
    for (const change of ['', '__proto__', 'a'.repeat(1000)]) {
      assert.throws(
        () => store.publish(change, dummyInvalidation()),
        (err) => err && err.code === 'CONFIG_UNKNOWN_CHANGE'
      );
    }
  });

  it('failed publish notifies nobody', () => {
    const store = createConfigSignalStore();
    let calls = 0;
    store.subscribe(() => {
      calls += 1;
    });
    assert.throws(
      () => store.publish('nope', dummyInvalidation()),
      (err) => err && err.code === 'CONFIG_UNKNOWN_CHANGE'
    );
    assert.equal(calls, 0);
  });
});
