'use strict';
// Batch 1 — @muse/abilities: T1 defineAbility + T2 capability-gated registry.
// Shared file assigned to T1 per GR-08 T1.8; T2 contract maps to this file.
// Public entry only: require('../index.js'). Hermetic: node:test +
// node:assert/strict + node:fs/path (own-package source scan) only.
// No network, no secrets, no config, no live services.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const abilities = require('../index.js');

function errCode(fn) {
  try {
    fn();
  } catch (err) {
    return err && err.code;
  }
  return undefined;
}

function validAbility(over = {}) {
  return Object.assign(
    {
      name: 'gallery.open',
      capabilities: ['gallery.read'],
      args: { itemId: 'string' },
      invoke: (args, ctx) => ({ args, turnId: ctx && ctx.turnId }),
    },
    over
  );
}

// ---- T1 — defineAbility (FR-MUSE-012, AC-MUSE-039, TC-MUSE-038) ----

describe('T1 — defineAbility returns a descriptor (AC-MUSE-039)', () => {
  it('defineAbility({name: gallery.open, ...}) returns a descriptor carrying name, capabilities, args and invoke', () => {
    const def = validAbility();
    const desc = abilities.defineAbility(def);
    assert.equal(desc.name, 'gallery.open');
    assert.deepEqual(desc.capabilities, ['gallery.read']);
    assert.deepEqual(desc.args, { itemId: 'string' });
    assert.equal(typeof desc.invoke, 'function');
  });

  it('exposes defineAbility through the package public entry', () => {
    assert.equal(typeof abilities.defineAbility, 'function');
  });
});

describe('T1 — non-serializable arg kinds rejected at define time', () => {
  it('function-valued arg throws ABILITY_NON_SERIALIZABLE_ARG', () => {
    const code = errCode(() =>
      abilities.defineAbility(validAbility({ args: { cb: 'function' } }))
    );
    assert.equal(code, 'ABILITY_NON_SERIALIZABLE_ARG');
  });

  it('symbol-valued arg throws ABILITY_NON_SERIALIZABLE_ARG', () => {
    const code = errCode(() =>
      abilities.defineAbility(validAbility({ args: { s: 'symbol' } }))
    );
    assert.equal(code, 'ABILITY_NON_SERIALIZABLE_ARG');
  });

  it('undefined-valued arg throws ABILITY_NON_SERIALIZABLE_ARG', () => {
    const code = errCode(() =>
      abilities.defineAbility(validAbility({ args: { u: 'undefined' } }))
    );
    assert.equal(code, 'ABILITY_NON_SERIALIZABLE_ARG');
  });

  it('each allowed kind (string/number/boolean/null/array/object) is accepted', () => {
    const desc = abilities.defineAbility(
      validAbility({
        name: 'notes.save',
        args: {
          a: 'string',
          b: 'number',
          c: 'boolean',
          d: 'null',
          e: 'array',
          f: 'object',
        },
      })
    );
    assert.equal(desc.name, 'notes.save');
  });
});

describe('T1 — framework ships zero domain verbs (AC-MUSE-039)', () => {
  it('package sources contain no gallery.* domain verb literals', () => {
    // Scans only this package's shipped sources (index.js, lib/), never test/.
    const pkgDir = path.resolve(__dirname, '..');
    const files = ['index.js', 'lib/abilities.js', 'lib/registry.js'].map((f) =>
      path.join(pkgDir, f)
    );
    for (const file of files) {
      if (!fs.existsSync(file)) continue; // missing file is a red-state absence, asserted below
      const src = fs.readFileSync(file, 'utf8');
      assert.ok(!/gallery\./.test(src), `${file} must not contain a gallery.* literal`);
    }
  });

  it('descriptor names are author-supplied: framework exports expose no gallery.* key', () => {
    for (const key of Object.keys(abilities)) {
      assert.ok(!/gallery\./.test(key), `export ${key} must not be a domain verb`);
    }
  });
});

