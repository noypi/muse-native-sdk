'use strict';
// T1 — scaffold a new project with one CLI command (NFR-BUILD-005 / AC-BUILD-005
// scaffold half), TC-BUILD-005.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path) only. All
// fixtures live under os.tmpdir(); no network, no Gradle, no device, no
// secrets, no live services.
// Fixture syntax ASSUMPTIONs (recorded in unit-tests/T1.md): no Gradle wrapper
// exists in the repo, so the gate is `node --test
// toolchain/cli/scaffoldSmoke.test.js`; the template is a fake tmpdir
// stand-in (manifest + placeholder bundle input), not a real template
// checkout; DX-CLI owns CLI UX, so no UX surface is asserted here.
// Red state: `packages/cli/lib/init.js` exposes no `scaffoldProject` yet, so
// every T1 case below fails until T1 lands. The T6 composition placeholder
// stays red until batch 3 (toolchain/cli/scaffoldSmoke.js does not exist).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const ENTRY_POINT = require('../../packages/cli/lib/entry-point.js');

function loadImpl() {
  return require('../../packages/cli/lib/init.js');
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

// Minimal fake template stand-in: manifest + placeholder bundle inputs for T2.
function writeTemplate(root) {
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, 'muse-native.yaml'), 'name: fake-app-not-real\n');
  fs.writeFileSync(path.join(root, 'package.json'), '{"name":"fake-app-not-real"}\n');
  fs.mkdirSync(path.join(root, 'src'), { recursive: true });
  fs.writeFileSync(path.join(root, 'src', 'App.tsx'), 'export default function App() {}\n');
  return root;
}

function readIfExists(p) {
  try {
    return fs.readFileSync(p, 'utf8');
  } catch {
    return null;
  }
}

describe('T1 — scaffoldProject contract shape', () => {
  it('returns { ok: true, projectDir } with projectDir equal to outDir', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-shape-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const outDir = path.join(root, 'my-app');
    const result = scaffoldProject({ templateDir, outDir });
    assert.equal(result.ok, true);
    assert.equal(result.projectDir, outDir);
  });

  it('routes through the entry-point registry (create→init)', () => {
    assert.equal(ENTRY_POINT.getEntryPointCommands('create'), 'init');
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-registry-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const outDir = path.join(root, 'my-app');
    const result = scaffoldProject({ templateDir, outDir });
    assert.equal(result.ok, true);
  });
});

describe('T1 — scaffolded dir contains manifest + build inputs for T2', () => {
  it('copies the manifest with identical content', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-manifest-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const outDir = path.join(root, 'my-app');
    const { projectDir } = scaffoldProject({ templateDir, outDir });
    assert.equal(
      readIfExists(path.join(projectDir, 'muse-native.yaml')),
      readIfExists(path.join(templateDir, 'muse-native.yaml'))
    );
  });

  it('copies the placeholder bundle inputs (package.json + src/App.tsx)', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-inputs-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const outDir = path.join(root, 'my-app');
    const { projectDir } = scaffoldProject({ templateDir, outDir });
    assert.equal(
      readIfExists(path.join(projectDir, 'package.json')),
      readIfExists(path.join(templateDir, 'package.json'))
    );
    assert.equal(
      readIfExists(path.join(projectDir, 'src', 'App.tsx')),
      readIfExists(path.join(templateDir, 'src', 'App.tsx'))
    );
  });
});

