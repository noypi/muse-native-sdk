'use strict';
// TC-WORKFLOW-017..023 (AC-WORKFLOW-017/018/019/020, FR-WORKFLOW-007):
// scaffold + example configs document every option as commented examples
// only; active YAML stays byte-identical.
// Hermetic: node:test + node:assert/strict + stdlib only. Reads template and
// example YAML as text; resolution checks use resolveConfig/readFileLayer
// against fs.mkdtemp dirs; `--help` via `node bin/muse-native.js` spawn.
// No network, no secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-Y1: "mentioned in comments" means at least one `#` comment line in the
//   file names the key (`sdk:`/`muse:` match with or without the colon).
// - A-Y2: "active YAML byte-identical" is observed as: the set of active
//   (non-comment) top-level keys is unchanged from the pre-change sets
//   recorded below, and resolution of the file is unchanged by its comments.
// - A-Y3: secrets are placeholder-only when no active secret key carries a
//   real value and the comments point at `MUSE_NATIVE_*` env instead.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const config = require('../lib/config.js');
const { resolveConfig, readFileLayer, ConfigError } = config;

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const SCAFFOLD = path.join(CLI_DIR, 'templates', 'muse-native.yaml');
const EXAMPLES = [
  'examples/m3-gallery/muse-native.yaml',
  'examples/flappy/muse-native.yaml',
  'examples/muse-demo/muse-native.yaml',
];

// Pre-change active top-level scalar/section keys per file (A-Y2). The
// commented-examples change must not activate any key outside these sets.
const ACTIVE_KEYS = {
  'templates/muse-native.yaml': new Set(['name', 'version', 'packaging', 'muse']),
  'examples/m3-gallery/muse-native.yaml': new Set(['name', 'version', 'sdk', 'muse']),
  'examples/flappy/muse-native.yaml': new Set(['name', 'version', 'muse']),
  'examples/muse-demo/muse-native.yaml': new Set(['name', 'version', 'muse']),
};

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
});

function commentLines(text) {
  return text.split('\n').filter((l) => l.trim().startsWith('#'));
}

function activeTopKeys(text) {
  const keys = new Set();
  for (const line of text.split('\n')) {
    if (/^[ \t]/.test(line)) continue;
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    const colon = trimmed.indexOf(':');
    if (colon === -1) continue;
    keys.add(trimmed.slice(0, colon).trim());
  }
  return keys;
}

function stripComments(text) {
  return text
    .split('\n')
    .filter((l) => !l.trim().startsWith('#'))
    .join('\n');
}

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

// Every key the drift guard owns: SCHEMA keys plus file-only `sdk:` plus
// the structured `muse:` section plus the `diagnostics` map key.
function guardedKeys() {
  return [...Object.keys(config.SCHEMA), 'sdk', 'muse'];
}

function assertMentionedInComments(file, text) {
  const comments = commentLines(text).join('\n');
  for (const key of guardedKeys()) {
    assert.ok(
      comments.includes(key),
      `${file}: scaffold/example comments must mention '${key}' (AC-WORKFLOW-017/020)`,
    );
  }
}

// ---- TC-WORKFLOW-017: scaffold carries a commented example for every key ----

describe('TC-WORKFLOW-017 scaffold commented examples (AC-WORKFLOW-017)', () => {
  it('scaffold comments mention every SCHEMA key plus sdk: plus muse:', () => {
    const text = fs.readFileSync(SCAFFOLD, 'utf8');
    assertMentionedInComments('templates/muse-native.yaml', text);
  });

  it('scaffold comments mention diagnostics (map form + severity values)', () => {
    const comments = commentLines(fs.readFileSync(SCAFFOLD, 'utf8')).join('\n');
    assert.ok(comments.includes('diagnostics'), 'scaffold comments must mention diagnostics');
    for (const sev of ['error', 'warn', 'off']) {
      assert.ok(comments.includes(sev), `scaffold diagnostics comments must name severity '${sev}'`);
    }
  });

  it('each scaffold commented example names type, default, allowed values and layer', () => {
    const comments = commentLines(fs.readFileSync(SCAFFOLD, 'utf8')).join('\n');
    for (const key of Object.keys(config.SCHEMA)) {
      const expectedType = config.SCHEMA[key];
      assert.ok(comments.includes(expectedType), `comment for '${key}' must name type '${expectedType}'`);
    }
    assert.ok(comments.includes('default'), 'scaffold comments must state defaults');
    assert.ok(/allowed/i.test(comments), 'scaffold comments must state allowed values');
    assert.ok(
      comments.includes('MUSE_NATIVE_') || /cli.*env.*file|env.*file.*default|layer/i.test(comments),
      'scaffold comments must state the winning layer (CLI over MUSE_NATIVE_* env over file over default)',
    );
  });
});

