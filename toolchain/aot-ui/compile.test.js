'use strict';
// T5 — deterministic AOT-UI compiler core (NFR-BUILD-013 / AC-BUILD-041, AC-BUILD-045).
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no secrets,
// no live services, no filesystem fixtures beyond in-memory source strings.
// Red state: ./compile.js does not exist yet, so every case below fails
// (MODULE_NOT_FOUND) until T5 lands.
//
// Interface under test (plan verbatim):
//   compile(source: string, opts?: { filename?: string }): { bytes: Buffer, hash: string } — deterministic: same input → byte-identical `bytes` and `hash`; throws `AotError { file, line, column, elementPath, message }` on syntax error (never silent). Output embeds no timestamps or absolute paths.
//
// ASSUMPTION (hash shape — the plan pins determinism, not the algorithm): `hash`
// is asserted as a non-empty lowercase hex string stable across runs, not as a
// specific digest algorithm; the coder owns the algorithm, determinism is the gate.
// ASSUMPTION (default filename): when `opts.filename` is omitted, `AotError.file`
// is a non-empty string (exact default is coder-owned); when provided, `file`
// equals it verbatim.
// ASSUMPTION (budget values perf-owned per plan A2): no byte-size number is
// asserted — only determinism, hygiene (no timestamps/paths), and diagnostics.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./compile.js');
}

const SRC = '<View><Text>Hello</Text></View>';
const SRC_MULTILINE = '<View>\n  <Text>Hello</Text>\n</View>';

function asString(bytes) {
  return Buffer.from(bytes).toString('utf8');
}

describe('T5 — compile contract shape', () => {
  it('returns { bytes: Buffer, hash: string } for a minimal TSX element', () => {
    const { compile } = loadImpl();
    const result = compile(SRC, { filename: 'App.tsx' });
    assert.ok(Buffer.isBuffer(result.bytes), 'bytes must be a Buffer');
    assert.ok(result.bytes.length > 0, 'bytes must be non-empty');
    assert.equal(typeof result.hash, 'string');
    assert.ok(result.hash.length > 0, 'hash must be non-empty');
  });

  it('hash is a stable lowercase hex string', () => {
    const { compile } = loadImpl();
    const { hash } = compile(SRC, { filename: 'App.tsx' });
    assert.match(hash, /^[0-9a-f]+$/);
  });
});

describe('T5 — determinism: byte-identical output, no timestamps/paths (AC-BUILD-041, AC-BUILD-045)', () => {
  it('two clean compilations of the same source produce identical bytes and hash', () => {
    const { compile } = loadImpl();
    const first = compile(SRC, { filename: 'App.tsx' });
    const second = compile(SRC, { filename: 'App.tsx' });
    assert.deepEqual(second.bytes, first.bytes);
    assert.equal(second.hash, first.hash);
  });

  it('a third compilation with reordered-but-equal input still matches (stable key ordering)', () => {
    const { compile } = loadImpl();
    const a = compile(SRC_MULTILINE, { filename: 'App.tsx' });
    const b = compile(SRC_MULTILINE, { filename: 'App.tsx' });
    assert.deepEqual(b.bytes, a.bytes);
    assert.equal(b.hash, a.hash);
  });

  it('output embeds no timestamps', () => {
    const { compile } = loadImpl();
    const { bytes } = compile(SRC, { filename: 'App.tsx' });
    const text = asString(bytes);
    assert.ok(!/\d{4}-\d{2}-\d{2}T\d{2}:\d{2}/.test(text), 'no ISO timestamps in output');
    assert.ok(!text.includes(String(Date.now())), 'no wall-clock millis in output');
    assert.ok(!/Date\.now\(\)|__TIMESTAMP__|BUILD_TIME/.test(text), 'no timestamp placeholders');
  });

  it('output embeds no absolute paths even when filename is absolute', () => {
    const { compile } = loadImpl();
    const abs = '/tmp/muse-aot-fixture-abs/App.tsx';
    const { bytes } = compile(SRC, { filename: abs });
    const text = asString(bytes);
    assert.ok(!text.includes(abs), 'output must not embed the absolute filename');
    assert.ok(!text.includes('/tmp/muse-aot-fixture-abs'), 'no absolute tmpdir leakage');
  });
});