describe('T1 — failure paths throw scaffold-failed with cleanup', () => {
  it('a missing templateDir throws Error("scaffold-failed: <reason>")', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-no-tpl-');
    assert.throws(
      () => scaffoldProject({ templateDir: path.join(root, 'absent'), outDir: path.join(root, 'app') }),
      (err) => err instanceof Error && /^scaffold-failed: \S/.test(err.message)
    );
  });

  it('a templateDir that is a file (not a directory) throws scaffold-failed', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-file-tpl-');
    const notDir = path.join(root, 'not-a-dir.txt');
    fs.writeFileSync(notDir, 'x');
    assert.throws(
      () => scaffoldProject({ templateDir: notDir, outDir: path.join(root, 'app') }),
      (err) => err instanceof Error && /^scaffold-failed: \S/.test(err.message)
    );
  });

  it('an existing non-empty outDir throws scaffold-failed and is left untouched', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-nonempty-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const outDir = path.join(root, 'taken');
    fs.mkdirSync(outDir, { recursive: true });
    fs.writeFileSync(path.join(outDir, 'keep.txt'), 'do-not-touch\n');
    assert.throws(
      () => scaffoldProject({ templateDir, outDir }),
      (err) => err instanceof Error && /^scaffold-failed: \S/.test(err.message)
    );
    assert.equal(readIfExists(path.join(outDir, 'keep.txt')), 'do-not-touch\n');
    assert.equal(readIfExists(path.join(outDir, 'muse-native.yaml')), null);
  });

  it('a mid-copy failure leaves no partial scaffold behind', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-partial-');
    // A manifest directory where a manifest file is expected forces the copy
    // to fail part-way; no scaffold output file may remain afterwards.
    const templateDir = path.join(root, 'broken-template');
    fs.mkdirSync(path.join(templateDir, 'muse-native.yaml'), { recursive: true });
    fs.writeFileSync(path.join(templateDir, 'package.json'), '{}\n');
    const outDir = path.join(root, 'app');
    assert.throws(
      () => scaffoldProject({ templateDir, outDir }),
      (err) => err instanceof Error && /^scaffold-failed: \S/.test(err.message)
    );
    for (const rel of ['muse-native.yaml', 'package.json', path.join('src', 'App.tsx')]) {
      assert.equal(readIfExists(path.join(outDir, rel)), null, `${rel} must not remain after cleanup`);
    }
  });

  it('non-string arguments are rejected with scaffold-failed (input validation)', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-types-');
    assert.throws(
      () => scaffoldProject({ templateDir: 42, outDir: path.join(root, 'app') }),
      (err) => err instanceof Error && /^scaffold-failed: \S/.test(err.message)
    );
    assert.throws(
      () => scaffoldProject({ templateDir: path.join(root, 't'), outDir: null }),
      (err) => err instanceof Error && /^scaffold-failed: \S/.test(err.message)
    );
  });

  it('failure messages carry a reason and leak no fixture internals', () => {
    const { scaffoldProject } = loadImpl();
    const root = mkTmp('scaffold-msg-');
    let message = '';
    try {
      scaffoldProject({ templateDir: path.join(root, 'absent'), outDir: path.join(root, 'app') });
    } catch (err) {
      message = err.message;
    }
    assert.match(message, /^scaffold-failed: \S.+/);
    assert.doesNotMatch(message, /fake-secret|token|BEGIN PRIVATE KEY/);
  });
});

describe('T6 composition placeholder — T1 scope only (skipped until batch 3)', () => {
  it.skip('runScaffoldSmoke composes scaffold→signed-AAB→mount→symbols naming the failing stage', () => {
    // T2–T4–T6 internals stay out: this case only requires that the composed
    // smoke for T1-scoped inputs exists and reports the T1 stage shape.
    // toolchain/cli/scaffoldSmoke.js lands in batch 3, so this is skipped
    // until then (pending batch 3 — scaffoldSmoke.js lands there).
    const { runScaffoldSmoke } = require('./scaffoldSmoke.js');
    const root = mkTmp('smoke-t1-scope-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const result = runScaffoldSmoke({ templateDir, workDir: root, packName: 'fake-pack-not-real' });
    assert.equal(result.ok, true);
    assert.equal(typeof result.projectDir, 'string');
    assert.ok(fs.existsSync(path.join(result.projectDir, 'muse-native.yaml')));
  });
});