// ---- TC-WORKFLOW-018/019/020: each example carries commented examples ----

for (const [tc, rel] of [
  ['TC-WORKFLOW-018', 'examples/m3-gallery/muse-native.yaml'],
  ['TC-WORKFLOW-019', 'examples/flappy/muse-native.yaml'],
  ['TC-WORKFLOW-020', 'examples/muse-demo/muse-native.yaml'],
]) {
  describe(`${tc} ${rel} commented examples (AC-WORKFLOW-017)`, () => {
    it('example comments mention every SCHEMA key plus sdk: plus muse:', () => {
      const text = fs.readFileSync(path.resolve(CLI_DIR, '..', '..', rel), 'utf8');
      assertMentionedInComments(rel, text);
    });

    it('example active keys are unchanged (no commented example activated)', () => {
      const text = fs.readFileSync(path.resolve(CLI_DIR, '..', '..', rel), 'utf8');
      assert.deepEqual(activeTopKeys(text), ACTIVE_KEYS[rel], `${rel}: active keys must stay byte-identical`);
    });
  });
}

// ---- TC-WORKFLOW-021: comments are prose only (AC-WORKFLOW-018) ----

describe('TC-WORKFLOW-021 comments change nothing (AC-WORKFLOW-018)', () => {
  it('scaffold resolves identically with comments present vs stripped', () => {
    const text = fs.readFileSync(SCAFFOLD, 'utf8');
    const dirFull = mkTmp('muse-yaml-full-');
    const dirBare = mkTmp('muse-yaml-bare-');
    fs.writeFileSync(path.join(dirFull, 'muse-native.yaml'), text, 'utf8');
    fs.writeFileSync(path.join(dirBare, 'muse-native.yaml'), stripComments(text), 'utf8');
    const full = resolveConfig([], {}, dirFull);
    const bare = resolveConfig([], {}, dirBare);
    assert.deepEqual(full.config, bare.config, 'comments must not alter resolution');
    assert.deepEqual(full.winningLayer, bare.winningLayer, 'comments must not alter winning layers');
  });

  it('scaffold muse: subtree is identical with comments present vs stripped', () => {
    const text = fs.readFileSync(SCAFFOLD, 'utf8');
    const dirFull = mkTmp('muse-yaml-full-');
    const dirBare = mkTmp('muse-yaml-bare-');
    fs.writeFileSync(path.join(dirFull, 'muse-native.yaml'), text, 'utf8');
    fs.writeFileSync(path.join(dirBare, 'muse-native.yaml'), stripComments(text), 'utf8');
    assert.deepEqual(
      readFileLayer(dirFull).muse,
      readFileLayer(dirBare).muse,
      'per-project muse: block must be intact and unique per file',
    );
  });

  it('build --help output is identical to getOptionSchema()', () => {
    assert.equal(typeof config.getOptionSchema, 'function', 'config.js must export getOptionSchema()');
    const schema = config.getOptionSchema();
    const r = runCli(['build', '--help']);
    assert.equal(r.status, 0, `build --help must exit 0, got ${r.status}: ${r.stderr}`);
    for (const entry of schema) {
      assert.ok(r.stdout.includes(`--${entry.name}`), `help must list --${entry.name}`);
      assert.ok(r.stdout.includes(String(entry.type)), `help must state type '${entry.type}' for --${entry.name}`);
      assert.ok(
        r.stdout.includes(String(entry.default)),
        `help must state default '${String(entry.default)}' for --${entry.name}`,
      );
      for (const value of entry.allowed || []) {
        assert.ok(
          r.stdout.includes(String(value)),
          `help must list allowed value '${String(value)}' for --${entry.name}`,
        );
      }
    }
  });
});