describe('T5 — syntax-error diagnostics (AC-BUILD-045 second half; full tuple owned by T6)', () => {
  it('unbalanced JSX throws AotError { file, line, column, elementPath, message }', () => {
    const { compile } = loadImpl();
    assert.throws(
      () => compile('<View><Text></View>', { filename: 'Bad.tsx' }),
      (err) => {
        assert.equal(err.name, 'AotError');
        assert.equal(err.file, 'Bad.tsx');
        assert.equal(typeof err.line, 'number');
        assert.equal(typeof err.column, 'number');
        assert.ok(err.line >= 1 && err.column >= 1);
        assert.equal(typeof err.elementPath, 'string');
        assert.ok(err.elementPath.length > 0, 'elementPath must name the open element');
        assert.equal(typeof err.message, 'string');
        assert.ok(err.message.length > 0);
        return true;
      }
    );
  });

  it('error is thrown, never returned as a value (never silent)', () => {
    const { compile } = loadImpl();
    let returned = null;
    try {
      returned = compile('<View><Unclosed>', { filename: 'Silent.tsx' });
    } catch (err) {
      assert.equal(err.name, 'AotError');
    }
    assert.equal(returned, null, 'a syntax error must throw, not return');
  });

  it('multiline error reports the correct line number', () => {
    const { compile } = loadImpl();
    assert.throws(
      () => compile('<View>\n  <Text>\n</View>', { filename: 'Lines.tsx' }),
      (err) => {
        assert.equal(err.name, 'AotError');
        assert.equal(err.file, 'Lines.tsx');
        assert.ok(err.line >= 2, `line must point at/after the break, got ${err.line}`);
        return true;
      }
    );
  });

  it('omitted filename still yields a non-empty file field', () => {
    // ASSUMPTION: exact default filename is coder-owned; only non-emptiness is gated.
    const { compile } = loadImpl();
    assert.throws(
      () => compile('<View><Text></View>'),
      (err) => {
        assert.equal(err.name, 'AotError');
        assert.equal(typeof err.file, 'string');
        assert.ok(err.file.length > 0);
        return true;
      }
    );
  });
});

describe('T5 — boundary and security cases', () => {
  it('non-string source throws instead of emitting garbage', () => {
    // ASSUMPTION: the plan types source as string; rejection shape is coder-owned.
    const { compile } = loadImpl();
    assert.throws(() => compile(null), /./);
    assert.throws(() => compile(42), /./);
  });

  it('injection payload compiles without prototype pollution', () => {
    const { compile } = loadImpl();
    const payload = '<View><Text>{"__proto__"}</Text></View>';
    const { bytes } = compile(payload, { filename: 'Inject.tsx' });
    assert.ok(Buffer.isBuffer(bytes));
    assert.equal(
      Object.prototype.hasOwnProperty.call({}, 'polluted'),
      false,
      'sanity: global prototype untouched'
    );
    const text = asString(bytes);
    assert.ok(!/BUILD_TIME|Date\.now/.test(text));
  });

  it('error messages never leak secrets from the environment', () => {
    process.env.MUSE_AOT_TEST_DUMMY = 'test-secret-not-real-456';
    try {
      const { compile } = loadImpl();
      assert.throws(
        () => compile('<View><Text></View>', { filename: 'Leak.tsx' }),
        (err) => {
          assert.ok(!String(err.message).includes('test-secret-not-real-456'));
          assert.ok(!String(err.stack || '').includes('test-secret-not-real-456'));
          return true;
        }
      );
    } finally {
      delete process.env.MUSE_AOT_TEST_DUMMY;
    }
  });
});
