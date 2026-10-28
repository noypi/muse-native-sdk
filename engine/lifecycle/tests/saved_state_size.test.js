'use strict';
// T4 — Bundle IPC size guard with disk spill (Batch 3).
// Plan Interface (verbatim):
// `createSizeGuard(storage: { write(key: string, value: string): void, read(key: string): string | undefined, remove(key: string): void }, limitBytes?: number): { pack(snapshot: object): { bundle: object, spilled: string[] }, unpack(packed: { bundle: object, spilled: string[] }): object } }` — default limit `BUNDLE_IPC_LIMIT_BYTES = 512000`; oversize scope payloads move to storage and are replaced in-bundle with `{__spilled: key}` tombstones; unpack rehydrates or throws `SAVED_STATE_SPILL_MISSING` naming the key.
// Spec refs: FR-LIFECYCLE-005, AC-LIFECYCLE-011 (TC-LIFECYCLE-011).
// Mirrors TC-LIFECYCLE-011 (saved_state_size_test.cpp) and the TransactionTooLargeException guard.
// Hermetic: node:test + node:assert/strict only; injected in-memory KV storage fake; no network/fs/secrets.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');

const { createSizeGuard, BUNDLE_IPC_LIMIT_BYTES } = require('../src/size_guard.js');

function createFakeStorage() {
  const map = new Map();
  return {
    write(key, value) { map.set(key, value); },
    read(key) { return map.has(key) ? map.get(key) : undefined; },
    remove(key) { map.delete(key); },
    _map: map,
  };
}

function utf8Bytes(value) {
  return Buffer.byteLength(JSON.stringify(value), 'utf8');
}

function assertSpillMissingThrows(fn, key) {
  assert.throws(
    fn,
    (err) => {
      assert.match(String((err && err.code) || ''), /SAVED_STATE_SPILL_MISSING/);
      assert.match(String((err && err.message) || ''), /SAVED_STATE_SPILL_MISSING/);
      assert.match(String((err && err.message) || ''), new RegExp(key));
      return true;
    },
    `expected SAVED_STATE_SPILL_MISSING naming key ${JSON.stringify(key)}`
  );
}

describe('TC-LIFECYCLE-011 size guard — module shape and default limit', () => {
  it('exports createSizeGuard and BUNDLE_IPC_LIMIT_BYTES = 512000', () => {
    assert.equal(typeof createSizeGuard, 'function');
    assert.equal(BUNDLE_IPC_LIMIT_BYTES, 512000);
  });

  it('guard exposes pack and unpack functions', () => {
    const guard = createSizeGuard(createFakeStorage());
    assert.equal(typeof guard.pack, 'function');
    assert.equal(typeof guard.unpack, 'function');
  });
});

describe('TC-LIFECYCLE-011 size guard — under-limit payload stays in bundle', () => {
  let storage;
  beforeEach(() => { storage = createFakeStorage(); });

  it('small snapshot packs with spilled:[] and bundle deep-equal to input', () => {
    const guard = createSizeGuard(storage);
    const snapshot = { 'screen-a': { count: 3 }, 'screen-b': { label: 'hi' } };
    const packed = guard.pack(snapshot);
    assert.deepEqual(packed.spilled, []);
    assert.deepEqual(packed.bundle, snapshot);
  });

  it('unpack of an unspilled pack returns an identical object', () => {
    const guard = createSizeGuard(storage);
    const snapshot = { 'screen-a': { count: 3 } };
    const packed = guard.pack(snapshot);
    assert.deepEqual(guard.unpack(packed), snapshot);
  });

  it('empty snapshot packs to empty bundle with no spills', () => {
    const guard = createSizeGuard(storage);
    const packed = guard.pack({});
    assert.deepEqual(packed.bundle, {});
    assert.deepEqual(packed.spilled, []);
    assert.deepEqual(guard.unpack(packed), {});
  });

  it('payload exactly at the limit stays inline (boundary inclusive)', () => {
    // ASSUMPTION: plan is silent on the exact boundary; most defensible is
    // "spill only when strictly over the limit" (<= stays in bundle).
    const small = createSizeGuard(storage, 1000000);
    const snapshot = { 'screen-a': { v: 'x'.repeat(10) } };
    const size = utf8Bytes(snapshot['screen-a']);
    const guard = createSizeGuard(storage, size);
    const packed = guard.pack(snapshot);
    assert.deepEqual(packed.spilled, []);
    assert.deepEqual(packed.bundle, snapshot);
    void small;
  });
});

