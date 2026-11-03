'use strict';
// Batch 1 — T3 createMuseMcp() connector (FR-MUSE-013, AC-MUSE-041, TC-MUSE-040).
// Owned by T3 per GR-08 T1.8; T4 contract maps to this file.
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

describe('T3 — createMuseMcp is exposed (AC-MUSE-041)', () => {
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

describe('T3 — one call registers + binds turn + serves call/tools (AC-MUSE-041)', () => {
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

describe('T3 — dispose() unbinds the turn so later call() fails stale-id (AC-MUSE-041)', () => {
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

describe('T4 — smart-component handle-only boundary (AC-MUSE-042, TC-MUSE-041)', () => {
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
describe('T3 — one-way dep: no transport/provider fields (TC-MUSE-040)', () => {
  it('muse-mcp.js source mentions no providerUrl and no apiKey', () => {
    const src = fs.readFileSync(path.join(__dirname, '..', 'muse-mcp.js'), 'utf8');
    assert.ok(!/providerUrl/.test(src), 'muse-mcp.js must not mention providerUrl');
    assert.ok(!/apiKey/.test(src), 'muse-mcp.js must not mention apiKey');
  });
});

describe('SEC — hostile getTurn() shapes fail closed with stale-id', () => {
  it('getTurn() that throws fails closed with { code: stale-id } (never raw propagation)', async () => {
    const hostile = new Error('hostile-control-boom');
    const handle = createMuseMcp({
      abilities: [ability()],
      control: {
        getTurn: () => {
          throw hostile;
        },
      },
      capabilities: ['gallery.read'],
    });
    const code = await callCode(undefined, () =>
      handle.call('gallery.open', { itemId: 'x' })
    );
    assert.equal(code, 'stale-id');
  });

  it('getTurn() returning null fails closed with { code: stale-id }', async () => {
    const handle = createMuseMcp({
      abilities: [ability()],
      control: { getTurn: () => null },
      capabilities: ['gallery.read'],
    });
    const code = await callCode(undefined, () =>
      handle.call('gallery.open', { itemId: 'x' })
    );
    assert.equal(code, 'stale-id');
  });

  it('getTurn() returning undefined fails closed with { code: stale-id }', async () => {
    const handle = createMuseMcp({
      abilities: [ability()],
      control: { getTurn: () => undefined },
      capabilities: ['gallery.read'],
    });
    const code = await callCode(undefined, () =>
      handle.call('gallery.open', { itemId: 'x' })
    );
    assert.equal(code, 'stale-id');
  });

  it('getTurn() returning a malformed turn (no string id) fails closed with { code: stale-id }', async () => {
    for (const malformed of [{}, { id: 42 }, { id: null }, { turnId: 'turn-1-wrong-key' }]) {
      const handle = createMuseMcp({
        abilities: [ability()],
        control: { getTurn: () => malformed },
        capabilities: ['gallery.read'],
      });
      const code = await callCode(undefined, () =>
        handle.call('gallery.open', { itemId: 'x' })
      );
      assert.equal(code, 'stale-id');
    }
  });
});

describe('T8 — tools snapshot carries name+args+capabilities, no remote wiring (AC-MUSE-041)', () => {
  it('every tools() entry exposes name, args and capabilities only', () => {
    const handle = createMuseMcp({
      abilities: [
        ability(),
        ability({
          name: 'notes.save',
          capabilities: ['notes.write'],
          args: { title: 'string' },
        }),
      ],
      control: { turnId: 'turn-8-tools-shape' },
      capabilities: ['gallery.read', 'notes.write'],
    });
    const tools = typeof handle.tools === 'function' ? handle.tools() : handle.tools;
    assert.equal(tools.length, 2);
    for (const t of tools) {
      assert.equal(typeof t.name, 'string');
      assert.ok(t.args && typeof t.args === 'object', 'entry must carry args map');
      assert.ok(Array.isArray(t.capabilities), 'entry must carry capabilities');
      for (const key of ['transport', 'providerUrl', 'apiKey', 'client']) {
        assert.ok(!(key in Object(t)), `tools entry must not carry ${key}`);
      }
    }
    const open = tools.find((t) => t.name === 'gallery.open');
    assert.deepEqual(open.args, { itemId: 'string' });
    assert.deepEqual(open.capabilities, ['gallery.read']);
  });

  it('empty abilities registers nothing: tools() is [] and call() fails unknown-ability', async () => {
    const handle = createMuseMcp({
      abilities: [],
      control: { turnId: 'turn-8-empty' },
      capabilities: ['gallery.read'],
    });
    const tools = typeof handle.tools === 'function' ? handle.tools() : handle.tools;
    assert.deepEqual(tools, []);
    const code = await callCode(undefined, () =>
      handle.call('gallery.open', { itemId: 'x' })
    );
    assert.equal(code, 'unknown-ability');
  });
});

describe('T8 — capability gating + turn binding hold through handle.call (AC-MUSE-040/041)', () => {
  it('call() for an ungranted capability fails closed with { code: capability-denied }', async () => {
    const handle = createMuseMcp({
      abilities: [
        ability({ name: 'notes.save', capabilities: ['notes.write'] }),
      ],
      control: { turnId: 'turn-8-denied' },
      capabilities: ['gallery.read'],
    });
    const code = await callCode(undefined, () =>
      handle.call('notes.save', { title: 't' })
    );
    assert.equal(code, 'capability-denied');
  });

  it('call() with a turn-less control fails closed with { code: stale-id }', async () => {
    const handle = createMuseMcp({
      abilities: [ability()],
      control: {},
      capabilities: ['gallery.read'],
    });
    const code = await callCode(undefined, () =>
      handle.call('gallery.open', { itemId: 'x' })
    );
    assert.equal(code, 'stale-id');
  });
});

describe('T8 — no server process: source carries no server/socket wiring (AC-MUSE-041)', () => {
  it('muse-mcp.js wires no server, socket, http, or port listener', () => {
    const src = fs.readFileSync(path.join(__dirname, '..', 'muse-mcp.js'), 'utf8');
    for (const token of [
      /require\(\s*['"]http['"]\s*\)/,
      /require\(\s*['"]net['"]\s*\)/,
      /createServer/,
      /WebSocket/,
      /\.listen\s*\(/,
      /\bnet\.connect\s*\(/,
      /\bnew\s+Socket\s*\(/,
    ]) {
      assert.ok(!token.test(src), `muse-mcp.js must not wire server surface ${token}`);
    }
  });
});

describe('T9 — forbidden keys denied by construction regardless of value (AC-MUSE-042)', () => {
  for (const key of ['transport', 'providerUrl', 'apiKey', 'client']) {
    for (const value of [undefined, null, false, 0, '']) {
      it(`own-property ${key}=${String(value)} still throws TypeError`, () => {
        assert.throws(
          () =>
            createMuseMcp({
              abilities: [ability()],
              control: { turnId: 'turn-9-any-value' },
              capabilities: ['gallery.read'],
              [key]: value,
            }),
          (err) => err instanceof TypeError
        );
      });
    }
  }
});

describe('T10 — exclusivity holds through the abilities path (AC-MUSE-043, FR-MUSE-007)', () => {
  it('docs/meta-first.md states the abilities-path exclusivity rule (TC-MUSE-042)', () => {
    const doc = fs.readFileSync(
      path.join(__dirname, '..', '..', '..', 'docs', 'meta-first.md'),
      'utf8'
    );
    assert.ok(
      /Through the abilities path, the only remote path is the adapter's server-side capability re-check/i.test(
        doc
      ),
      'doc must state the abilities-path remote rule'
    );
    assert.ok(/AC-MUSE-019/.test(doc), 'doc must cite AC-MUSE-019');
  });

  it('no smart-component source describes transport/providerUrl/apiKey (AC-MUSE-042)', () => {
    const roots = [
      path.join(__dirname, '..', 'composites'),
      path.join(__dirname, '..', 'selection.js'),
      path.join(__dirname, '..', 'textinput.js'),
      path.join(__dirname, '..', 'theme.js'),
      path.join(__dirname, '..', 'index.js'),
    ];
    const files = [];
    for (const r of roots) {
      if (!fs.existsSync(r)) continue;
      const st = fs.statSync(r);
      if (st.isDirectory()) {
        for (const f of fs.readdirSync(r)) files.push(path.join(r, f));
      } else {
        files.push(r);
      }
    }
    assert.ok(files.length > 0, 'expected component sources to scan');
    for (const f of files) {
      const src = fs.readFileSync(f, 'utf8');
      // `transport` is matched only in option/wiring shape: bare prose
      // (e.g. "clipboard transport failures") is not provider wiring.
      for (const token of [/providerUrl/, /apiKey/, /[.{"']transport\b|\btransport\s*[:=]/]) {
        assert.ok(
          !token.test(src),
          `${path.basename(f)} must not describe ${token}`
        );
      }
    }
  });
});
describe('SEC — proto-inherited forbidden keys are not honored (own-property guard only)', () => {
  for (const key of ['transport', 'providerUrl', 'apiKey', 'client']) {
    it(`inherited ${key} on the prototype is ignored: construction succeeds and call() works`, async () => {
      const opts = Object.create({ [key]: 'forbidden-value' });
      opts.abilities = [ability()];
      opts.control = { turnId: 'turn-9-proto-guard' };
      opts.capabilities = ['gallery.read'];
      const handle = createMuseMcp(opts);
      const out = await handle.call('gallery.open', { itemId: 'x' });
      assert.deepEqual(out, { args: { itemId: 'x' }, turnId: 'turn-9-proto-guard' });
    });

    it(`own-property ${key} still throws TypeError`, () => {
      assert.throws(
        () =>
          createMuseMcp({
            abilities: [ability()],
            control: { turnId: 'turn-9-own-guard' },
            capabilities: ['gallery.read'],
            [key]: 'forbidden-value',
          }),
        (err) => err instanceof TypeError
      );
    });
  }
});
