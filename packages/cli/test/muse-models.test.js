'use strict';
// T3 — muse-native.yaml:muse models/presets validated against the proxy
// allow-list at build time. Hermetic: node:test + node:assert/strict + stdlib
// (fs/os/path) only. Allow-list fixtures are inline (mirroring T1's verified
// IDs) so no project config is read, except the two committed template/content
// file assertions which read committed text files only (no secrets, no env).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

function loadImpl() {
  return require('../lib/muse-models.js');
}

// Inline allow-list mirroring proxy/proxy.config.json (T1 verified IDs only).
function allowList() {
  return {
    version: 1,
    routes: {
      segment: { model: 'sam-3.1', params: { mask_encoding: 'polygon' }, stream: 'none', timeoutMs: 8000, retry: 1 },
      reason: { model: 'muse-spark-1.3', params: { reasoning_effort: 'high' }, stream: 'sse', timeoutMs: 30000, retry: 1 },
      structure: { model: 'muse-spark-1.3', params: { strict: true }, stream: 'sse', timeoutMs: 30000, retry: 1 },
      transcribe: { model: 'muse-voice-transcribe-1.0', params: { mode: 'PUSH_TO_TALK' }, stream: 'sse', timeoutMs: 30000, retry: 1 },
      'generate-image': { model: 'muse-image', params: { prompt: 'x', size: '1024x1024' }, stream: 'progressive', timeoutMs: 60000, retry: 0 },
    },
  };
}

function validYaml() {
  return {
    muse: {
      models: {
        segment: 'sam-3.1',
        reason: 'muse-spark-1.3',
        structure: 'muse-spark-1.3',
        transcribe: 'muse-voice-transcribe-1.0',
        'generate-image': 'muse-image',
      },
      presets: {
        'reason-default': { route: 'reason', params: { reasoning_effort: 'high' } },
        'lens-fast': { route: 'segment', params: { mask_encoding: 'polygon' } },
      },
    },
  };
}

function assertConfigError(fn, expectedKey) {
  let err;
  try {
    fn();
  } catch (e) {
    err = e;
  }
  assert.ok(err, 'expected ConfigError');
  assert.equal(err.file, 'muse-native.yaml');
  assert.equal(typeof err.key, 'string');
  assert.ok(err.key.includes(expectedKey), `key must name '${expectedKey}', got '${err.key}'`);
  assert.equal(typeof err.expected, 'string');
  assert.ok(err.expected.length > 0, 'expected must describe the valid value');
  return err;
}

