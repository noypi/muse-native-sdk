'use strict';
// TC-WORKFLOW-015 (AC-WORKFLOW-015, FR-WORKFLOW-006, RULE-WORKFLOW-002/007) — Batch 1.
// Scope: renderer schema/default/fail-closed ONLY (T1 + T2). This file is shared
// with TC-GRAPHICS-042 (AC-GRAPHICS-040 present-path selection, graphics run);
// NO present-path selection is asserted here — that belongs to the graphics run.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports lib/config.js
// and lib/cli.js in-process; file layer uses fs.mkdtemp dirs; env is a plain
// object argument; no network, no secrets, no live services, no dist/ writes.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const config = require('../lib/config.js');
const cli = require('../lib/cli.js');

const { resolveConfig, validateConfig, ConfigError } = config;

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

function noDist(dir) {
  assert.ok(!fs.existsSync(path.join(dir, 'dist')), 'no dist/ writes on failure');
}

// ---- T1: schema + default (AC-WORKFLOW-015 default `cpu`) ----

describe('T1 renderer schema and default (AC-WORKFLOW-015)', () => {
  it("SCHEMA declares renderer as 'string'", () => {
    assert.equal(config.SCHEMA.renderer, 'string');
  });

  it("DEFAULTS pins renderer to 'cpu'", () => {
    assert.equal(config.DEFAULTS.renderer, 'cpu');
  });

  it('resolving with no renderer in any layer yields cpu with winningLayer default', () => {
    const dir = mkTmp('muse-renderer-');
    const { config: resolved, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(resolved.renderer, 'cpu');
    assert.equal(winningLayer.renderer, 'default');
  });

  it('CLI --renderer wins and names the cli layer', () => {
    const dir = mkTmp('muse-renderer-');
    const { config: resolved, winningLayer } = resolveConfig(['--renderer', 'egl'], {}, dir);
    assert.equal(resolved.renderer, 'egl');
    assert.equal(winningLayer.renderer, 'cli');
  });

  it('env MUSE_NATIVE_RENDERER wins and names the env layer', () => {
    const dir = mkTmp('muse-renderer-');
    const { config: resolved, winningLayer } = resolveConfig([], { MUSE_NATIVE_RENDERER: 'skia' }, dir);
    assert.equal(resolved.renderer, 'skia');
    assert.equal(winningLayer.renderer, 'env');
  });

  it('file muse-native.yaml wins and names the file layer (RULE-WORKFLOW-007 per-project file)', () => {
    const dir = mkTmp('muse-renderer-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'renderer: egl\n', 'utf8');
    const { config: resolved, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(resolved.renderer, 'egl');
    assert.equal(winningLayer.renderer, 'file');
  });

  it('CLI beats env beats file (documented precedence)', () => {
    const dir = mkTmp('muse-renderer-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'renderer: cpu\n', 'utf8');
    const envOnly = resolveConfig([], { MUSE_NATIVE_RENDERER: 'skia' }, dir);
    assert.equal(envOnly.config.renderer, 'skia');
    assert.equal(envOnly.winningLayer.renderer, 'env');
    const cliWins = resolveConfig(['--renderer', 'egl'], { MUSE_NATIVE_RENDERER: 'skia' }, dir);
    assert.equal(cliWins.config.renderer, 'egl');
    assert.equal(cliWins.winningLayer.renderer, 'cli');
  });

  it('each accepted value resolves verbatim via CLI', () => {
    const dir = mkTmp('muse-renderer-');
    for (const value of ['cpu', 'egl', 'skia']) {
      const { config: resolved } = resolveConfig(['--renderer', value], {}, dir);
      assert.equal(resolved.renderer, value);
    }
  });
});

// ---- T2: fail closed on unknown values at configure (RULE-WORKFLOW-002) ----

describe('T2 fail-closed — file layer (AC-WORKFLOW-015)', () => {
  it('unknown renderer in file throws ConfigError naming renderer + cpu, egl, skia', () => {
    const dir = mkTmp('muse-renderer-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'renderer: vulkan\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /renderer/);
      assert.match(String(err.message), /cpu, egl, skia/);
      return true;
    });
    noDist(dir);
  });

  it('validateConfig rejects an unknown renderer naming the option and accepted values', () => {
    assert.throws(() => validateConfig({ renderer: 'vulkan' }, 'muse-native.yaml'), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /renderer/);
      assert.match(String(err.message), /cpu, egl, skia/);
      return true;
    });
  });

  it('validateConfig accepts each of cpu, egl, skia', () => {
    for (const value of ['cpu', 'egl', 'skia']) {
      assert.doesNotThrow(() => validateConfig({ renderer: value }, 'muse-native.yaml'));
    }
  });

  it('non-string renderer in file fails naming renderer', () => {
    assert.throws(() => validateConfig({ renderer: 42 }, 'muse-native.yaml'), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /renderer/);
      return true;
    });
  });
});

describe('T2 fail-closed — assertRendererValue helper (CFG-BAD-RENDERER)', () => {
  it('config.js exports assertRendererValue()', () => {
    assert.equal(typeof config.assertRendererValue, 'function', 'config.js must export assertRendererValue()');
  });

  it("assertRendererValue accepts cpu, egl, skia without throwing", () => {
    for (const value of ['cpu', 'egl', 'skia']) {
      assert.doesNotThrow(() => config.assertRendererValue(value, '<test>'));
    }
  });

  it('assertRendererValue rejects vulkan with ConfigError code CFG-BAD-RENDERER', () => {
    assert.throws(() => config.assertRendererValue('vulkan', '<argv>'), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.code, 'CFG-BAD-RENDERER');
      assert.match(String(err.message), /renderer/);
      assert.match(String(err.message), /cpu, egl, skia/);
      return true;
    });
  });
});

describe('T2 fail-closed — CLI and env layers (AC-WORKFLOW-015)', () => {
  it('unknown --renderer value throws at configure naming --renderer + cpu, egl, skia, no dist/', () => {
    const dir = mkTmp('muse-renderer-');
    assert.throws(() => resolveConfig(['--renderer', 'vulkan'], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /renderer/);
      assert.match(String(err.message), /cpu, egl, skia/);
      return true;
    });
    noDist(dir);
  });

  it('unknown MUSE_NATIVE_RENDERER value throws at configure naming renderer + values, no dist/', () => {
    const dir = mkTmp('muse-renderer-');
    assert.throws(() => resolveConfig([], { MUSE_NATIVE_RENDERER: 'vulkan' }, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /renderer/);
      assert.match(String(err.message), /cpu, egl, skia/);
      return true;
    });
    noDist(dir);
  });

  it('validateOptions accepts --renderer egl without throwing', () => {
    assert.equal(typeof cli.validateOptions, 'function', 'cli.js must export validateOptions()');
    assert.doesNotThrow(() => cli.validateOptions(['build', '--renderer', 'egl']));
  });

  it('validateOptions rejects --renderer vulkan with OptionError naming --renderer + cpu, egl, skia', () => {
    assert.throws(() => cli.validateOptions(['build', '--renderer', 'vulkan']), (err) => {
      assert.equal(err.name, 'OptionError');
      assert.match(String(err.option), /--renderer/);
      assert.match(String(err.accepted), /cpu, egl, skia/);
      return true;
    });
  });
});
