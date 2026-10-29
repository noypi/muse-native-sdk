'use strict';
// Batch FR-PACKAGING-004/005 T1 — Real ZIP APK assembler (FR-PACKAGING-004,
// AC-PACKAGING-008 / TC-PACKAGING-008).
// Scope: `buildApk` from `packages/cli/lib/apk.js` only. Every emitted APK
// must be a valid ZIP (`unzip -l` equivalent lists entries) containing at
// minimum AndroidManifest.xml, classes.dex, resources.arsc, plus
// lib/<abi>/libRuntime.so per arch and assets/tree.mui.bin.
// Hermetic: node:test + node:assert/strict + node:zlib + stdlib only.
// Fully mocked fixture bytes, no downloads, no secrets, no live device,
// no existing app config, no dist/ writes. ZIP parsing is implemented
// inline with Buffer reads (no third-party ZIP dependency exists in the
// checkout) and supports stored (method 0) and deflated (method 8) entries
// so the tests pin behavior, not the coder's compression choice.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const zlib = require('node:zlib');

const { buildApk } = require('../lib/apk.js');

const MANIFEST_XML = '<?xml version="1.0" encoding="utf-8"?><manifest package="com.muse.gallery" />';
const DEX_BYTES = Buffer.from('DEX-FIXTURE-035-classes', 'utf8');
const RESOURCES_ARSC = Buffer.from('ARSC-FIXTURE-035-resources', 'utf8');
const TREE_BIN = Buffer.from('TREE-FIXTURE-035-mui-bin-payload', 'utf8');

function fixtureLibs(abis) {
  return abis.map((abi) => ({
    abi,
    name: 'libRuntime.so',
    bytes: Buffer.from(`SO-FIXTURE-035-${abi}`, 'utf8'),
  }));
}

function fixtureInput(overrides) {
  return {
    manifestXml: MANIFEST_XML,
    dexBytes: DEX_BYTES,
    resourcesArsc: RESOURCES_ARSC,
    nativeLibs: fixtureLibs(['arm64-v8a', 'x86_64']),
    treeBin: TREE_BIN,
    ...(overrides || {}),
  };
}

// Minimal ZIP reader: locates EOCD, walks the central directory, returns
// entry descriptors. Throws an assertion failure (not a parse throw) when
// the buffer is not a valid ZIP so failures read as test failures.
function parseZipEntries(buf) {
  assert.ok(Buffer.isBuffer(buf), 'buildApk must return a Buffer');
  assert.ok(buf.length > 22, 'buffer long enough to hold EOCD');
  let eocd = -1;
  for (let i = buf.length - 22; i >= 0; i--) {
    if (buf[i] === 0x50 && buf[i + 1] === 0x4b && buf[i + 2] === 0x05 && buf[i + 3] === 0x06) {
      eocd = i;
      break;
    }
  }
  assert.ok(eocd >= 0, 'EOCD record (PK\\x05\\x06) present — valid ZIP');
  const count = buf.readUInt16LE(eocd + 10);
  const cdOffset = buf.readUInt32LE(eocd + 16);
  assert.ok(cdOffset < buf.length, 'central directory offset inside buffer');
  const entries = [];
  let p = cdOffset;
  for (let n = 0; n < count; n++) {
    assert.equal(buf.readUInt32LE(p), 0x02014b50, `central header signature for entry ${n}`);
    const method = buf.readUInt16LE(p + 10);
    const compSize = buf.readUInt32LE(p + 24);
    const uncompSize = buf.readUInt32LE(p + 28);
    const nLen = buf.readUInt16LE(p + 28);
    const xLen = buf.readUInt16LE(p + 30);
    const cLen = buf.readUInt16LE(p + 32);
    const localOff = buf.readUInt32LE(p + 42);
    const name = buf.toString('utf8', p + 46, p + 46 + nLen);
    entries.push({ name, method, compSize, uncompSize, localOff });
    p += 46 + nLen + xLen + cLen;
  }
  assert.ok(entries.length > 0, 'ZIP contains at least one entry');
  return entries;
}

