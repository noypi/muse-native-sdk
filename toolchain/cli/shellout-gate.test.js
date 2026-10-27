'use strict';
// T3 — IDE/CI shell-out gate (NFR-BUILD-003 / AC-BUILD-003).
// Hermetic: node:test + node:assert + stdlib (fs/os/path) only.
// Builds tmp fixture templates under os.tmpdir(); no network, no secrets,
// no live services.
// Fixture syntax ASSUMPTIONs (recorded in unit-tests/T3.md): a clean
// template shells out via `muse-native <command>` (or the `muse` alias,
// optionally prefixed with `npx `) where <command> is served by T2's
// `getEntryPointCommands()` registry; a template is dirty when it invokes
// `gradle`/`gradlew`, `cmake`, or `bundler` directly instead of the CLI.
// Red state: ../shellout-gate.js and ../shellout-policy.json do not exist
// yet, so every case below fails (MODULE_NOT_FOUND / ENOENT) until T3 lands.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const GATE_DIR = __dirname;
const POLICY_FILE = path.join(GATE_DIR, 'shellout-policy.json');
const ENTRY_POINT = require('../../packages/cli/lib/entry-point.js');

function loadImpl() {
  return require('./shellout-gate.js');
}

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function writeTemplate(content, root, rel = 'template.yml') {
  const full = path.join(root, rel);
  fs.mkdirSync(path.dirname(full), { recursive: true });
  fs.writeFileSync(full, content);
  return full;
}

function allowedCommands() {
  return ENTRY_POINT.getEntryPointCommands().map((e) => e.command);
}

describe('T3 — auditShellOut contract shape', () => {
  it('returns { ok: boolean, directLogic: Array<{ file, line, reason }> } on a clean shell-out template', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-clean-');
    const file = writeTemplate('steps:\n  - run: npx muse-native build\n', root);
    const result = auditShellOut([file]);
    assert.equal(typeof result.ok, 'boolean');
    assert.ok(Array.isArray(result.directLogic));
    assert.equal(result.ok, true);
    assert.deepEqual(result.directLogic, []);
  });

  it('an empty template list is clean (ok true, no direct logic)', () => {
    const { auditShellOut } = loadImpl();
    const result = auditShellOut([]);
    assert.equal(result.ok, true);
    assert.deepEqual(result.directLogic, []);
  });
});

describe('T3 — clean shell-out templates pass', () => {
  it('a template invoking the CLI for build passes', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-build-');
    const file = writeTemplate('# CI template\nsteps:\n  - run: muse-native build\n', root);
    const result = auditShellOut([file]);
    assert.equal(result.ok, true);
    assert.deepEqual(result.directLogic, []);
  });

  it('every operation served by T2 getEntryPointCommands() is accepted as a shell-out', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-registry-');
    const commands = allowedCommands();
    assert.ok(commands.length > 0, 'T2 registry must serve at least one command');
    for (const command of commands) {
      const file = writeTemplate(`steps:\n  - run: muse-native ${command}\n`, root, `${command}.yml`);
      const result = auditShellOut([file]);
      assert.equal(result.ok, true, `muse-native ${command} must shell out cleanly`);
      assert.deepEqual(result.directLogic, []);
    }
  });

  it('the muse alias shells out cleanly', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-alias-');
    const file = writeTemplate('steps:\n  - run: muse build\n', root);
    const result = auditShellOut([file]);
    assert.equal(result.ok, true);
    assert.deepEqual(result.directLogic, []);
  });
});

describe('T3 — inlined build logic fails naming file and line', () => {
  it('a fixture with a direct gradle invocation fails naming the file, line, and reason', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-gradle-');
    const rel = 'ci-template.yml';
    const file = writeTemplate('steps:\n  - run: muse-native build\n  - run: ./gradlew assembleRelease\n', root, rel);
    const result = auditShellOut([file]);
    assert.equal(result.ok, false);
    assert.ok(result.directLogic.length > 0, 'must report at least one direct-logic entry');
    const entry = result.directLogic.find((d) => d.file.includes(rel));
    assert.ok(entry, `an entry must name the offending file, got: ${JSON.stringify(result.directLogic)}`);
    assert.equal(entry.line, 3);
    assert.ok(typeof entry.reason === 'string' && entry.reason.length > 0, 'an entry must carry a reason');
    assert.match(entry.reason, /gradle/i);
  });

  it('a direct cmake invocation is reported per file and line', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-cmake-');
    const rel = path.join('ide', 'plugin-shim.js');
    const file = writeTemplate('// IDE plugin shim\nconst cmd = "cmake --build build";\n', root, rel);
    const result = auditShellOut([file]);
    assert.equal(result.ok, false);
    const entry = result.directLogic.find((d) => d.file.includes('plugin-shim.js'));
    assert.ok(entry, `an entry must name the offending file, got: ${JSON.stringify(result.directLogic)}`);
    assert.equal(entry.line, 2);
    assert.match(entry.reason, /cmake/i);
  });

  it('a direct bundler invocation duplicating CLI internals is reported', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-bundler-');
    const rel = 'ci-package.yml';
    const file = writeTemplate('steps:\n  - run: bundler package --release\n', root, rel);
    const result = auditShellOut([file]);
    assert.equal(result.ok, false);
    const entry = result.directLogic.find((d) => d.file.includes(rel));
    assert.ok(entry, `an entry must name the offending file, got: ${JSON.stringify(result.directLogic)}`);
    assert.equal(entry.line, 2);
    assert.ok(entry.reason.length > 0);
  });

  it('a clean file stays clean when a sibling template is dirty', () => {
    const { auditShellOut } = loadImpl();
    const root = mkTmp('shellout-mixed-');
    const clean = writeTemplate('steps:\n  - run: muse-native test\n', root, 'clean.yml');
    const dirtyRel = 'dirty.yml';
    const dirty = writeTemplate('steps:\n  - run: gradle test\n', root, dirtyRel);
    const result = auditShellOut([clean, dirty]);
    assert.equal(result.ok, false);
    assert.ok(result.directLogic.every((d) => !d.file.includes('clean.yml')), 'clean file must not be blamed');
    assert.ok(result.directLogic.some((d) => d.file.includes(dirtyRel)), 'dirty file must be named');
  });
});

describe('T3 — shellout-policy.json pins', () => {
  it('policy enumerates adapter locations and forbidden patterns', () => {
    const policy = JSON.parse(fs.readFileSync(POLICY_FILE, 'utf8'));
    const locations = policy.adapterLocations || policy.adapters || policy.locations;
    assert.ok(Array.isArray(locations) && locations.length > 0, 'policy must list covered adapter locations');
    const joined = JSON.stringify(policy).toLowerCase();
    for (const name of ['ide', 'ci']) {
      assert.ok(joined.includes(name), `policy must cover ${name} adapters, got: ${JSON.stringify(locations)}`);
    }
    const patterns = policy.forbiddenPatterns || policy.forbidden || policy.patterns;
    assert.ok(Array.isArray(patterns) && patterns.length > 0, 'policy must list forbidden patterns');
    const flat = patterns.map(String).join('\n').toLowerCase();
    for (const name of ['gradle', 'cmake', 'bundler']) {
      assert.ok(flat.includes(name), `policy must forbid direct ${name} invocations`);
    }
  });
});
