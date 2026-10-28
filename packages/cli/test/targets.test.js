'use strict';
// TC-WORKFLOW-007 (T1: FR-WORKFLOW-003 AC-WORKFLOW-007; FR-WORKFLOW-005 AC-WORKFLOW-014 strict key).
// T1 scope: typed config schema + printable resolution only (resolveConfig / getOptionSchema).
// No coder running; these tests must RED until packages/cli/lib/config.js gains the new keys.
// Hermetic: node:test + node:assert/strict + stdlib only. Imports lib/config.js
// in-process; file layer uses fs.mkdtemp dirs; env is a plain object argument
// (process.env never read/mutated); no network, no secrets, no live services.
// ASSUMPTION A1 (contract genuinely silent): boolean keys (withSample,
// release, strict) use bare CLI flags (`--with-sample`, `--release`,
// `--strict` meaning true) and `key: true/false` yaml scalars; value keys
// (abi, modules, output) use `--<key> <value>` / string scalars; env form is
// MUSE_NATIVE_<UPPER_SNAKE> via existing envNameFor; defaults are
// withSample:false, release:false, strict:false, abi/modules/output undefined,
// diagnostics undefined. Positional-target equivalence is T2, not tested here.

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

describe('T1 targets — withSample/strict/diagnostics schema + resolution', () => {
  it('defaults: withSample:false, strict:false, diagnostics undefined, all layer "default"', () => {
    const dir = mkTmp('muse-t1-targets-');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.withSample, false);
    assert.equal(config.strict, false);
    assert.equal(config.diagnostics, undefined);
    assert.equal(winningLayer.withSample, 'default');
    assert.equal(winningLayer.strict, 'default');
  });

  it('CLI --with-sample selects the sample with layer "cli"', () => {
    const dir = mkTmp('muse-t1-targets-');
    const { config, winningLayer } = resolveConfig(['--with-sample'], {}, dir);
    assert.equal(config.withSample, true);
    assert.equal(winningLayer.withSample, 'cli');
  });

  it('file key withSample:true resolves with layer "file"', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'withSample: true\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], {}, dir);
    assert.equal(config.withSample, true);
    assert.equal(winningLayer.withSample, 'file');
  });

  it('env MUSE_NATIVE_WITH_SAMPLE beats file with layer "env"', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'withSample: false\n', 'utf8');
    const { config, winningLayer } = resolveConfig([], { MUSE_NATIVE_WITH_SAMPLE: 'true' }, dir);
    assert.equal(winningLayer.withSample, 'env');
    assert.equal(config.withSample, 'true');
  });

  it('CLI --strict is reflected with layer "cli"', () => {
    const dir = mkTmp('muse-t1-targets-');
    const { config, winningLayer } = resolveConfig(['--strict'], {}, dir);
    assert.equal(config.strict, true);
    assert.equal(winningLayer.strict, 'cli');
  });

  it('wrong type for withSample throws ConfigError naming key/file/expected, no partial return', () => {
    const dir = mkTmp('muse-t1-targets-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'withSample: 42\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'withSample');
      assert.ok(String(err.message).includes('withSample'));
      return true;
    });
  });

  it('getOptionSchema includes withSample/strict/diagnostics from live SCHEMA', () => {
    const names = new Map(getOptionSchema().map((e) => [e.name, e]));
    for (const key of ['withSample', 'strict', 'diagnostics']) {
      assert.ok(names.has(key), `schema must include ${key}`);
    }
    assert.equal(names.get('withSample').default, false);
    assert.equal(names.get('strict').default, false);
  });
});