describe('validateMuseSection', () => {
  it('accepts a fully pinned models+presets section with { ok: true }', () => {
    const { validateMuseSection } = loadImpl();
    assert.deepEqual(validateMuseSection(validYaml(), allowList()), { ok: true });
  });

  it('accepts models-only yaml: absent preset resolves to the route default (no new surface)', () => {
    const { validateMuseSection } = loadImpl();
    const yaml = validYaml();
    delete yaml.muse.presets;
    assert.deepEqual(validateMuseSection(yaml, allowList()), { ok: true });
  });

  it('accepts a missing muse section as { ok: true } (ASSUMPTION: muse: is optional until adopted)', () => {
    const { validateMuseSection } = loadImpl();
    assert.deepEqual(validateMuseSection({}, allowList()), { ok: true });
  });

  it('throws ConfigError on a preset with an unlisted param', () => {
    const { validateMuseSection } = loadImpl();
    const yaml = validYaml();
    yaml.muse.presets['reason-default'].params.evil = 1;
    assertConfigError(() => validateMuseSection(yaml, allowList()), 'evil');
  });

  it('throws ConfigError on style_preset in a preset (assumed param, never allow-listed)', () => {
    const { validateMuseSection } = loadImpl();
    const yaml = validYaml();
    yaml.muse.presets['lens-fast'] = {
      route: 'generate-image',
      params: { prompt: 'x', size: '1024x1024', style_preset: 'foo' },
    };
    assertConfigError(() => validateMuseSection(yaml, allowList()), 'style_preset');
  });

  it('throws ConfigError on a preset naming an unknown route', () => {
    const { validateMuseSection } = loadImpl();
    const yaml = validYaml();
    yaml.muse.presets['bad-route'] = { route: 'speak', params: {} };
    assertConfigError(() => validateMuseSection(yaml, allowList()), 'speak');
  });

  it('throws ConfigError on a non-Muse model in muse.models (gpt-4)', () => {
    const { validateMuseSection } = loadImpl();
    const yaml = validYaml();
    yaml.muse.models.reason = 'gpt-4';
    assertConfigError(() => validateMuseSection(yaml, allowList()), 'gpt-4');
  });

  it('throws ConfigError on a model that disagrees with the allow-list for its route', () => {
    const { validateMuseSection } = loadImpl();
    const yaml = validYaml();
    yaml.muse.models.segment = 'muse-spark-1.3';
    assertConfigError(() => validateMuseSection(yaml, allowList()), 'segment');
  });

  it('throws ConfigError with file muse-native.yaml and a helpful expected string', () => {
    const { validateMuseSection } = loadImpl();
    const yaml = validYaml();
    yaml.muse.models.reason = 'gpt-4';
    const err = assertConfigError(() => validateMuseSection(yaml, allowList()), 'gpt-4');
    assert.equal(err.file, 'muse-native.yaml');
    assert.match(err.expected, /muse-spark-1\.3/);
  });

  it('throws ConfigError (never ok) on a null allowList — fail-closed build validation', () => {
    const { validateMuseSection } = loadImpl();
    let result;
    let err;
    try {
      result = validateMuseSection(validYaml(), null);
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'null allowList must throw, never return ok');
    assert.equal(err.name, 'ConfigError');
    assert.equal(err.file, 'muse-native.yaml');
    assert.ok(result === undefined, 'must never return { ok: true } on a null allowList');
  });

  it('throws ConfigError (never ok) on an undefined allowList — fail-closed build validation', () => {
    const { validateMuseSection } = loadImpl();
    let result;
    let err;
    try {
      result = validateMuseSection(validYaml(), undefined);
    } catch (e) {
      err = e;
    }
    assert.ok(err, 'undefined allowList must throw, never return ok');
    assert.equal(err.name, 'ConfigError');
    assert.equal(err.file, 'muse-native.yaml');
    assert.ok(result === undefined, 'must never return { ok: true } on an undefined allowList');
  });
});

describe('muse template and content carry the muse: section', () => {
  const tmpRoots = [];
  afterEach(() => {
    while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  });

  it('scaffold template packages/cli/templates/muse-native.yaml gains a muse: section', () => {
    const text = fs.readFileSync(path.join(__dirname, '..', 'templates', 'muse-native.yaml'), 'utf8');
    assert.match(text, /^muse:/m, 'template must declare a top-level muse: mapping');
    assert.ok(text.includes('reason-default'), 'template must name the reason-default preset');
    assert.ok(text.includes('muse-spark-1.3'), 'template must pin the verified Spark model ID');
  });

  it('template presets validate cleanly against the allow-list', () => {
    const { validateMuseSection } = loadImpl();
    // Minimal structural parse: the template must at least declare models for
    // every allow-listed route; full YAML parsing is build-layer work.
    const text = fs.readFileSync(path.join(__dirname, '..', 'templates', 'muse-native.yaml'), 'utf8');
    for (const model of ['sam-3.1', 'muse-spark-1.3', 'muse-voice-transcribe-1.0', 'muse-image']) {
      assert.ok(text.includes(model), `template must pin verified model ${model}`);
    }
    assert.deepEqual(validateMuseSection(validYaml(), allowList()), { ok: true });
  });

  it('repo muse-native.yaml declares muse.models + muse.presets (text-level check)', () => {
    const root = path.resolve(__dirname, '..', '..', '..', 'muse-native.yaml');
    const text = fs.readFileSync(root, 'utf8');
    assert.match(text, /^muse:/m, 'root muse-native.yaml must declare muse:');
    assert.ok(text.includes('models:'), 'root file must declare muse.models');
    assert.ok(text.includes('presets:'), 'root file must declare muse.presets');
  });
});