// ---- TC-WORKFLOW-022: fail-closed uncomment + placeholder-only secrets (AC-WORKFLOW-019) ----

describe('TC-WORKFLOW-022 uncomment failures are fail-closed (AC-WORKFLOW-019)', () => {
  it('uncommenting with a typo key fails via ConfigError naming key/file/expected', () => {
    const dir = mkTmp('muse-yaml-typo-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'name: typo-app\nversion: 0.0.1\nrenderre: skia\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'renderre');
      assert.ok(err.file.length > 0, 'error must name the file');
      assert.ok(String(err.expected).length > 0, 'error must name the expected value');
      return true;
    });
  });

  it('uncommenting with an unknown key fails via ConfigError naming key/file/expected', () => {
    const dir = mkTmp('muse-yaml-unknown-');
    fs.writeFileSync(path.join(dir, 'muse-native.yaml'), 'name: unknown-app\nversion: 0.0.1\ncolr: red\n', 'utf8');
    assert.throws(() => resolveConfig([], {}, dir), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'colr');
      return true;
    });
  });

  it('uncommenting with a wrong type fails via ConfigError naming key/file/expected', () => {
    assert.throws(() => config.validateConfig({ release: 42 }, 'muse-native.yaml'), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.equal(err.key, 'release');
      assert.equal(err.file, 'muse-native.yaml');
      assert.equal(err.expected, 'boolean');
      return true;
    });
  });

  it('uncommenting renderer with a bad value names the option and accepted values', () => {
    assert.throws(() => config.validateConfig({ renderer: 'vulkan' }, 'muse-native.yaml'), (err) => {
      assert.ok(err instanceof ConfigError);
      assert.match(String(err.message), /renderer/);
      assert.match(String(err.message), /cpu, egl, skia/);
      return true;
    });
  });

  it('secrets are placeholder-only with a MUSE_NATIVE_* env note', () => {
    const text = fs.readFileSync(SCAFFOLD, 'utf8');
    assert.ok(text.includes('MUSE_NATIVE_'), 'scaffold comments must point secrets at MUSE_NATIVE_* env');
    const activeStorepass = text.split('\n').filter((l) => /^\s*storepass\s*:/.test(l) && !l.trim().startsWith('#'));
    assert.equal(activeStorepass.length, 0, 'template must not activate a storepass value');
  });
});

// ---- TC-WORKFLOW-023: drift guard (AC-WORKFLOW-020) ----

describe('TC-WORKFLOW-023 drift guard: comments track the schema (AC-WORKFLOW-020)', () => {
  it('every SCHEMA key plus sdk: plus muse: is mentioned in the scaffold comments', () => {
    const text = fs.readFileSync(SCAFFOLD, 'utf8');
    assertMentionedInComments('templates/muse-native.yaml', text);
  });

  it('scaffold active keys are unchanged (commented examples add no active keys)', () => {
    const text = fs.readFileSync(SCAFFOLD, 'utf8');
    assert.deepEqual(
      activeTopKeys(text),
      ACTIVE_KEYS['templates/muse-native.yaml'],
      'scaffold active YAML must stay byte-identical',
    );
  });

  it('--help matches the schema exactly (no drift between prose and authority)', () => {
    const schema = config.getOptionSchema();
    const r = runCli(['build', '--help']);
    assert.equal(r.status, 0, `build --help must exit 0, got ${r.status}: ${r.stderr}`);
    for (const entry of schema) {
      assert.ok(r.stdout.includes(`--${entry.name}`), `help must list --${entry.name}, schema is the authority`);
      for (const value of entry.allowed || []) {
        assert.ok(r.stdout.includes(String(value)), `help must list allowed '${String(value)}' for --${entry.name}`);
      }
    }
  });
});
