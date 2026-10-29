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

  // FR-PACKAGING-006 Stage 2 §6: specified `engineLibs` default map
  // (per-ABI `engine/out/<abi>/libRuntime.so`, same root as
  // `defaultEngineLibs()` in lib/config.js — read from resolveConfig
  // behavior, not invented).
  function expectedEngineLibs() {
    const out = {};
    for (const abi of ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86']) {
      out[abi] = path.join(__dirname, '..', '..', '..', 'engine', 'out', abi, 'libRuntime.so');
    }
    return out;
  }

  it('init template file (name/version + muse: block) resolves without ConfigError', () => {
    const template = fs.readFileSync(path.join(__dirname, '..', 'templates', 'muse-native.yaml'), 'utf8');
    const dir = mkApp(template);
    const { config, winningLayer } = resolveIn(dir);
    assert.deepEqual(config, { name: 'hello-app', version: '0.0.1', withSample: false, abi: undefined, release: false, modules: undefined, output: undefined, strict: false, diagnostics: undefined, archs: ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'], packaging: 'fat', keystore: path.join(__dirname, '..', 'templates', 'android', 'debug.keystore'), keyAlias: 'androiddebugkey', storepass: undefined, engineLibs: expectedEngineLibs() });
    assert.equal(winningLayer.name, 'file');
    assert.equal(winningLayer.version, 'file');
  });

  it('nested muse: lines never leak into the scalar config map', () => {
    const template = fs.readFileSync(path.join(__dirname, '..', 'templates', 'muse-native.yaml'), 'utf8');
    const dir = mkApp(template);
    const { config } = resolveIn(dir);
    assert.deepEqual(Object.keys(config).sort(), ['abi', 'archs', 'diagnostics', 'engineLibs', 'keyAlias', 'keystore', 'modules', 'name', 'output', 'packaging', 'release', 'storepass', 'strict', 'version', 'withSample']);
  });

  it('hand-written name/version-only file resolves identically to the template scalars', () => {
    const dir = mkApp('name: hello-app\nversion: 0.0.1\n');
    const { config, winningLayer } = resolveIn(dir);
    assert.deepEqual(config, { name: 'hello-app', version: '0.0.1', withSample: false, abi: undefined, release: false, modules: undefined, output: undefined, strict: false, diagnostics: undefined, archs: ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'], packaging: 'fat', keystore: path.join(__dirname, '..', 'templates', 'android', 'debug.keystore'), keyAlias: 'androiddebugkey', storepass: undefined, engineLibs: expectedEngineLibs() });
    assert.deepEqual(winningLayer, { name: 'file', version: 'file', withSample: 'default', abi: 'default', release: 'default', modules: 'default', output: 'default', strict: 'default', diagnostics: 'default', archs: 'default', packaging: 'default', keystore: 'default', keyAlias: 'default', storepass: 'default', engineLibs: 'default' });
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
    assert.deepEqual(config, { name: 'hi', version: '0.0.1', withSample: false, abi: undefined, release: false, modules: undefined, output: undefined, strict: false, diagnostics: undefined, archs: ['arm64-v8a', 'armeabi-v7a', 'x86_64', 'x86'], packaging: 'fat', keystore: path.join(__dirname, '..', 'templates', 'android', 'debug.keystore'), keyAlias: 'androiddebugkey', storepass: undefined, engineLibs: expectedEngineLibs() });
  });
});

// ---------------------------------------------------------------------------
// BUG-TSX-010 Batch 2 — T3 seam cases (TC-TSX-022): the T1-parsed template
// `muse:` subtree (not a hand-built object) validates `{ ok: true }`
// against the inline allow-list; unknown route / wrong model / unlisted
// param throw `ConfigError` (existing contract, now wired).
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only. The
// template text is read from the committed template file and parsed with a
// minimal in-test indentation-aware parser, so the parser routing (nested
// `muse:` lines must not leak as scalar keys) is covered; fixtures use only
// dummy IDs, no secrets, no network, no live services.
// Interface (plan verbatim): `node --test
// packages/cli/test/muse-models.test.js` — seam cases: template-file
// `muse:` subtree validates `{ ok: true }` against the inline allow-list;
// unknown route / wrong model / unlisted param throw `ConfigError`
// (existing contract, now wired).
// ---------------------------------------------------------------------------
describe('BUG-TSX-010 T3 — template-file muse: subtree seam (TC-TSX-022)', () => {
  // Minimal indentation-aware parse of the top-level `muse:` subtree out of
  // raw yaml text: mirrors the T1 routing (nested lines must not leak as
  // scalar keys) without adding a YAML dependency. Returns `{ muse }`.
  function parseMuseSubtree(text) {
    const lines = text.split('\n');
    const start = lines.findIndex((l) => /^muse:\s*$/.test(l));
    assert.ok(start >= 0, 'fixture text must declare a top-level muse: block');
    const muse = {};
    const stack = [{ indent: -1, obj: muse }];
    for (const raw of lines.slice(start + 1)) {
      if (/^\s*(#|$)/.test(raw)) continue; // comment or blank
      const indent = raw.match(/^\s*/)[0].length;
      if (indent === 0) break; // next top-level key: muse: subtree ends
      const m = raw.match(/^\s*([^:\s][^:]*):\s*(.*?)\s*$/);
      assert.ok(m, `fixture line must be key: value, got ${JSON.stringify(raw)}`);
      while (stack[stack.length - 1].indent >= indent) stack.pop();
      const parent = stack[stack.length - 1].obj;
      const key = m[1].trim();
      const value = m[2];
      if (value === '' || value === '{}') {
        parent[key] = value === '{}' ? {} : {};
        if (value === '') stack.push({ indent, obj: parent[key] });
      } else {
        parent[key] = value;
      }
    }
    return { muse };
  }

  function templateText() {
    return fs.readFileSync(path.join(__dirname, '..', 'templates', 'muse-native.yaml'), 'utf8');
  }

  it('template-file muse: subtree validates { ok: true } against the inline allow-list', () => {
    const { validateMuseSection } = loadImpl();
    const parsed = parseMuseSubtree(templateText());
    assert.ok(parsed.muse.models, 'parsed subtree must carry models');
    assert.ok(parsed.muse.presets, 'parsed subtree must carry presets');
    assert.deepEqual(validateMuseSection(parsed, allowList()), { ok: true });
  });

  it('parsed template subtree with an unknown route throws ConfigError naming the route', () => {
    const { validateMuseSection } = loadImpl();
    const text = templateText().replace('  models:\n', '  models:\n    speak: gpt-4\n');
    assertConfigError(() => validateMuseSection(parseMuseSubtree(text), allowList()), 'speak');
  });

  it('parsed template subtree with a wrong model throws ConfigError naming the model', () => {
    const { validateMuseSection } = loadImpl();
    const text = templateText().replace('    reason: muse-spark-1.3', '    reason: gpt-4');
    assertConfigError(() => validateMuseSection(parseMuseSubtree(text), allowList()), 'gpt-4');
  });

  it('parsed template subtree with an unlisted preset param throws ConfigError naming the param', () => {
    const { validateMuseSection } = loadImpl();
    const text = templateText().replace(
      '        reasoning_effort: high',
      '        reasoning_effort: high\n        evil: 1',
    );
    assertConfigError(() => validateMuseSection(parseMuseSubtree(text), allowList()), 'evil');
  });
});

// ---------------------------------------------------------------------------
// BUG-TSX-010 security hardening (security-review.md "Security Tests
// Requested"): production `parseMuseSubtree` (packages/cli/lib/config.js)
// must reject dangerous keys fail-closed with ConfigError naming the key.
// Hermetic: node:test + node:assert/strict only; no fs, no env, no network.
// ---------------------------------------------------------------------------
describe('BUG-TSX-010 security — parseMuseSubtree rejects dangerous keys', () => {
  function parseProd(text) {
    const { parseMuseSubtree } = require('../lib/config.js');
    return parseMuseSubtree(text);
  }

  it("parseMuseSubtree('muse:\\n  __proto__:\\n    x: 1') throws ConfigError with key __proto__", () => {
    assert.throws(() => parseProd('muse:\n  __proto__:\n    x: 1'), (err) => {
      assert.equal(err.name, 'ConfigError');
      assert.equal(err.key, '__proto__');
      return true;
    });
  });

  it("parseMuseSubtree('muse:\\n  constructor:\\n    x: 1') throws ConfigError with key constructor", () => {
    assert.throws(() => parseProd('muse:\n  constructor:\n    x: 1'), (err) => {
      assert.equal(err.name, 'ConfigError');
      assert.equal(err.key, 'constructor');
      return true;
    });
  });

  it("parseMuseSubtree('muse:\\n  prototype:\\n    x: 1') throws ConfigError with key prototype", () => {
    assert.throws(() => parseProd('muse:\n  prototype:\n    x: 1'), (err) => {
      assert.equal(err.name, 'ConfigError');
      assert.equal(err.key, 'prototype');
      return true;
    });
  });
});
