'use strict';
// T6 — AOT-UI source-located diagnostics (NFR-BUILD-013 / AC-BUILD-042,
// AC-BUILD-045 second half). Exercises the T5 `compile` surface; no new impl
// file (coder splits the parser only if needed — reviewer confirms).
// Hermetic: node:test + node:assert/strict + stdlib only. Sources are
// in-memory strings; no filesystem, no network, no secrets, no live services.
// NOTE: T5 `compile.js` is already committed, so these cases run against the
// real T5 surface — a passing case here is GREEN-ON-ARRIVAL (T5 already
// satisfies the tuple), not missing behavior.
//
// Interface under test (plan verbatim):
//   none — asserts the T5 AotError contract surface (file, line, column, elementPath); no new callable
//
// ASSUMPTION (column convention): `column` is 1-based.
// ASSUMPTION (elementPath): `elementPath` names the open-element chain at the
// error site (slash-joined, e.g. `View/Text`); at top level it is `(root)`.
// ASSUMPTION (file): `AotError.file` equals `opts.filename` verbatim when
// provided, and is a non-empty string when omitted.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('./compile.js');
}

describe('T6 — diagnostics tuple on bad JSX (AC-BUILD-042)', () => {
  it('unclosed element throws AotError naming file/line/column/elementPath', () => {
    const { compile, AotError } = loadImpl();
    assert.throws(
      () => compile('<View><Text>oops', { filename: 'screen.tsx' }),
      (err) => {
        assert.ok(err instanceof AotError);
        assert.equal(err.name, 'AotError');
        assert.equal(err.file, 'screen.tsx');
        assert.ok(Number.isInteger(err.line) && err.line >= 1);
        assert.ok(Number.isInteger(err.column) && err.column >= 1);
        assert.match(String(err.elementPath), /View/);
        assert.ok(err.message.length > 0);
        return true;
      }
    );
  });

  it('mismatched closing tag reports the element path of the open element', () => {
    const { compile, AotError } = loadImpl();
    assert.throws(
      () => compile('<View><Text>hi</View>', { filename: 'screen.tsx' }),
      (err) => {
        assert.ok(err instanceof AotError);
        assert.equal(err.file, 'screen.tsx');
        assert.match(String(err.elementPath), /Text/);
        return true;
      }
    );
  });

  it('multiline error reports the correct line number', () => {
    const { compile, AotError } = loadImpl();
    const source = '<View>\n  <Text>ok</Text>\n  <Broken>oops\n</View>';
    assert.throws(
      () => compile(source, { filename: 'multi.tsx' }),
      (err) => {
        assert.ok(err instanceof AotError);
        assert.ok(err.line >= 3);
        assert.equal(err.file, 'multi.tsx');
        return true;
      }
    );
  });

  it('omitted filename still yields a non-empty file field', () => {
    const { compile, AotError } = loadImpl();
    assert.throws(
      () => compile('<View><Text>oops'),
      (err) => {
        assert.ok(err instanceof AotError);
        assert.equal(typeof err.file, 'string');
        assert.ok(err.file.length > 0);
        return true;
      }
    );
  });

  it('unexpected closing tag with no open element still carries a tuple', () => {
    const { compile, AotError } = loadImpl();
    assert.throws(
      () => compile('</View>', { filename: 'stray.tsx' }),
      (err) => {
        assert.ok(err instanceof AotError);
        assert.equal(err.file, 'stray.tsx');
        assert.ok(Number.isInteger(err.line) && Number.isInteger(err.column));
        assert.ok(String(err.elementPath).length > 0);
        return true;
      }
    );
  });
});

describe('T6 — errors fail the build, never swallowed (AC-BUILD-045)', () => {
  it('syntax error is thrown, never returned as a value', () => {
    const { compile } = loadImpl();
    let returned = 'not-called';
    try {
      returned = compile('<View><Text>oops', { filename: 't.tsx' });
    } catch (err) {
      assert.equal(err.name, 'AotError');
    }
    assert.equal(returned, 'not-called');
  });

  it('clean source does not throw (control case)', () => {
    const { compile } = loadImpl();
    const result = compile('<View><Text>ok</Text></View>', { filename: 'ok.tsx' });
    assert.ok(Buffer.isBuffer(result.bytes));
    assert.match(result.hash, /^[0-9a-f]+$/);
  });

  it('error messages never leak env secrets', () => {
    const { compile } = loadImpl();
    process.env.MUSE_AOT_TEST_DUMMY = 'top-secret-dummy-value';
    try {
      assert.throws(
        () => compile('<View><Text>oops', { filename: 't.tsx' }),
        (err) => !String(err && err.message).includes('top-secret-dummy-value')
      );
    } finally {
      delete process.env.MUSE_AOT_TEST_DUMMY;
    }
  });

  it('non-string source throws instead of emitting garbage', () => {
    const { compile } = loadImpl();
    assert.throws(() => compile(42), TypeError);
    assert.throws(() => compile(null), TypeError);
  });
});