describe('TC-LIFECYCLE-011 size guard — oversize scope spills per scope', () => {
  let storage;
  beforeEach(() => { storage = createFakeStorage(); });

  it('one oversize scope spills to storage with a tombstone; unpack restores byte-identical', () => {
    const guard = createSizeGuard(storage, 40);
    const big = { text: 'x'.repeat(200) };
    const snapshot = { 'screen-a': { tiny: 1 }, 'screen-b': big };
    const packed = guard.pack(snapshot);
    assert.deepEqual(packed.spilled, ['screen-b']);
    assert.deepEqual(packed.bundle['screen-a'], { tiny: 1 });
    assert.deepEqual(packed.bundle['screen-b'], { __spilled: 'screen-b' });
    // Spilled payload persisted as UTF-8 JSON, byte-identical to the source scope.
    assert.equal(storage.read('screen-b'), JSON.stringify(big));
    assert.deepEqual(guard.unpack(packed), snapshot);
  });

  it('never splits a scope: the whole scope value moves to storage', () => {
    const guard = createSizeGuard(storage, 30);
    const snapshot = { 'screen-a': { a: 1, b: 2, c: 'x'.repeat(100) } };
    const packed = guard.pack(snapshot);
    assert.deepEqual(packed.spilled, ['screen-a']);
    assert.deepEqual(Object.keys(packed.bundle['screen-a']), ['__spilled']);
    assert.deepEqual(guard.unpack(packed), snapshot);
  });

  it('small scopes stay inline while only the oversize scope spills', () => {
    const guard = createSizeGuard(storage, 50);
    const snapshot = {
      'screen-a': { n: 1 },
      'screen-b': { blob: 'y'.repeat(300) },
      'screen-c': { m: 2 },
    };
    const packed = guard.pack(snapshot);
    assert.deepEqual(packed.spilled, ['screen-b']);
    assert.deepEqual(packed.bundle['screen-a'], { n: 1 });
    assert.deepEqual(packed.bundle['screen-c'], { m: 2 });
    assert.deepEqual(guard.unpack(packed), snapshot);
  });

  it('total in-bundle bytes stay under the limit after spilling', () => {
    const limit = 60;
    const guard = createSizeGuard(storage, limit);
    const snapshot = { 'screen-a': { keep: 1 }, 'screen-b': { blob: 'z'.repeat(500) } };
    const packed = guard.pack(snapshot);
    assert.ok(packed.spilled.length > 0);
    assert.ok(utf8Bytes(packed.bundle) <= limit);
  });

  it('size is measured in UTF-8 JSON bytes (multibyte chars count fully)', () => {
    // 'é' is 2 bytes in UTF-8 but 1 UTF-16 code unit: byte-length accounting
    // must spill earlier than string-length accounting would.
    // ASSUMPTION: plan says "UTF-8 JSON encoding"; tests pin the multibyte rule.
    const oneChar = utf8Bytes('é');
    assert.equal(oneChar, 4); // JSON '"é"' -> quote + 2 bytes + quote
    const guard = createSizeGuard(storage, 3);
    const snapshot = { 'screen-a': 'é' };
    const packed = guard.pack(snapshot);
    assert.deepEqual(packed.spilled, ['screen-a']);
    assert.deepEqual(guard.unpack(packed), snapshot);
  });

  it('custom limitBytes overrides the default 512000 cap', () => {
    const guard = createSizeGuard(storage, 10);
    const packed = guard.pack({ 'screen-a': { v: 'x'.repeat(50) } });
    assert.deepEqual(packed.spilled, ['screen-a']);
  });
});

describe('TC-LIFECYCLE-011 size guard — unpack errors and storage seam', () => {
  let storage;
  beforeEach(() => { storage = createFakeStorage(); });

  it('missing spill entry throws SAVED_STATE_SPILL_MISSING naming the key', () => {
    const guard = createSizeGuard(storage, 40);
    const packed = guard.pack({ 'screen-a': { blob: 'x'.repeat(200) } });
    assert.deepEqual(packed.spilled, ['screen-a']);
    storage.remove('screen-a');
    assertSpillMissingThrows(() => guard.unpack(packed), 'screen-a');
  });

  it('hand-built tombstone with no stored value throws naming the key', () => {
    const guard = createSizeGuard(storage);
    assertSpillMissingThrows(
      () => guard.unpack({ bundle: { ghost: { __spilled: 'ghost' } }, spilled: ['ghost'] }),
      'ghost'
    );
  });

  it('storage write receives the scope key and the JSON string of the scope value', () => {
    // ASSUMPTION: storage key equals the scope name and the stored string is
    // JSON.stringify(scopeValue); the plan names the tombstone shape but not the
    // storage encoding, so tests pin the most defensible JSON-UTF-8 encoding.
    const seen = [];
    const recording = {
      write(k, v) { seen.push([k, v]); storage.write(k, v); },
      read: (k) => storage.read(k),
      remove: (k) => storage.remove(k),
    };
    const guard = createSizeGuard(recording, 20);
    const value = { blob: 'q'.repeat(100) };
    guard.pack({ 'screen-a': value });
    assert.deepEqual(seen, [['screen-a', JSON.stringify(value)]]);
  });

  it('pack does not mutate its input snapshot', () => {
    // ASSUMPTION: plan silent on aliasing; non-mutating pack is most defensible.
    const guard = createSizeGuard(storage, 20);
    const snapshot = { 'screen-a': { v: 'x'.repeat(100) } };
    const before = JSON.parse(JSON.stringify(snapshot));
    guard.pack(snapshot);
    assert.deepEqual(snapshot, before);
  });
});