function extractEntry(buf, entry) {
  const p = entry.localOff;
  assert.equal(buf.readUInt32LE(p), 0x04034b50, `local header signature for ${entry.name}`);
  const nLen = buf.readUInt16LE(p + 26);
  const xLen = buf.readUInt16LE(p + 28);
  const start = p + 30 + nLen + xLen;
  const raw = buf.subarray(start, start + entry.compSize);
  assert.equal(raw.length, entry.compSize, `full data present for ${entry.name}`);
  if (entry.method === 0) return Buffer.from(raw);
  if (entry.method === 8) return zlib.inflateRawSync(raw);
  assert.fail(`unsupported compression method ${entry.method} for ${entry.name}`);
  return null;
}

function entryNames(buf) {
  return parseZipEntries(buf).map((e) => e.name);
}

describe('T1 AC-PACKAGING-008 — emitted APK is a valid ZIP', () => {
  it('buffer opens as a ZIP: local file header first, EOCD + central directory present', () => {
    const out = buildApk(fixtureInput());
    assert.equal(bufSig(out), 'PK\x03\x04');
    const names = entryNames(out);
    assert.ok(names.length >= 4, 'at least the required entries present');
  });

  it('fat apk lists every required entry (AC-PACKAGING-008 minimum set)', () => {
    const names = entryNames(buildApk(fixtureInput()));
    for (const required of [
      'AndroidManifest.xml',
      'classes.dex',
      'resources.arsc',
      'assets/tree.mui.bin',
      'lib/arm64-v8a/libRuntime.so',
      'lib/x86_64/libRuntime.so',
    ]) {
      assert.ok(names.includes(required), `entry present: ${required}`);
    }
  });
});

function bufSig(buf) {
  return String.fromCharCode(buf[0], buf[1]) + String.fromCharCode(buf[2], buf[3]);
}

describe('T1 — entry payloads round-trip byte-identical', () => {
  it('manifest, dex, resources, and tree asset extract exactly', () => {
    const out = buildApk(fixtureInput());
    const byName = new Map(parseZipEntries(out).map((e) => [e.name, e]));
    assert.deepEqual(extractEntry(out, byName.get('AndroidManifest.xml')), Buffer.from(MANIFEST_XML, 'utf8'));
    assert.deepEqual(extractEntry(out, byName.get('classes.dex')), DEX_BYTES);
    assert.deepEqual(extractEntry(out, byName.get('resources.arsc')), RESOURCES_ARSC);
    assert.deepEqual(extractEntry(out, byName.get('assets/tree.mui.bin')), TREE_BIN);
  });

  it('each native lib extracts to its own per-arch bytes', () => {
    const out = buildApk(fixtureInput());
    const byName = new Map(parseZipEntries(out).map((e) => [e.name, e]));
    assert.deepEqual(
      extractEntry(out, byName.get('lib/arm64-v8a/libRuntime.so')),
      Buffer.from('SO-FIXTURE-035-arm64-v8a', 'utf8'),
    );
    assert.deepEqual(
      extractEntry(out, byName.get('lib/x86_64/libRuntime.so')),
      Buffer.from('SO-FIXTURE-035-x86_64', 'utf8'),
    );
  });
});

describe('T1 — split variant contains only its own arch lib', () => {
  it('single-arch build has lib/arm64-v8a and no lib/x86_64 entry', () => {
    const names = entryNames(buildApk(fixtureInput({ nativeLibs: fixtureLibs(['arm64-v8a']) })));
    assert.ok(names.includes('lib/arm64-v8a/libRuntime.so'));
    assert.ok(!names.some((n) => n.startsWith('lib/x86_64/')), 'no x86_64 lib in arm64-only split');
    assert.ok(names.includes('AndroidManifest.xml'));
    assert.ok(names.includes('classes.dex'));
    assert.ok(names.includes('resources.arsc'));
  });

  it('x86_64-only build has lib/x86_64 and no lib/arm64-v8a entry', () => {
    const names = entryNames(buildApk(fixtureInput({ nativeLibs: fixtureLibs(['x86_64']) })));
    assert.ok(names.includes('lib/x86_64/libRuntime.so'));
    assert.ok(!names.some((n) => n.startsWith('lib/arm64-v8a/')), 'no arm64 lib in x86_64-only split');
  });
});

