'use strict';
// Batch 2 — T4 createMuseMcp() connector (FR-MUSE-013, AC-MUSE-041, TC-MUSE-040).
// Owned by T4 per GR-08 T1.8. Public entry only: require('../muse-mcp.js')
// plus the ../index.js re-export. Hermetic: node:test + node:assert/strict
// + node:fs/path (own-file source scan) only. No network, no secrets,
// no config, no live services, no server/socket.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const { createMuseMcp } = require('../muse-mcp.js');

function ability(over = {}) {
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

// Unwraps either a sync throw or a rejected promise into its `code`.
async function callCode(promiseOrValue, syncFn) {
  try {
    const out = syncFn === undefined ? promiseOrValue : syncFn();
    if (out && typeof out.then === 'function') await out;
  } catch (err) {
    return err && err.code;
  }
  return undefined;
}

describe('T4 — createMuseMcp is exposed (AC-MUSE-041)', () => {
  it('muse-mcp.js exposes createMuseMcp as a function', () => {
    assert.equal(typeof createMuseMcp, 'function');
  });

  it('package root index.js re-exports createMuseMcp', () => {
    // ASSUMPTION: the root entry keeps all existing exports and adds
    // createMuseMcp alongside them (additive re-export, no reload of
    // existing behavior).
    const nativeIndex = require('../index.js');
    assert.equal(typeof nativeIndex.createMuseMcp, 'function');
  });
});

describe('T4 — one call registers + binds turn + serves call/tools (AC-MUSE-041)', () => {
  it('call(name, args) invokes the ability with the control getTurn() turn id', async () => {
    const seen = [];
    const control = { getTurn: () => ({ id: 'turn-3-control-turn' }) };
    const handle = createMuseMcp({
      abilities: [
        ability({
          invoke: (args, ctx) => {
            seen.push(ctx);
            return 'opened';
          },
        }),
      ],
      control,
      capabilities: ['gallery.read'],
    });
    const out = await handle.call('gallery.open', { itemId: 'x' });
    assert.equal(out, 'opened');
    assert.deepEqual(seen, [{ turnId: 'turn-3-control-turn' }]);
  });

  it('accepts a plain { turnId } control shape', async () => {
    let seen;
    const handle = createMuseMcp({
      abilities: [
        ability({
          invoke: (args, ctx) => {
            seen = ctx;
            return true;
          },
        }),
      ],
      control: { turnId: 'turn-5-plain-turn' },
      capabilities: ['gallery.read'],
    });
    await handle.call('gallery.open', { itemId: 'x' });
    assert.deepEqual(seen, { turnId: 'turn-5-plain-turn' });
  });

  it('reads the control current turn per call (never a stale snapshot)', async () => {
    const seen = [];
    let current = 'turn-1-first';
    const control = { getTurn: () => ({ id: current }) };
    const handle = createMuseMcp({
      abilities: [
        ability({
          invoke: (args, ctx) => {
            seen.push(ctx.turnId);
            return true;
          },
        }),
      ],
      control,
      capabilities: ['gallery.read'],
    });
    await handle.call('gallery.open', { itemId: 'x' });
    current = 'turn-2-second';
    await handle.call('gallery.open', { itemId: 'x' });
    assert.deepEqual(seen, ['turn-1-first', 'turn-2-second']);
  });

  it('tools lists the registered abilities', async () => {
    const handle = createMuseMcp({
      abilities: [
        ability(),
        ability({
          name: 'notes.save',
          capabilities: ['notes.write'],
          args: { title: 'string' },
        }),
      ],
      control: { turnId: 'turn-1-list' },
      capabilities: ['gallery.read', 'notes.write'],
    });
    const tools = Array.isArray(handle.tools)
      ? handle.tools
      : typeof handle.tools === 'function'
        ? handle.tools()
        : assert.fail('handle.tools must be an array or a function returning one');
    const names = tools.map((t) => t.name).sort();
    assert.deepEqual(names, ['gallery.open', 'notes.save']);
  });

  it('no server/socket: handle exposes exactly registry, tools, call and dispose', () => {
    const handle = createMuseMcp({
      abilities: [ability()],
      control: { turnId: 'turn-1-keys' },
      capabilities: ['gallery.read'],
    });
    assert.deepEqual(Object.keys(handle).sort(), [
      'call',
      'dispose',
      'registry',
      'tools',
    ]);
  });
});

describe('T4 — dispose() unbinds the turn so later call() fails stale-id (AC-MUSE-041)', () => {
  it('call() after dispose() fails closed with { code: stale-id }', async () => {
    const handle = createMuseMcp({
      abilities: [ability()],
      control: { turnId: 'turn-4-live' },
      capabilities: ['gallery.read'],
    });
    handle.dispose();
    const code = await callCode(undefined, () =>
      handle.call('gallery.open', { itemId: 'x' })
    );
    assert.equal(code, 'stale-id');
  });

  it('dispose() never mutates the control object', () => {
    const control = { turnId: 'turn-6-untouched' };
    const snapshot = { turnId: control.turnId };
    const handle = createMuseMcp({
      abilities: [ability()],
      control,
      capabilities: ['gallery.read'],
    });
    handle.dispose();
    assert.deepEqual({ turnId: control.turnId }, snapshot);
    assert.equal(typeof control.getTurn, 'undefined');
  });
});

describe('T5 — smart-component handle-only boundary (AC-MUSE-042, TC-MUSE-041)', () => {
  it('handle carries exactly registry, tools, call and dispose (no transport/provider fields)', () => {
    const handle = createMuseMcp({
      abilities: [ability()],
      control: { turnId: 'turn-7-handle-only' },
      capabilities: ['gallery.read'],
    });
    assert.deepEqual(Object.keys(handle).sort(), [
      'call',
      'dispose',
      'registry',
      'tools',
    ]);
    for (const key of ['transport', 'providerUrl', 'apiKey', 'client']) {
      assert.ok(!(key in handle), `handle must not carry ${key}`);
    }
  });

  for (const key of ['transport', 'providerUrl', 'apiKey', 'client']) {
    it(`createMuseMcp rejects a top-level ${key} option instead of storing it`, () => {
      // Plan Interface: forbidden surface throws `{ code: 'capability-denied' }`
      // or `TypeError` at construction; the value must never be stored.
      assert.throws(
        () =>
          createMuseMcp({
            abilities: [ability()],
            control: { turnId: 'turn-7-forbidden' },
            capabilities: ['gallery.read'],
            [key]: 'forbidden-value',
          }),
        (err) =>
          err instanceof TypeError || (err && err.code === 'capability-denied')
      );
    });
  }

  it('muse-mcp.js source mentions no transport and no client wiring', () => {
    const src = fs.readFileSync(path.join(__dirname, '..', 'muse-mcp.js'), 'utf8');
    assert.ok(!/transport/.test(src), 'muse-mcp.js must not mention transport');
    // `client` as a bare identifier would over-match (e.g. comments); match
    // only option-shaped uses: `.client`, `client:` or `client =`.
    assert.ok(
      !/[.]client\b|\bclient\s*[:=]/.test(src),
      'muse-mcp.js must not take a client option'
    );
  });
});
describe('T4 — one-way dep: no transport/provider fields (TC-MUSE-040)', () => {
  it('muse-mcp.js source mentions no providerUrl and no apiKey', () => {
    const src = fs.readFileSync(path.join(__dirname, '..', 'muse-mcp.js'), 'utf8');
    assert.ok(!/providerUrl/.test(src), 'muse-mcp.js must not mention providerUrl');
    assert.ok(!/apiKey/.test(src), 'muse-mcp.js must not mention apiKey');
  });
});
