'use strict';
// Batch 1 T2 (AC-PACKAGING-006, FR-WORKFLOW-003, AC-WORKFLOW-008).
// Scope: CLI --archs/--packaging overrides + --print-config winning layer,
// via validateOptions/resolveConfig only. Must RED until
// packages/cli/lib/cli.js + packages/cli/lib/config.js gain the new flags.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/cli.js + lib/config.js in-process; file layer uses fs.mkdtemp dirs;
// env is a plain object argument; no network, no secrets, no live services.
// ASSUMPTION (contract genuinely silent): `--archs <csv>` and
// `--packaging <fat|split|both>` are space-form value flags like the existing
// VALUE_FLAGS pattern; env names MUSE_NATIVE_ARCHS / MUSE_NATIVE_PACKAGING.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { validateOptions, OptionError } = require('../lib/cli.js');
const { resolveConfig, ConfigError } = require('../lib/config.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

describe('T2 validateOptions accepts --archs/--packaging', () => {
  it('accepts --archs <csv>', () => {
    validateOptions(['build', 'app', '--archs', 'arm64-v8a,x86_64']);
  });

  it('accepts --packaging split', () => {
    validateOptions(['build', 'app', '--packaging', 'split']);
  });

  it('rejects invalid --packaging value naming accepted set', () => {
    assert.throws(() => validateOptions(['build', 'app', '--packaging', 'universal']), (err) => {
      assert.ok(err instanceof OptionError);
      assert.equal(err.option, '--packaging');
      assert.match(String(err.accepted), /fat.*split.*both/);
      return true;
    });
  });

  it('rejects unknown option naming option + accepted', () => {
    assert.throws(() => validateOptions(['build', 'app', '--nope', 'x']), (err) => {
      assert.ok(err instanceof OptionError);
      assert.match(String(err.option), /--nope/);
      return true;
    });
  });
});

describe('T2 resolveConfig CLI overrides + winning layer (TC-006)', () => {
  it('CLI --archs/--packaging override file values with layer "cli"', () => {
    const dir = mkTmp('muse-pack-over-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'archs: x86\npackaging: fat\n',
      'utf8',
    );
    const { config, winningLayer } = resolveConfig(
      ['--archs', 'arm64-v8a,x86_64', '--packaging', 'split'],
      {},
      dir,
    );
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(config.packaging, 'split');
    assert.equal(winningLayer.archs, 'cli');
    assert.equal(winningLayer.packaging, 'cli');
  });

  it('env layer resolves with layer "env"', () => {
    const dir = mkTmp('muse-pack-over-');
    const { config, winningLayer } = resolveConfig(
      [],
      { MUSE_NATIVE_ARCHS: 'arm64-v8a,x86_64', MUSE_NATIVE_PACKAGING: 'both' },
      dir,
    );
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(config.packaging, 'both');
    assert.equal(winningLayer.archs, 'env');
    assert.equal(winningLayer.packaging, 'env');
  });

  it('file layer resolves with layer "file"', () => {
    const dir = mkTmp('muse-pack-over-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'archs: arm64-v8a, x86_64\npackaging: both\n',
      'utf8',
    );
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(config.packaging, 'both');
    assert.equal(winningLayer.archs, 'file');
    assert.equal(winningLayer.packaging, 'file');
  });

  it('CLI beats env beats file for packaging with named winning layer', () => {
    const dir = mkTmp('muse-pack-over-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'packaging: fat\n', 'utf8');
    const fromEnv = resolveConfig([], { MUSE_NATIVE_PACKAGING: 'split' }, dir);
    assert.equal(fromEnv.config.packaging, 'split');
    assert.equal(fromEnv.winningLayer.packaging, 'env');
    const fromCli = resolveConfig(
      ['--packaging', 'both'],
      { MUSE_NATIVE_PACKAGING: 'split' },
      dir,
    );
    assert.equal(fromCli.config.packaging, 'both');
    assert.equal(fromCli.winningLayer.packaging, 'cli');
  });

  it('invalid CLI packaging value fails at configure naming accepted set', () => {
    const dir = mkTmp('muse-pack-over-');
    assert.throws(() => resolveConfig(['--packaging', 'universal'], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /universal/);
      assert.match(String(err.message), /fat, split, both/);
      return true;
    });
  });
});
