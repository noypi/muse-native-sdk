'use strict';
// MERGED rebase resolution: keeps BOTH add-add sides (different modules).
// HEAD side - TC-WORKFLOW-015 (AC-WORKFLOW-015, FR-WORKFLOW-006, RULE-WORKFLOW-002-007):
// workflow-owned renderer option validation at config-load time
// (lib config.js + lib cli.js). Hermetic: node:test + node:assert-strict +
// stdlib only; file layer uses fs.mkdtemp dirs; env is a plain object argument.
// Ours side (b526001, AC-GRAPHICS-041, TC-GRAPHICS-043 CLI half) -
// graphics-owned parseRendererPreference and resolveEffectivePath unit tests over
// lib renderer-preference.js (pure parse + probe-downgrade reporting per
// FR-GRAPHICS-014). Imports deduplicated; both afterEach hooks retained.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const config = require('../lib/config.js');
const cli = require('../lib/cli.js');

const { resolveConfig, validateConfig, ConfigError } = config;

const MODULE_PATH = '../lib/renderer-preference.js';

function load() {
  return require(MODULE_PATH);
}

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

const savedEnv = { ...process.env };
afterEach(() => {
  for (const k of Object.keys(process.env)) {
    if (!(k in savedEnv)) delete process.env[k];
  }
  for (const [k, v] of Object.entries(savedEnv)) process.env[k] = v;
});

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

// --- parseRendererPreference ------------------------------------------------

describe('T1 — parseRendererPreference defaults (AC-GRAPHICS-041)', () => {
  it("null yields requested 'cpu'", () => {
    const r = load().parseRendererPreference(null);
    assert.equal(r.requested, 'cpu');
  });

  it("undefined yields requested 'cpu'", () => {
    const r = load().parseRendererPreference(undefined);
    assert.equal(r.requested, 'cpu');
  });

  it("'' yields requested 'cpu'", () => {
    const r = load().parseRendererPreference('');
    assert.equal(r.requested, 'cpu');
  });

  it("default reports requested and effective separately (effective 'cpu', not downgraded, empty trigger)", () => {
    const r = load().parseRendererPreference(null);
    assert.equal(r.effective, 'cpu');
    assert.equal(r.downgraded, false);
    assert.equal(r.trigger, '');
  });
});

describe('T1 — parseRendererPreference accepted values (AC-GRAPHICS-041)', () => {
  for (const value of ['cpu', 'egl', 'skia']) {
    it(`'${value}' is accepted with requested '${value}'`, () => {
      const r = load().parseRendererPreference(value);
      assert.equal(r.requested, value);
    });
  }

  it('known value without probe info is not downgraded (effective===requested, empty trigger)', () => {
    // ASSUMPTION A-P1: parse alone performs no probing — it has no probe
    // argument — so effective===requested, downgraded===false, trigger==='' .
    for (const value of ['cpu', 'egl', 'skia']) {
      const r = load().parseRendererPreference(value);
      assert.equal(r.effective, value);
      assert.equal(r.downgraded, false);
      assert.equal(r.trigger, '');
    }
  });
});

describe('T1 — parseRendererPreference fail-closed (AC-GRAPHICS-041)', () => {
  it("unknown string throws Error naming option and values ('renderer: unknown value ... (expected cpu|egl|skia)')", () => {
    const { parseRendererPreference } = load();
    assert.throws(
      () => parseRendererPreference('vulkan'),
      (err) =>
        err instanceof Error &&
        /renderer: unknown value/.test(err.message) &&
        /expected cpu\|egl\|skia/.test(err.message)
    );
  });

  it('unknown value error is a thrown Error (no return-code contract)', () => {
    const { parseRendererPreference } = load();
    let caught = null;
    try {
      parseRendererPreference('metal');
    } catch (err) {
      caught = err;
    }
    assert.ok(caught instanceof Error, 'must throw, not return a code');
  });

  it("case-variant 'EGL' throws (exact match only)", () => {
    // ASSUMPTION A-P2: matching is case-sensitive; the Interface line names
    // only lowercase values, so 'EGL' is unknown and throws.
    assert.throws(() => load().parseRendererPreference('EGL'), /renderer: unknown value/);
  });

  it('non-string truthy input throws the same renderer Error', () => {
    // ASSUMPTION A-P3: the Interface line pins strings; non-string input is
    // fail-closed with the same unknown-value Error rather than coerced.
    for (const raw of [42, true, {}, ['egl']]) {
      assert.throws(() => load().parseRendererPreference(raw), /renderer: unknown value/, `raw=${String(raw)}`);
    }
  });

  it('never reads env/config beyond its arguments', () => {
    process.env.RENDERER = 'skia';
    process.env.renderer = 'egl';
    process.env.MUSE_RENDERER = 'skia';
    const r = load().parseRendererPreference(null);
    assert.equal(r.requested, 'cpu');
    assert.equal(r.effective, 'cpu');
  });
});

// --- resolveEffectivePath ----------------------------------------------------

describe('T1 — resolveEffectivePath keeps available paths (AC-GRAPHICS-041)', () => {
  it("requested 'cpu' stays 'cpu' with empty trigger even when nothing is available", () => {
    const r = load().resolveEffectivePath('cpu', { eglAvailable: false, skiaAvailable: false });
    assert.equal(r.effective, 'cpu');
    assert.equal(r.trigger, '');
  });

  it("requested 'egl' stays 'egl' with empty trigger when available", () => {
    const r = load().resolveEffectivePath('egl', { eglAvailable: true, skiaAvailable: false });
    assert.equal(r.effective, 'egl');
    assert.equal(r.trigger, '');
  });

  it("requested 'skia' stays 'skia' with empty trigger when available", () => {
    const r = load().resolveEffectivePath('skia', { eglAvailable: false, skiaAvailable: true });
    assert.equal(r.effective, 'skia');
    assert.equal(r.trigger, '');
  });
});

describe('T1 — resolveEffectivePath downgrades unavailable paths (AC-GRAPHICS-041)', () => {
  it("unavailable 'egl' downgrades to 'cpu' with a non-empty egl-naming trigger", () => {
    const r = load().resolveEffectivePath('egl', { eglAvailable: false, skiaAvailable: true });
    assert.equal(r.effective, 'cpu');
    assert.ok(typeof r.trigger === 'string' && r.trigger.length > 0, 'trigger must be non-empty');
    assert.match(r.trigger.toLowerCase(), /egl/);
  });

  it("unavailable 'skia' downgrades to 'cpu' with a non-empty skia-naming trigger", () => {
    const r = load().resolveEffectivePath('skia', { eglAvailable: true, skiaAvailable: false });
    assert.equal(r.effective, 'cpu');
    assert.ok(typeof r.trigger === 'string' && r.trigger.length > 0, 'trigger must be non-empty');
    assert.match(r.trigger.toLowerCase(), /skia/);
  });

  it("both paths unavailable: requested 'egl' still resolves to 'cpu' (never blank)", () => {
    const r = load().resolveEffectivePath('egl', { eglAvailable: false, skiaAvailable: false });
    assert.equal(r.effective, 'cpu');
    assert.ok(r.trigger.length > 0);
  });

  it('unknown requested path throws the renderer Error', () => {
    // ASSUMPTION A-P4: fail-closed applies here too — the Interface line's
    // throw contract is stated once for the module, so resolve rejects
    // unknown values instead of resolving them.
    assert.throws(
      () => load().resolveEffectivePath('vulkan', { eglAvailable: true, skiaAvailable: true }),
      /renderer: unknown value/
    );
  });
});
