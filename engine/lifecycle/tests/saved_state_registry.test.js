'use strict';
// T3 — SavedStateHandle-equivalent scoped registry (Batch 2).
// Plan Interface (verbatim):
// `createSavedStateRegistry(): { scoped(scope: string): { set(key: string, value: unknown): void, get(key: string): unknown, remove(key: string): void, keys(): string[] }, snapshot(): Record<string, Record<string, unknown>>, restore(data: Record<string, Record<string, unknown>>): void, clear(scope?: string): void }` — cross-scope reads return undefined; non-JSON-serializable value on set throws `SAVED_STATE_UNENCODABLE` naming scope+key.
// Spec refs: FR-LIFECYCLE-005, AC-LIFECYCLE-013 (TC-LIFECYCLE-013 part).
// Mirrors TC-LIFECYCLE-013 (save_restore_test.cpp), registry-boundary part.
// Hermetic: node:test + node:assert/strict only; no network/fs/secrets/live services.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createSavedStateRegistry } = require('../src/saved_state_registry.js');

function assertUnencodableThrows(fn, scope, key) {
  assert.throws(
    fn,
    (err) => {
      assert.match(String((err && err.code) || ''), /SAVED_STATE_UNENCODABLE/);
      assert.match(String((err && err.message) || ''), /SAVED_STATE_UNENCODABLE/);
      assert.match(String((err && err.message) || ''), new RegExp(scope));
      assert.match(String((err && err.message) || ''), new RegExp(key));
      return true;
    },
    `expected SAVED_STATE_UNENCODABLE naming scope ${JSON.stringify(scope)} and key ${JSON.stringify(key)}`
  );
}

describe('TC-LIFECYCLE-013 registry — scoped set/get/keys/remove', () => {
  let registry;
  beforeEach(() => {
    registry = createSavedStateRegistry();
  });

  it('scoped handle exposes set/get/remove/keys with the contracted shapes', () => {
    const handle = registry.scoped('screen-a');
    assert.equal(typeof handle.set, 'function');
    assert.equal(typeof handle.get, 'function');
    assert.equal(typeof handle.remove, 'function');
    assert.equal(typeof handle.keys, 'function');
    assert.deepEqual(handle.keys(), []);
    assert.equal(registry.scoped, registry.scoped);
  });

  it('set then get round-trips plain JSON values', () => {
    const handle = registry.scoped('screen-a');
    handle.set('count', 3);
    handle.set('label', 'hello');
    handle.set('flag', true);
    handle.set('nothing', null);
    handle.set('list', [1, 2, 3]);
    handle.set('obj', { x: 1, nested: { y: [1, 2] } });
    assert.equal(handle.get('count'), 3);
    assert.equal(handle.get('label'), 'hello');
    assert.equal(handle.get('flag'), true);
    assert.equal(handle.get('nothing'), null);
    assert.deepEqual(handle.get('list'), [1, 2, 3]);
    assert.deepEqual(handle.get('obj'), { x: 1, nested: { y: [1, 2] } });
  });

  it('set overwrites an existing key', () => {
    const handle = registry.scoped('screen-a');
    handle.set('k', 'first');
    handle.set('k', 'second');
    assert.equal(handle.get('k'), 'second');
  });

  it('get of a missing key returns undefined', () => {
    assert.equal(registry.scoped('screen-a').get('absent'), undefined);
  });

  it('keys lists exactly the keys in insertion scope', () => {
    const handle = registry.scoped('screen-a');
    handle.set('b', 1);
    handle.set('a', 2);
    assert.deepEqual(handle.keys().sort(), ['a', 'b']);
  });

  it('remove deletes the key and it disappears from keys', () => {
    const handle = registry.scoped('screen-a');
    handle.set('k', 1);
    handle.remove('k');
    assert.equal(handle.get('k'), undefined);
    assert.deepEqual(handle.keys(), []);
  });

  it('remove of a missing key is a safe no-op', () => {
    // ASSUMPTION: plan is silent on removing absent keys; most defensible is no-throw.
    const handle = registry.scoped('screen-a');
    handle.remove('absent');
    assert.deepEqual(handle.keys(), []);
  });
});