// ---------------------------------------------------------------------------
// BUG-TSX-010 Batch 1 — T1: `muse:` subtree routed out of the flat config
// parser so init output validates. Hermetic: node:test + node:assert/strict +
// stdlib (fs/os/path) only. resolveConfig is driven through its public
// interface against tmp-dir muse-native.yaml files; no env secrets, no
// network, no live services.
// Interface (plan verbatim):
// `resolveConfig(argv: string[], env: Record<string,string>, cwd: string):
// { config: { name: string, version: string }, winningLayer:
// Record<string,string> }` — muse: key no longer throws ConfigError
// unknown-key; scalar validation unchanged.
// ---------------------------------------------------------------------------
describe('BUG-TSX-010 T1 — muse: subtree bypasses flat ConfigError gate', () => {
  const tmpRoots = [];
  afterEach(() => {
    while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  });

  function mkApp(yamlText) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-t1-'));
    tmpRoots.push(dir);
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), yamlText);
    return dir;
  }

  function resolveIn(dir, argv = ['build'], env = {}) {
    const { resolveConfig } = require('../lib/config.js');
    return resolveConfig(argv, env, dir);
  }

  it('init template file (name/version + muse: block) resolves without ConfigError', () => {
    const template = fs.readFileSync(path.join(__dirname, '..', 'templates', 'muse-native.yaml'), 'utf8');
    const dir = mkApp(template);
    const { config, winningLayer } = resolveIn(dir);
    assert.deepEqual(config, { name: 'hello-app', version: '0.0.1' });
    assert.equal(winningLayer.name, 'file');
    assert.equal(winningLayer.version, 'file');
  });

  it('nested muse: lines never leak into the scalar config map', () => {
    const template = fs.readFileSync(path.join(__dirname, '..', 'templates', 'muse-native.yaml'), 'utf8');
    const dir = mkApp(template);
    const { config } = resolveIn(dir);
    assert.deepEqual(Object.keys(config).sort(), ['name', 'version']);
  });

  it('hand-written name/version-only file resolves identically to the template scalars', () => {
    const dir = mkApp('name: hello-app\nversion: 0.0.1\n');
    const { config, winningLayer } = resolveIn(dir);
    assert.deepEqual(config, { name: 'hello-app', version: '0.0.1' });
    assert.deepEqual(winningLayer, { name: 'file', version: 'file' });
  });

  it('unknown scalar key still throws ConfigError fail-closed (never partial)', () => {
    const dir = mkApp('name: hi\nversion: 0.0.1\nevil: 1\n');
    assert.throws(() => resolveIn(dir), (err) => {
      assert.equal(err.name, 'ConfigError');
      assert.equal(err.key, 'evil');
      assert.match(err.message, /unknown key/);
      return true;
    });
  });

  it('wrong scalar type (numeric name:) still throws ConfigError naming the type', () => {
    const dir = mkApp('name: 123\nversion: 0.0.1\n');
    assert.throws(() => resolveIn(dir), (err) => {
      assert.equal(err.name, 'ConfigError');
      assert.equal(err.key, 'name');
      assert.equal(err.expected, 'string');
      return true;
    });
  });

  it('--muse CLI flag stays rejected: unknown key (structured section, not a scalar key)', () => {
    const dir = mkApp('name: hi\nversion: 0.0.1\n');
    assert.throws(() => resolveIn(dir, ['build', '--muse', 'x']), (err) => {
      assert.equal(err.name, 'ConfigError');
      assert.equal(err.key, 'muse');
      return true;
    });
  });

  it('MUSE_NATIVE_MUSE env stays ignored: never becomes a config key (ASSUMPTION: env layer is scalar-only)', () => {
    const dir = mkApp('name: hi\nversion: 0.0.1\n');
    const { config } = resolveIn(dir, ['build'], { MUSE_NATIVE_MUSE: 'x' });
    assert.deepEqual(config, { name: 'hi', version: '0.0.1' });
  });
});
