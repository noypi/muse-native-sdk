'use strict';
// TC-WORKFLOW-010 (T5: FR-WORKFLOW-004, AC-WORKFLOW-010) — Batch 3.
// `muse-native build --help` lists every option with type, default and
// allowed values, generated from the same schema object T1/T2 validate
// against so list and schema cannot drift. The test iterates
// `getOptionSchema()` vs help stdout.
// Hermetic: node:test + node:assert/strict + stdlib only. Spawns
// `node bin/muse-native.js` with no app dir (help needs no project);
// no network, no secrets, no live services.
// Scope: help CONTENT only. Out-of-scope flags (`--with-sample`, `--strict`,
// `--abi`, `--release`, `--modules`, `--output`, `--archs`, `--packaging`)
// are rejected as unknown per plan A3 — but rejection behavior belongs to
// T6; this file asserts only that help lists what the schema owns.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T5a: `getOptionSchema()` is exported from `packages/cli/lib/config.js`
//   (the module that owns the schema T1/T2 validate against); each entry is
//   `{ name: string, type: string, default: unknown, allowed?: unknown[] }`.
// - A-T5b: help is invoked as `muse-native build --help` (plan Interface line)
//   with no `<appDir>`; it exits 0 without requiring a project dir.
// - A-T5c: for each schema entry, help stdout names the `--<name>` flag and
//   states its type and default on/near the same line; entries with an
//   `allowed` list state the allowed values.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

describe('TC-WORKFLOW-010 — build --help lists every schema option', () => {
  it('getOptionSchema is exported and returns entries with name/type/default', () => {
    const config = require('../lib/config.js');
    assert.equal(typeof config.getOptionSchema, 'function', 'config.js must export getOptionSchema()');
    const schema = config.getOptionSchema();
    assert.ok(Array.isArray(schema), 'getOptionSchema() must return an array');
    assert.ok(schema.length > 0, 'schema must own at least one option');
    for (const entry of schema) {
      assert.equal(typeof entry.name, 'string', 'each entry must have a string name');
      assert.equal(typeof entry.type, 'string', 'each entry must have a string type');
      assert.ok(Object.prototype.hasOwnProperty.call(entry, 'default'), 'each entry must have a default key');
      if (entry.allowed !== undefined) {
        assert.ok(Array.isArray(entry.allowed), `allowed for '${entry.name}' must be an array`);
      }
    }
  });

  it('muse-native build --help exits 0', () => {
    const r = runCli(['build', '--help']);
    assert.equal(r.status, 0, `build --help must exit 0, got ${r.status}: ${r.stderr}`);
  });

  it('help stdout lists every schema entry by --<name> flag', () => {
    const { getOptionSchema } = require('../lib/config.js');
    const schema = getOptionSchema();
    const r = runCli(['build', '--help']);
    assert.equal(r.status, 0, `build --help must exit 0, got ${r.status}: ${r.stderr}`);
    for (const entry of schema) {
      assert.ok(
        r.stdout.includes(`--${entry.name}`),
        `help must list --${entry.name}, got:\n${r.stdout}`,
      );
    }
  });

  it('help states type and default for every schema entry', () => {
    const { getOptionSchema } = require('../lib/config.js');
    const schema = getOptionSchema();
    const r = runCli(['build', '--help']);
    assert.equal(r.status, 0, `build --help must exit 0, got ${r.status}: ${r.stderr}`);
    for (const entry of schema) {
      assert.ok(
        r.stdout.includes(String(entry.type)),
        `help must state type '${entry.type}' for --${entry.name}, got:\n${r.stdout}`,
      );
      assert.ok(
        r.stdout.includes(String(entry.default)),
        `help must state default '${String(entry.default)}' for --${entry.name}, got:\n${r.stdout}`,
      );
    }
  });

  it('help states allowed values for entries that define them', () => {
    const { getOptionSchema } = require('../lib/config.js');
    const schema = getOptionSchema().filter((e) => Array.isArray(e.allowed));
    const r = runCli(['build', '--help']);
    assert.equal(r.status, 0, `build --help must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(schema.length > 0, 'schema must define allowed values for at least one option (e.g. --stage)');
    for (const entry of schema) {
      for (const value of entry.allowed) {
        assert.ok(
          r.stdout.includes(String(value)),
          `help must list allowed value '${String(value)}' for --${entry.name}, got:\n${r.stdout}`,
        );
      }
    }
  });

  it('top-level muse-native --help routing stays intact (control)', () => {
    const r = runCli(['--help']);
    assert.equal(r.status, 0, `top-level --help must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(r.stdout.includes('build'), 'top-level help must still list the build command');
  });
});
