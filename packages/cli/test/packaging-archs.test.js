'use strict';
// Batch 1 (T1 config validation + T3 template default resolution).
// T1 scope: resolveConfig archs/packaging defaults + fail-closed validation
//   (AC-PACKAGING-005, RULE-WORKFLOW-006).
// T3 scope: scaffold documents archs + unedited scaffold resolves to default
//   all-arch via T1 (AC-PACKAGING-004).
// Hermetic: node:test + node:assert/strict + stdlib only. Imports
// lib/config.js in-process; file layer uses fs.mkdtemp dirs; env is a plain
// object argument; no network, no secrets, no live services.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const { resolveConfig, ConfigError } = require('../lib/config.js');

const ALL_FOUR = ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'];

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

describe('T1 defaults — archs all four + packaging fat', () => {
  it('default resolution yields all four ABIs + fat with layers "default"', () => {
    const dir = mkTmp('muse-pack-archs-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ALL_FOUR);
    assert.equal(config.packaging, 'fat');
    assert.equal(winningLayer.archs, 'default');
    assert.equal(winningLayer.packaging, 'default');
  });
});

describe('T1 fail-closed — unknown ABI (AC-PACKAGING-005)', () => {
  it('unknown ABI in file fails at configure naming value + accepted set', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: arm64-v8a, mips\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /mips/);
      assert.match(String(err.message), /arm64-v8a, armeabi-v7a, x86_64, x86/);
      return true;
    });
  });

  it('unknown ABI via CLI fails naming value + accepted set', () => {
    const dir = mkTmp('muse-pack-archs-');
    assert.throws(() => resolveConfig(['--archs', 'x86_64,mips'], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /mips/);
      assert.match(String(err.message), /arm64-v8a, armeabi-v7a, x86_64, x86/);
      return true;
    });
  });
});

describe('T1 fail-closed — bad packaging value', () => {
  it('bad packaging value fails naming value + fat, split, both', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'packaging: universal\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /universal/);
      assert.match(String(err.message), /fat, split, both/);
      return true;
    });
  });
});

describe('T1 fail-closed — empty selection never defaults silently', () => {
  it('empty archs selection is an error', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: \n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      return true;
    });
  });
});

describe('T1 archs file forms', () => {
  it('CSV scalar file form parses to array', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: arm64-v8a, x86_64\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
    assert.equal(winningLayer.archs, 'file');
  });

  it('YAML flow-list file form parses to array', () => {
    const dir = mkTmp('muse-pack-archs-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'archs: [arm64-v8a, x86_64]\n', 'utf8');
    const { config } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ['arm64-v8a', 'x86_64']);
  });
});

describe('T3 scaffold — template documents archs + resolves to default (AC-PACKAGING-004)', () => {
  it('template file mentions archs', () => {
    const template = path.resolve(__dirname, '..', 'templates', 'muse-native.yaml');
    const text = fs.readFileSync(template, 'utf8');
    assert.match(text, /archs/);
  });

  it('template ships packaging fat default key', () => {
    const template = path.resolve(__dirname, '..', 'templates', 'muse-native.yaml');
    const text = fs.readFileSync(template, 'utf8');
    assert.match(text, /packaging/);
  });

  it('unedited scaffold resolves to all four ABIs via resolveConfig', () => {
    const template = path.resolve(__dirname, '..', 'templates', 'muse-native.yaml');
    const dir = mkTmp('muse-pack-archs-');
    fs.copyFileSync(template, path.join(dir, 'muse-native.yaml'));
    const { config } = resolveConfig([], {}, dir);
    assert.deepEqual(config.archs, ALL_FOUR);
  });
});
