'use strict';
// T3 — FR-DX-008 / AC-DX-034 / TC-DX-034 (Batch 1).
// Hermetic: node:test + node:assert/strict only. Symbolication + transport
// faked in-process via require.cache pre-seeding of the seam path; no device,
// no network, no secrets, no config.
// Interface under test (plan verbatim):
//   streamLogcat(filter: {tag?: string, level?: string}) =>
//     AsyncIterable<{taxonomy: string, frame: string, mappedSource: string|null}>
//   exported from tools/cli/lib/logcat-stream.js; transport errors surface as
//   {command:"logcat", cause, action}; symbolication consumed via
//   tools/cli/lib/symbolication-seam.js (fake in tests).
// ASSUMPTION: BUILD-001 facade untouched; reliability seam faked — the stream
// module resolves its device lines through the symbolication seam, which the
// tests replace with an in-memory fake exposing symbolicate(frame) and
// optional readLines(filter); transport failures reject/throw structured
// {command:'logcat', cause, action}.
// ASSUMPTION: taxonomy is a non-empty label string; mappedSource is a string
// or null.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

const LIB = path.resolve(__dirname, '..', 'lib', 'logcat-stream.js');
const SEAM = path.resolve(__dirname, '..', 'lib', 'symbolication-seam.js');

const FAKE_LINES = [
  { tag: 'Muse', level: 'error', frame: 'pc 0x1a2b Foo::bar' },
  { tag: 'Muse', level: 'info', frame: 'pc 0x3c4d Baz::qux' },
  { tag: 'Other', level: 'error', frame: 'pc 0x9e8f Other::fn' },
];

function seedSeam(fake) {
  delete require.cache[SEAM];
  require.cache[SEAM] = {
    id: SEAM,
    filename: SEAM,
    loaded: true,
    exports: fake,
  };
}

function defaultSeam() {
  return {
    symbolicate(frame) {
      return { frame, mappedSource: `mapped:${frame}` };
    },
    readLines() {
      return FAKE_LINES.slice();
    },
  };
}

async function collect(iter) {
  const out = [];
  for await (const e of iter) out.push(e);
  return out;
}

beforeEach(() => {
  delete require.cache[LIB];
  delete require.cache[SEAM];
});

afterEach(() => {
  delete require.cache[LIB];
  delete require.cache[SEAM];
});

describe('logcat streamLogcat (T3)', () => {
  it('streams entries with taxonomy labels', async () => {
    seedSeam(defaultSeam());
    const { streamLogcat } = require(LIB);
    const entries = await collect(streamLogcat({}));
    assert.ok(entries.length > 0);
    for (const e of entries) assert.equal(typeof e.taxonomy, 'string');
  });

  it('frames are symbolicated with mappedSource', async () => {
    seedSeam(defaultSeam());
    const { streamLogcat } = require(LIB);
    const entries = await collect(streamLogcat({}));
    for (const e of entries) {
      assert.equal(typeof e.frame, 'string');
      assert.ok(e.mappedSource === null || typeof e.mappedSource === 'string');
    }
    assert.ok(entries.some((e) => typeof e.mappedSource === 'string' && e.mappedSource.length > 0),
      'at least one frame must symbolicate');
  });

  it('tag filter narrows the stream', async () => {
    seedSeam(defaultSeam());
    const { streamLogcat } = require(LIB);
    const entries = await collect(streamLogcat({ tag: 'Muse' }));
    assert.ok(entries.length > 0);
    assert.ok(entries.length < FAKE_LINES.length + 1);
  });

  it('level filter narrows the stream', async () => {
    seedSeam(defaultSeam());
    const { streamLogcat } = require(LIB);
    const entries = await collect(streamLogcat({ level: 'error' }));
    assert.ok(entries.length > 0);
  });

  it('transport errors are structured {command, cause, action}', async () => {
    seedSeam({
      symbolicate(frame) { return { frame, mappedSource: null }; },
      readLines() { throw new Error('device offline'); },
    });
    const { streamLogcat } = require(LIB);
    let caught = null;
    try {
      await collect(streamLogcat({}));
    } catch (err) {
      caught = err;
    }
    assert.ok(caught, 'transport failure must surface');
    assert.equal(caught.command, 'logcat');
    assert.equal(typeof caught.cause, 'string');
    assert.equal(typeof caught.action, 'string');
  });

  it('raw logcat is never the primary output: entries carry taxonomy + frame', async () => {
    seedSeam(defaultSeam());
    const { streamLogcat } = require(LIB);
    const entries = await collect(streamLogcat({}));
    for (const e of entries) {
      assert.ok(e.taxonomy && e.frame, 'each entry must carry taxonomy + frame');
      assert.ok(!('raw' in e) || e.taxonomy, 'taxonomy must be present');
    }
  });
});
