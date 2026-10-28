'use strict';
// TC-WORKFLOW-008 (T1: FR-WORKFLOW-003 AC-WORKFLOW-008; FR-WORKFLOW-005 AC-WORKFLOW-014 report).
// T1 scope: --abi/--release/--modules/--output overrides reflected in the
// resolved config + winningLayer, via resolveConfig/getOptionSchema only.
// Must RED until packages/cli/lib/config.js gains the new keys.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/config.js in-process; file layer uses fs.mkdtemp dirs; env is a plain
// object argument; no network, no secrets, no live services.
// ASSUMPTION A1 (contract genuinely silent): `--abi X`, `--modules M`,
// `--output D` are space-form value flags; `--release` is a bare boolean
// flag meaning true; env names MUSE_NATIVE_ABI / MUSE_NATIVE_RELEASE /
// MUSE_NATIVE_MODULES / MUSE_NATIVE_OUTPUT; defaults abi/modules/output
// undefined, release false.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveConfig, getOptionSchema, ConfigError } = require('../lib/config.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

describe('T1 overrides — abi/release/modules/output schema + resolution', () => {
  it('defaults: abi/modules/output undefined, release false, layers "default"', () => {
    const dir = mkTmp('muse-t1-overrides-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.abi, undefined);
    assert.equal(config.modules, undefined);
    assert.equal(config.output, undefined);
    assert.equal(config.release, false);
    assert.equal(winningLayer.abi, 'default');
    assert.equal(winningLayer.release, 'default');
    assert.equal(winningLayer.modules, 'default');
    assert.equal(winningLayer.output, 'default');
  });

  it('CLI --abi/--release/--modules/--output override with layer "cli"', () => {
    const dir = mkTmp('muse-t1-overrides-');
    const { config, winningLayer } = resolveConfig(
      ['--abi', 'arm64-v8a', '--release', '--modules', 'a,b', '--output', 'dist/x'],
      {},
      dir,
    );
    assert.equal(config.abi, 'arm64-v8a');
    assert.equal(config.release, true);
    assert.equal(config.modules, 'a,b');
    assert.equal(config.output, 'dist/x');
    assert.equal(winningLayer.abi, 'cli');
    assert.equal(winningLayer.release, 'cli');
    assert.equal(winningLayer.modules, 'cli');
    assert.equal(winningLayer.output, 'cli');
  });

  it('CLI beats env beats file for abi with named winning layer', () => {
    const dir = mkTmp('muse-t1-overrides-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'abi: from-file\n', 'utf8');
    const fromEnv = resolveConfig([], { MUSE_NATIVE_ABI: 'from-env' }, dir);
    assert.equal(fromEnv.config.abi, 'from-env');
    assert.equal(fromEnv.winningLayer.abi, 'env');
    const fromCli = resolveConfig(['--abi', 'from-cli'], { MUSE_NATIVE_ABI: 'from-env' }, dir);
    assert.equal(fromCli.config.abi, 'from-cli');
    assert.equal(fromCli.winningLayer.abi, 'cli');
  });

  it('file keys resolve with layer "file"', () => {
    const dir = mkTmp('muse-t1-overrides-');
    fs.writeFileSync(
      path.join(dir, 'muse-native.yaml'),
      'abi: arm64-v8a\nrelease: true\nmodules: a,b\noutput: dist/x\n',
      'utf8',
    );
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.abi, 'arm64-v8a');
    assert.equal(config.release, true);
    assert.equal(winningLayer.abi, 'file');
    assert.equal(winningLayer.release, 'file');
  });

  it('unknown key throws ConfigError naming key/file/expected with no partial return', () => {
    const dir = mkTmp('muse-t1-overrides-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'nope: 1\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'nope');
      return true;
    });
  });

  it('wrong type for abi throws ConfigError naming key and expected type', () => {
    const dir = mkTmp('muse-t1-overrides-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'abi: 42\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'abi');
      assert.ok(String(err.message).includes('abi'));
      return true;
    });
  });

  it('getOptionSchema includes abi/release/modules/output from live SCHEMA', () => {
    const names = new Map(getOptionSchema().map((e) => [e.name, e]));
    for (const key of ['abi', 'release', 'modules', 'output']) {
      assert.ok(names.has(key), `schema must include ${key}`);
    }
    assert.equal(names.get('release').default, false);
  });
});