describe('T1 — unsigned: no signature block entries', () => {
  it('emits no META-INF signature entries (unsigned per interface)', () => {
    const names = entryNames(buildApk(fixtureInput()));
    assert.ok(!names.some((n) => n.startsWith('META-INF/')), 'unsigned — no META-INF/ entries');
  });
});

describe('T1 — fail-closed on missing manifest/dex/resources (Error apk: <reason>)', () => {
  it('missing manifestXml throws Error starting with apk:', () => {
    assert.throws(() => buildApk(fixtureInput({ manifestXml: undefined })), (err) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /^apk: /);
      return true;
    });
  });

  it('empty manifestXml throws Error starting with apk:', () => {
    assert.throws(() => buildApk(fixtureInput({ manifestXml: '' })), (err) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /^apk: /);
      return true;
    });
  });

  it('empty dexBytes throws Error starting with apk:', () => {
    assert.throws(() => buildApk(fixtureInput({ dexBytes: Buffer.alloc(0) })), (err) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /^apk: /);
      return true;
    });
  });

  it('missing dexBytes throws Error starting with apk:', () => {
    assert.throws(() => buildApk(fixtureInput({ dexBytes: undefined })), (err) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /^apk: /);
      return true;
    });
  });

  it('missing resourcesArsc throws Error starting with apk:', () => {
    assert.throws(() => buildApk(fixtureInput({ resourcesArsc: undefined })), (err) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /^apk: /);
      return true;
    });
  });

  it('empty resourcesArsc throws Error starting with apk:', () => {
    assert.throws(() => buildApk(fixtureInput({ resourcesArsc: Buffer.alloc(0) })), (err) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /^apk: /);
      return true;
    });
  });
});

describe('T1 — synchronous pure return, never a promise, never writes to disk', () => {
  it('returns a Buffer synchronously (not a thenable); takes no output path', () => {
    const out = buildApk(fixtureInput());
    assert.ok(Buffer.isBuffer(out), 'returns Buffer bytes for the caller to write');
    assert.equal(typeof out.then, 'undefined', 'synchronous — no promise');
    assert.equal(buildApk.length >= 1, true, 'takes a single input object (no out-path parameter)');
  });
});

// ASSUMPTION A-T1a (plan Detail "fail-closed on empty inputs" does not
// enumerate treeBin): an empty tree asset fails closed like the other
// required blobs instead of emitting an APK with a zero-byte tree.
describe('T1 ASSUMPTION A-T1a — empty treeBin fails closed', () => {
  it('empty treeBin throws Error starting with apk:', () => {
    assert.throws(() => buildApk(fixtureInput({ treeBin: Buffer.alloc(0) })), (err) => {
      assert.ok(err instanceof Error);
      assert.match(err.message, /^apk: /);
      return true;
    });
  });
});

// ASSUMPTION A-T1b (plan pins lib/<abi>/ entries "per arch" but no
// behavior for an arch-less build): an empty nativeLibs list still emits
// a valid ZIP with the required non-lib entries and no lib/ entries.
describe('T1 ASSUMPTION A-T1b — empty nativeLibs emits lib-less but valid ZIP', () => {
  it('no archs still yields a valid ZIP with manifest/dex/resources/tree and no lib/ entries', () => {
    const names = entryNames(buildApk(fixtureInput({ nativeLibs: [] })));
    assert.ok(names.includes('AndroidManifest.xml'));
    assert.ok(names.includes('classes.dex'));
    assert.ok(names.includes('resources.arsc'));
    assert.ok(names.includes('assets/tree.mui.bin'));
    assert.ok(!names.some((n) => n.startsWith('lib/')), 'no lib/ entries without archs');
  });
});