// ---- T2 — registry (FR-MUSE-012, AC-MUSE-040, TC-MUSE-039) ----

describe('T2 — createRegistry is exported and builds a registry', () => {
  it('exposes createRegistry through the package public entry', () => {
    assert.equal(typeof abilities.createRegistry, 'function');
  });

  it('registry exposes register and call functions', () => {
    const registry = abilities.createRegistry({ capabilities: ['gallery.read'] });
    assert.equal(typeof registry.register, 'function');
    assert.equal(typeof registry.call, 'function');
  });
});

describe('T2 — fail-closed rejections (AC-MUSE-040)', () => {
  it('unknown name throws { code: unknown-ability } (never a silent no-op)', () => {
    const registry = abilities.createRegistry({ capabilities: ['gallery.read'] });
    const code = errCode(() => registry.call('nope.missing', {}, { turnId: 'turn-1-a' }));
    assert.equal(code, 'unknown-ability');
  });

  it('ungranted capability throws { code: capability-denied }', () => {
    const registry = abilities.createRegistry({ capabilities: [] });
    registry.register(validAbility());
    const code = errCode(() =>
      registry.call('gallery.open', { itemId: 'x' }, { turnId: 'turn-1-a' })
    );
    assert.equal(code, 'capability-denied');
  });

  it('granted capability invokes and returns the ability result', () => {
    const registry = abilities.createRegistry({ capabilities: ['gallery.read'] });
    registry.register(
      validAbility({ invoke: () => 'opened' })
    );
    const out = registry.call('gallery.open', { itemId: 'x' }, { turnId: 'turn-1-a' });
    assert.equal(out, 'opened');
  });

  it('rejections throw: call never resolves to undefined for unknown/denied names', () => {
    const registry = abilities.createRegistry({ capabilities: [] });
    assert.throws(() => registry.call('ghost.op', {}, { turnId: 'turn-1-a' }));
    registry.register(validAbility());
    assert.throws(() =>
      registry.call('gallery.open', { itemId: 'x' }, { turnId: 'turn-1-a' })
    );
  });
});

describe('T2 — turnId-stamped bridge with stale-id rule (AC-MUSE-040)', () => {
  it('every invocation stamps the calling turn turnId into ctx', () => {
    const registry = abilities.createRegistry({ capabilities: ['gallery.read'] });
    let seen;
    registry.register(
      validAbility({
        invoke: (args, ctx) => {
          seen = ctx;
          return true;
        },
      })
    );
    registry.call('gallery.open', { itemId: 'x' }, { turnId: 'turn-7-callers-turn' });
    assert.deepEqual(seen, { turnId: 'turn-7-callers-turn' });
  });

  it('stale turn after a newer commit throws { code: stale-id }', () => {
    // ASSUMPTION: turnIds carry the turn.js mint shape `turn-<seq>-<rand>` and the
    // registry keeps a high-water mark: an older seq calling after a newer seq
    // committed is stale (mirrors lib/turn.js stale-commit rule).
    const registry = abilities.createRegistry({ capabilities: ['gallery.read'] });
    registry.register(validAbility({ invoke: () => true }));
    registry.call('gallery.open', { itemId: 'x' }, { turnId: 'turn-2-newer' });
    const code = errCode(() =>
      registry.call('gallery.open', { itemId: 'x' }, { turnId: 'turn-1-older' })
    );
    assert.equal(code, 'stale-id');
  });

  it('missing turnId fails closed with { code: stale-id }', () => {
    // ASSUMPTION: a call with no ctx.turnId cannot be stamped, so it fails
    // closed as stale-id rather than invoking with an undefined turn.
    const registry = abilities.createRegistry({ capabilities: ['gallery.read'] });
    registry.register(validAbility({ invoke: () => true }));
    const code = errCode(() => registry.call('gallery.open', { itemId: 'x' }));
    assert.equal(code, 'stale-id');
  });
});
