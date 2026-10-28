/**
 * TC-M3-009 — library token gate (literal fails) with the
 * application-literal advisory boundary (FR-M3-005, AC-M3-009, RULE-M3-002,
 * CONFLICT-M3-005).
 *
 * Hermetic: node:test + node:assert/strict only. Imports the entry through
 * the package public entry (index.js), drives
 * packages/native/scripts/check-tokens.js as a child process, reads
 * package.json / check-tokens.js source from disk. No network, no secrets,
 * no services.
 *
 * ASSUMPTION A1 (lintAppLiteral home): `lintAppLiteral(file, line)` is
 * exported from the package public entry (`packages/native/index.js`),
 * mirroring `driveState` (T4). The plan fixes the signature but not the
 * module; the entry is the public boundary under test.
 * ASSUMPTION A2 (advisory-only means in-process success): for application
 * code the boundary never fails — `lintAppLiteral` returns its advisory
 * record instead of throwing or exiting non-zero. Exit 0 holds because the
 * call completes in-process with an advisory record.
 * ASSUMPTION G4 (gate testability): `scripts/check-tokens.js` resolves the
 * scanned library root from the `LIB_PATH` env var when set, defaulting to
 * the package root next to the script (established by T4, ASSUMPTION G3).
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
const HERE = dirname(fileURLToPath(import.meta.url));
const NATIVE_DIR = join(HERE, '..');
const CHECK_SCRIPT = join(NATIVE_DIR, 'scripts', 'check-tokens.js');
const PACKAGE_JSON = join(NATIVE_DIR, 'package.json');

// ---------------------------------------------------------------------------
// T7 gate wiring: the T4 gate is extended (not recreated) + npm wiring
// ---------------------------------------------------------------------------

test('check-tokens.js gate script exists (T4 gate extended, not recreated)', () => {
  assert.ok(existsSync(CHECK_SCRIPT), `missing gate script: ${CHECK_SCRIPT}`);
});

test('package.json wires check-tokens.js --scope lib as a check/prebuild script', () => {
  const pkg = JSON.parse(readFileSync(PACKAGE_JSON, 'utf8'));
  const scripts = pkg.scripts ?? {};
  const wired = Object.entries(scripts).some(
    ([name, cmd]) =>
      typeof cmd === 'string' &&
      cmd.includes('check-tokens.js') &&
      /^(check|prebuild|prepublish)/.test(name),
  );
  assert.ok(wired, 'package.json must wire check-tokens.js as a check/prebuild script');
});

function runLibGate(env = {}) {
  return spawnSync(process.execPath, [CHECK_SCRIPT, '--scope', 'lib'], {
    encoding: 'utf8',
    env: { ...process.env, ...env },
  });
}

// ---------------------------------------------------------------------------
// CONFLICT-M3-005: library scope fails on literals…
// ---------------------------------------------------------------------------

test('check-tokens.js --scope lib exits 0 on the clean library tree', () => {
  const res = runLibGate();
  assert.equal(res.error, undefined, `gate failed to spawn: ${res.error}`);
  assert.equal(res.status, 0, `gate must exit 0 on the clean tree; stderr: ${res.stderr}`);
});

test('check-tokens.js --scope lib exits 1 naming file:line for a library literal', () => {
  const dir = mkdtempSync(join(tmpdir(), 'muse-lib-scope-'));
  try {
    writeFileSync(
      join(dir, 'lib-widget.js'),
      'module.exports = { color: "#ff0000", margin: "8px" };\n',
    );
    const res = runLibGate({ LIB_PATH: dir });
    assert.notEqual(res.status, 0, 'lib-scope gate must fail on a library literal');
    assert.equal(res.status, 1, 'lib-scope gate must exit 1 on a library literal');
    const out = `${res.stdout ?? ''}${res.stderr ?? ''}`;
    assert.ok(
      /lib-widget\.js:\d+/.test(out),
      `lib-scope gate must list file:line of the literal; got: ${out}`,
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

// ---------------------------------------------------------------------------
// …application scope only ever warns (FR-TSX-010 / AC-TSX-027 boundary)
// ---------------------------------------------------------------------------

test('lintAppLiteral is exported from the package entry', () => {
  const entry = require('../index.js');
  assert.equal(
    typeof entry.lintAppLiteral,
    'function',
    'lintAppLiteral(file, line) must be exported from the package entry',
  );
});

test('lintAppLiteral returns a CAPO0012 advisory record with file:line', () => {
  const { lintAppLiteral } = require('../index.js');
  const record = lintAppLiteral('app/screens/Home.js', 42);
  assert.equal(typeof record, 'object', 'lintAppLiteral must return a record object');
  assert.equal(record.code, 'CAPO0012', 'app-literal advisory code must be CAPO0012');
  assert.equal(record.severity, 'advisory', 'app-literal severity must always be advisory');
  assert.equal(typeof record.message, 'string', 'advisory message must be a string');
  assert.ok(record.message.length > 0, 'advisory message must be non-empty');
  const where = `${record.message}`;
  assert.ok(
    where.includes('app/screens/Home.js') && where.includes('42'),
    `advisory message must locate the literal (file:line); got: ${where}`,
  );
});

test('an application literal yields advisory only — never a failure (exit 0)', () => {
  const { lintAppLiteral } = require('../index.js');
  let record = null;
  let threw = null;
  try {
    record = lintAppLiteral('app/screens/Home.js', 7);
  } catch (err) {
    threw = err;
  }
  assert.equal(threw, null, 'lintAppLiteral must never throw for application code');
  assert.ok(record, 'lintAppLiteral must return an advisory record for an app literal');
  assert.equal(record.code, 'CAPO0012');
  assert.equal(record.severity, 'advisory', 'app literal must warn, never fail');
});

test('lintAppLiteral is always advisory, even for a clean app line', () => {
  const { lintAppLiteral } = require('../index.js');
  const record = lintAppLiteral('app/screens/Home.js', 1);
  assert.equal(record.code, 'CAPO0012', 'advisory code holds for any app line');
  assert.equal(record.severity, 'advisory', 'severity is always advisory, never error');
});

// ---------------------------------------------------------------------------
// CONFLICT-M3-005 scope assignment is documented at the gate
// ---------------------------------------------------------------------------

test('check-tokens.js documents the CONFLICT-M3-005 scope assignment', () => {
  const source = readFileSync(CHECK_SCRIPT, 'utf8');
  assert.ok(
    source.includes('CONFLICT-M3-005'),
    'check-tokens.js must cite CONFLICT-M3-005 (library fails, app warns)',
  );
  assert.ok(
    /app.*warn|warn.*app|advisory/i.test(source),
    'check-tokens.js must document that application literals warn (advisory) instead of failing',
  );
});
