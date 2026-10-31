'use strict';
// TC-WORKFLOW-016 (AC-WORKFLOW-015 help part + AC-WORKFLOW-016, FR-WORKFLOW-006,
// RULE-WORKFLOW-002) — Batch 2 (T3, T4).
// Hermetic: node:test + node:assert/strict + stdlib only. Imports lib/config.js
// in-process; build --help / --print-config via `node bin/muse-native.js`
// spawn against fs.mkdtemp dirs; template read from templates/muse-native.yaml;
// no network, no secrets, no live services, no dist/ writes, no process.env mutation.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const config = require('../lib/config.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

// ---------------------------------------------------------------------------
// T3 — help part (AC-WORKFLOW-015): getOptionSchema renderer entry + help +
// scaffold template.
// Interface (plan verbatim):
// `getOptionSchema(): Array<{ name: string, type: string, default: unknown,
// allowed?: string[] }>` includes `{ name: 'renderer', type: 'string',
// default: 'cpu', allowed: ['cpu', 'egl', 'skia'] }`
// ---------------------------------------------------------------------------
describe('T3 getOptionSchema renderer entry exact shape (AC-WORKFLOW-015)', () => {
  it("schema includes { name: 'renderer', type: 'string', default: 'cpu', allowed: ['cpu','egl','skia'] }", () => {
    assert.equal(typeof config.getOptionSchema, 'function', 'config.js must export getOptionSchema()');
    const schema = config.getOptionSchema();
    const entry = schema.find((e) => e.name === 'renderer');
    assert.ok(entry, 'getOptionSchema() must include a renderer entry');
    assert.deepEqual(
      entry,
      { name: 'renderer', type: 'string', default: 'cpu', allowed: ['cpu', 'egl', 'skia'] },
      'renderer entry must match the schema exactly',
    );
  });

  it('renderer allowed list matches the live RENDERER validation set', () => {
    const entry = config.getOptionSchema().find((e) => e.name === 'renderer');
    assert.ok(entry, 'renderer entry must exist');
    // Every allowed value must validate; anything else must throw ConfigError.
    for (const value of entry.allowed) {
      assert.doesNotThrow(() => config.validateConfig({ renderer: value }, '<test>'));
    }
    assert.throws(() => config.validateConfig({ renderer: 'vulkan' }, '<test>'), /renderer/);
  });
});

