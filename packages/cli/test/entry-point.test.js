'use strict';
// T2 — CLI single entry point registry (NFR-BUILD-003 / AC-BUILD-003).
// Hermetic: node:test + node:assert only. No CLI subprocesses, no network,
// no secrets, no live services. Does not modify packages/cli/lib/cli.js.
// Red state: ../lib/entry-point.js does not exist yet, so every case below
// fails with MODULE_NOT_FOUND until T2 lands.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

function loadImpl() {
  return require('../lib/entry-point.js');
}

const OPERATIONS = ['create', 'build', 'run', 'test', 'profile', 'package'];

describe('T2 — getEntryPointCommands covers all six operations', () => {
  it('returns an entry for each of create/build/run/test/profile/package', () => {
    const { getEntryPointCommands } = loadImpl();
    const entries = getEntryPointCommands();
    assert.ok(Array.isArray(entries), 'must return an array');
    for (const operation of OPERATIONS) {
      const entry = entries.find((e) => e.operation === operation);
      assert.ok(entry, `must include an entry for operation '${operation}'`);
      assert.equal(typeof entry.command, 'string', `'${operation}' entry must carry a command string`);
      assert.ok(entry.command.length > 0, `'${operation}' command must be non-empty (routes through the CLI, never bypasses it)`);
    }
  });

  it('create maps to the init alias', () => {
    const { getEntryPointCommands } = loadImpl();
    const entry = getEntryPointCommands().find((e) => e.operation === 'create');
    assert.ok(entry.command.includes('init'), `create must map to the init alias, got '${entry.command}'`);
  });

  it('build and run map directly to their CLI commands', () => {
    const { getEntryPointCommands } = loadImpl();
    const entries = getEntryPointCommands();
    assert.equal(entries.find((e) => e.operation === 'build').command, 'build');
    assert.equal(entries.find((e) => e.operation === 'run').command, 'run');
  });

  it('test, profile and package resolve to CLI commands (never a bypass)', () => {
    const { getEntryPointCommands } = loadImpl();
    const entries = getEntryPointCommands();
    for (const operation of ['test', 'profile', 'package']) {
      const entry = entries.find((e) => e.operation === operation);
      assert.ok(entry, `must include an entry for operation '${operation}'`);
      assert.equal(typeof entry.command, 'string');
      assert.ok(entry.command.length > 0, `'${operation}' must resolve to a CLI command string`);
    }
  });

  it("unknown operations throw Error('unknown operation: <name>')", () => {
    const { getEntryPointCommands } = loadImpl();
    assert.throws(() => getEntryPointCommands('no-such-op'), /unknown operation: no-such-op/);
  });
});

describe('T2 — existing CLI dispatch is unchanged', () => {
  it('packages/cli/lib/cli.js still exposes its dispatch surface', () => {
    const cli = require('../lib/cli.js');
    assert.equal(typeof cli.main, 'function', 'cli.js must still export main');
    assert.equal(typeof cli.validateOptions, 'function', 'cli.js must still export validateOptions');
    assert.equal(cli.OptionError.name, 'OptionError', 'cli.js must still export OptionError');
  });
});