describe('TC-LIFECYCLE-013 registry — scopes are isolated', () => {
  it('cross-scope reads return undefined', () => {
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('k', 'value-a');
    assert.equal(registry.scoped('screen-b').get('k'), undefined);
    assert.deepEqual(registry.scoped('screen-b').keys(), []);
  });

  it('same key in two scopes holds independent values', () => {
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('k', 'a');
    registry.scoped('screen-b').set('k', 'b');
    assert.equal(registry.scoped('screen-a').get('k'), 'a');
    assert.equal(registry.scoped('screen-b').get('k'), 'b');
  });

  it('remove in one scope leaves the same key in another scope intact', () => {
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('k', 'a');
    registry.scoped('screen-b').set('k', 'b');
    registry.scoped('screen-a').remove('k');
    assert.equal(registry.scoped('screen-a').get('k'), undefined);
    assert.equal(registry.scoped('screen-b').get('k'), 'b');
  });
});

describe('TC-LIFECYCLE-013 registry — snapshot/clear/restore round-trip', () => {
  it('snapshot captures per-scope records; clear() then restore returns identical values', () => {
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('count', 3);
    registry.scoped('screen-b').set('label', 'hi');
    const snap = registry.snapshot();
    assert.deepEqual(snap, { 'screen-a': { count: 3 }, 'screen-b': { label: 'hi' } });
    registry.clear();
    assert.equal(registry.scoped('screen-a').get('count'), undefined);
    assert.equal(registry.scoped('screen-b').get('label'), undefined);
    registry.restore(snap);
    assert.equal(registry.scoped('screen-a').get('count'), 3);
    assert.equal(registry.scoped('screen-b').get('label'), 'hi');
  });

  it('clear(scope) leaves other scopes intact', () => {
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('k', 1);
    registry.scoped('screen-b').set('k', 2);
    registry.clear('screen-a');
    assert.equal(registry.scoped('screen-a').get('k'), undefined);
    assert.equal(registry.scoped('screen-b').get('k'), 2);
  });

  it('clear() with no argument empties every scope', () => {
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('k', 1);
    registry.scoped('screen-b').set('k', 2);
    registry.clear();
    assert.deepEqual(registry.snapshot(), {});
  });

  it('restore replaces current state rather than merging', () => {
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('stale', true);
    registry.restore({ 'screen-b': { fresh: 1 } });
    assert.equal(registry.scoped('screen-a').get('stale'), undefined);
    assert.equal(registry.scoped('screen-b').get('fresh'), 1);
  });

  it('snapshot is a deep copy: mutating it does not affect the registry', () => {
    // ASSUMPTION: plan does not pin copy semantics; deep-copy snapshot is the most
    // defensible Bundle-bridge behavior (callers must not alias registry memory).
    const registry = createSavedStateRegistry();
    registry.scoped('screen-a').set('obj', { x: 1 });
    const snap = registry.snapshot();
    snap['screen-a'].obj.x = 999;
    assert.deepEqual(registry.scoped('screen-a').get('obj'), { x: 1 });
  });

  it('restore deep-copies its input: later mutation of the argument is not observed', () => {
    // ASSUMPTION: symmetric to the snapshot copy rule above.
    const registry = createSavedStateRegistry();
    const data = { 'screen-a': { obj: { x: 1 } } };
    registry.restore(data);
    data['screen-a'].obj.x = 999;
    assert.deepEqual(registry.scoped('screen-a').get('obj'), { x: 1 });
  });
});

describe('TC-LIFECYCLE-013 registry — unencodable values throw SAVED_STATE_UNENCODABLE', () => {
  it('circular value on set throws naming scope and key and stores nothing', () => {
    const registry = createSavedStateRegistry();
    const circular = { self: null };
    circular.self = circular;
    assertUnencodableThrows(() => registry.scoped('screen-a').set('loop', circular), 'screen-a', 'loop');
    assert.equal(registry.scoped('screen-a').get('loop'), undefined);
  });

  it('BigInt value on set throws naming scope and key', () => {
    const registry = createSavedStateRegistry();
    assertUnencodableThrows(() => registry.scoped('screen-a').set('big', 10n), 'screen-a', 'big');
  });

  it('function value on set throws naming scope and key', () => {
    // ASSUMPTION: JSON.stringify(function) yields undefined rather than throwing,
    // so the registry must treat undefined-after-stringify as unencodable; functions
    // can never cross the Bundle bridge. Error names scope+key per the contract.
    const registry = createSavedStateRegistry();
    assertUnencodableThrows(() => registry.scoped('screen-b').set('cb', () => {}), 'screen-b', 'cb');
  });

  it('a failed set does not disturb neighboring keys', () => {
    const registry = createSavedStateRegistry();
    const handle = registry.scoped('screen-a');
    handle.set('ok', 1);
    const circular = {};
    circular.me = circular;
    assertUnencodableThrows(() => handle.set('bad', circular), 'screen-a', 'bad');
    assert.equal(handle.get('ok'), 1);
    assert.deepEqual(handle.keys(), ['ok']);
  });
});