describe('T3 printBuildHelp lists --renderer with type/default/allowed (AC-WORKFLOW-015)', () => {
  it('build --help names --renderer with its type, default and every allowed value', () => {
    const entry = config.getOptionSchema().find((e) => e.name === 'renderer');
    assert.ok(entry, 'rendererentry must exist');
    const r = runCli(['build', '--help']);
    assert.equal(r.status, 0, `build --help must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(r.stdout.includes('--renderer'), 'help must list --renderer');
    assert.ok(r.stdout.includes(entry.type), `help must state type '${entry.type}' for --renderer`);
    assert.ok(r.stdout.includes(String(entry.default)), `help must state default '${entry.default}' for --renderer`);
    for (const value of entry.allowed) {
      assert.ok(r.stdout.includes(value), `help must list allowed value '${value}' for --renderer`);
    }
  });
});

describe('T3 scaffold template documents renderer without changing default (AC-WORKFLOW-015)', () => {
  it('templates/muse-native.yaml documents the renderer option', () => {
    const text = fs.readFileSync(path.join(CLI_DIR, 'templates', 'muse-native.yaml'), 'utf8');
    assert.ok(text.includes('renderer'), 'scaffold template must document renderer');
    assert.ok(/cpu/.test(text), 'template renderer docs must name the cpu default');
    assert.ok(/egl/.test(text), 'template renderer docs must name egl');
    assert.ok(/skia/.test(text), 'template renderer docs must name skia');
  });

  it('scaffold default still resolves to cpu (template adds docs, not a new default)', () => {
    const text = fs.readFileSync(path.join(CLI_DIR, 'templates', 'muse-native.yaml'), 'utf8');
    const rendererLines = text.split('\n').filter((l) => /^\s*renderer\s*:/.test(l) && !l.trim().startsWith('#'));
    for (const line of rendererLines) {
      assert.match(line, /cpu/, 'any active renderer: line in the template must keep the cpu default');
    }
    assert.equal(config.DEFAULTS.renderer, 'cpu', 'DEFAULTS.renderer must stay cpu');
  });
});

// ---------------------------------------------------------------------------
// T4 — resolved renderer preference (AC-WORKFLOW-016, FR-WORKFLOW-006).
// Interface (plan verbatim):
// `resolveRendererPreference(resolvedConfig: { config: { renderer: string } },
// probe?: { effective?: string, trigger?: string }):
// { requested: 'cpu' | 'egl' | 'skia', effective: 'cpu' | 'egl' | 'skia',
// downgraded: boolean, trigger?: string }`
// — `requested` echoes `config.renderer` verbatim;
// `effective` equals `probe.effective` when a valid probe value is supplied,
// else `requested`; `downgraded` is true iff `effective !== requested`
// ---------------------------------------------------------------------------
describe('T4 resolveRendererPreference — requested/effective/downgraded (AC-WORKFLOW-016)', () => {
  it('config.js exports resolveRendererPreference()', () => {
    assert.equal(typeof config.resolveRendererPreference, 'function', 'config.js must export resolveRendererPreference()');
  });

  it('no probe: effective equals requested, downgraded false, no trigger', () => {
    for (const value of ['cpu', 'egl', 'skia']) {
      const out = config.resolveRendererPreference({ config: { renderer: value } });
      assert.deepEqual(
        out,
        { requested: value, effective: value, downgraded: false },
        `no-probe for '${value}' must echo requested with downgraded false`,
      );
    }
  });

  it('probe cpu trigger on requested egl returns downgraded true with the trigger', () => {
    const out = config.resolveRendererPreference(
      { config: { renderer: 'egl' } },
      { effective: 'cpu', trigger: 'egl-probe-failed' },
    );
    assert.equal(out.requested, 'egl');
    assert.equal(out.effective, 'cpu');
    assert.equal(out.downgraded, true);
    assert.equal(out.trigger, 'egl-probe-failed');
  });

  it('probe matching requested is not a downgrade (trigger preserved, downgraded false)', () => {
    const out = config.resolveRendererPreference(
      { config: { renderer: 'egl' } },
      { effective: 'egl', trigger: 'egl-probe-ok' },
    );
    assert.equal(out.requested, 'egl');
    assert.equal(out.effective, 'egl');
    assert.equal(out.downgraded, false);
  });

  it('invalid probe effective falls back to requested with downgraded false', () => {
    const out = config.resolveRendererPreference(
      { config: { renderer: 'skia' } },
      { effective: 'vulkan', trigger: 'bogus-probe' },
    );
    assert.equal(out.requested, 'skia');
    assert.equal(out.effective, 'skia');
    assert.equal(out.downgraded, false);
  });

  it('missing probe.effective falls back to requested with downgraded false', () => {
    const out = config.resolveRendererPreference(
      { config: { renderer: 'egl' } },
      { trigger: 'no-value' },
    );
    assert.equal(out.requested, 'egl');
    assert.equal(out.effective, 'egl');
    assert.equal(out.downgraded, false);
  });

  it('never mutates config.renderer (preference-only; downgrade stays engine-owned)', () => {
    const resolvedConfig = { config: { renderer: 'egl' } };
    const out = config.resolveRendererPreference(resolvedConfig, { effective: 'cpu', trigger: 'probe' });
    assert.equal(resolvedConfig.config.renderer, 'egl', 'config.renderer must be unchanged');
    assert.equal(out.requested, 'egl');
    assert.equal(out.effective, 'cpu');
  });
});

describe('T4 --print-config pair shape: requested always present, effective defaults to requested pre-probe', () => {
  it('--print-config carries requested + effective with effective == requested pre-probe and downgraded false', () => {
    const appDir = mkTmp('muse-renderer-pref-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: pref-app\nversion: 0.0.1\nrenderer: egl\n', 'utf8');
    const r = runCli(['build', appDir, '--print-config']);
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const parsed = JSON.parse(r.stdout.trim());
    assert.ok(parsed.renderer, 'printed output must carry a renderer pair object');
    assert.equal(parsed.renderer.requested, 'egl', 'requested must echo config.renderer verbatim');
    assert.equal(parsed.renderer.effective, 'egl', 'effective must default to requested pre-probe');
    assert.equal(parsed.renderer.downgraded, false, 'pre-probe pair must not be downgraded');
  });

  it('--print-config defaults the pair to cpu when no layer sets renderer', () => {
    const appDir = mkTmp('muse-renderer-pref-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: pref-default\nversion: 0.0.1\n', 'utf8');
    const r = runCli(['build', appDir, '--print-config']);
    assert.equal(r.status, 0, `--print-config must exit 0, got ${r.status}: ${r.stderr}`);
    const parsed = JSON.parse(r.stdout.trim());
    assert.equal(parsed.renderer.requested, 'cpu', 'requested must default to cpu');
    assert.equal(parsed.renderer.effective, 'cpu', 'effective must default to requested pre-probe');
  });
});
