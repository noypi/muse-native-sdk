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

// T6 composition entry point (toolchain/cli/scaffoldSmoke.js lands in batch 3).
function loadImplT6() {
  return require('./scaffoldSmoke.js');
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

// Minimal fake template stand-in: the fixed nine-member TEMPLATE_FILES set
// from packages/cli/lib/init.js:11-21 (read-only reference — DO NOT modify
// init.js). Placeholder content suffices per plan A5: scaffoldProject copies
// bytes verbatim except the single `APP_NAME =` Makefile line rewrite, so the
// smoke composition asserts existence, not template content. (The pre-T3
// 3-file fixture had no Makefile and hence no APP_NAME handling to mirror —
// the placeholder `APP_NAME =` line below exists only so the rewrite has a
// line to target.)
function writeTemplate(root) {
  fs.mkdirSync(root, { recursive: true });
  fs.writeFileSync(path.join(root, 'muse-native.yaml'), 'name: fake-app-not-real\n');
  fs.writeFileSync(path.join(root, 'package.json'), '{"name":"fake-app-not-real"}\n');
  fs.writeFileSync(path.join(root, 'tsconfig.json'), '{}\n');
  fs.writeFileSync(path.join(root, 'README.md'), '# fake-app-not-real\n');
  fs.writeFileSync(path.join(root, 'Makefile'), 'APP_NAME = fake-app-not-real\n');
  fs.writeFileSync(path.join(root, 'AndroidManifest.xml'), '<manifest/>\n');
  fs.mkdirSync(path.join(root, 'src'), { recursive: true });
  fs.writeFileSync(path.join(root, 'src', 'App.tsx'), 'export default function App() {}\n');
  fs.mkdirSync(path.join(root, 'proxy'), { recursive: true });
  fs.writeFileSync(path.join(root, 'proxy', 'proxy.config.json'), '{}\n');
  fs.mkdirSync(path.join(root, 'res', 'values'), { recursive: true });
  fs.writeFileSync(path.join(root, 'res', 'values', 'strings.xml'), '<resources></resources>\n');
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

describe('T6 composition — scaffold→signed-AAB→mount→symbols (batch 3)', () => {
  it('runScaffoldSmoke composes scaffold→signed-AAB→mount→symbols naming the failing stage', () => {
    // Batch-3 legitimate test update (GR-08 T1.13): the batch-1 `it.skip`
    // placeholder is un-skipped into the real TC-BUILD-005 composition case.
    // All 10 T1 cases above are untouched. Thin composition over T1–T4: no
    // logic is reimplemented here, only the composed observable outcome.
    const { runScaffoldSmoke } = require('./scaffoldSmoke.js');
    const root = mkTmp('smoke-full-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const workDir = path.join(root, 'work');
    fs.mkdirSync(workDir, { recursive: true });
    const result = runScaffoldSmoke({ templateDir, workDir, packName: 'fake-pack-not-real' });
    assert.equal(result.ok, true);
    assert.equal(typeof result.projectDir, 'string');
    assert.ok(fs.existsSync(path.join(result.projectDir, 'muse-native.yaml')), 'scaffolded manifest must exist');
    assert.equal(typeof result.aabPath, 'string');
    assert.ok(fs.existsSync(result.aabPath), `signed AAB must exist: ${result.aabPath}`);
    assert.equal(typeof result.mountPoint, 'string');
    assert.ok(fs.existsSync(result.mountPoint), `mount point must exist: ${result.mountPoint}`);
    assert.ok(Array.isArray(result.symbols), 'symbols must be an array');
    assert.ok(result.symbols.length > 0, 'symbols must be non-empty');
  });

  it('a scaffold-stage failure throws smoke-failed naming the scaffold stage', () => {
    const { runScaffoldSmoke } = loadImplT6();
    const root = mkTmp('smoke-bad-scaffold-');
    const workDir = path.join(root, 'work');
    fs.mkdirSync(workDir, { recursive: true });
    assert.throws(
      () => runScaffoldSmoke({ templateDir: path.join(root, 'absent'), workDir, packName: 'fake-pack-not-real' }),
      (err) => err instanceof Error && /^smoke-failed: scaffold: \S/.test(err.message)
    );
  });

  it('a delivery-stage failure throws smoke-failed naming the delivery stage', () => {
    const { runScaffoldSmoke } = loadImplT6();
    const root = mkTmp('smoke-bad-pack-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const workDir = path.join(root, 'work');
    fs.mkdirSync(workDir, { recursive: true });
    assert.throws(
      () => runScaffoldSmoke({ templateDir, workDir, packName: '' }),
      (err) => err instanceof Error && /^smoke-failed: delivery: \S/.test(err.message)
    );
  });

  it('failure messages carry stage + reason and leak no fixture internals', () => {
    const { runScaffoldSmoke } = loadImplT6();
    const root = mkTmp('smoke-msg-');
    const workDir = path.join(root, 'work');
    fs.mkdirSync(workDir, { recursive: true });
    let message = '';
    try {
      runScaffoldSmoke({ templateDir: path.join(root, 'absent'), workDir, packName: 'fake-pack-not-real' });
    } catch (err) {
      message = err.message;
    }
    assert.match(message, /^smoke-failed: \S+: \S.+/);
    assert.doesNotMatch(message, /fake-secret|token|BEGIN PRIVATE KEY/);
  });
});

describe('BUG-BUILD-002 T2/T3 — delivery disposal in runScaffoldSmoke', () => {
  function countPlayPackDirs() {
    return fs.readdirSync(os.tmpdir()).filter((n) => n.startsWith('play-pack-')).length;
  }

  it('T2: a retry-name delivery failure releases the handle and still throws smoke-failed: delivery:', () => {
    const { runScaffoldSmoke } = loadImplT6();
    const root = mkTmp('smoke-disposal-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const workDir = path.join(root, 'work');
    fs.mkdirSync(workDir, { recursive: true });
    const before = countPlayPackDirs();
    let message = '';
    try {
      // ASSUMPTION (plan A1/A2): first request for a name containing `retry`
      // ends `failed`, so the delivery stage throws and must release the
      // acquired handle best-effort before smokeFailed('delivery', err).
      runScaffoldSmoke({ templateDir, workDir, packName: 'fake-pack-retry-not-real' });
    } catch (err) {
      message = err.message;
    }
    assert.match(message, /^smoke-failed: delivery: \S.+/, 'must throw single-prefixed delivery error');
    assert.equal((message.match(/smoke-failed/g) || []).length, 1, 'single stage prefix only');
    const after = countPlayPackDirs();
    assert.equal(after, before, `failed delivery must leave zero net new play-pack-* dirs (before=${before} after=${after})`);
  });

  it('T2: success path keeps the returned mountPoint live and caller-owned', () => {
    const { runScaffoldSmoke } = loadImplT6();
    const root = mkTmp('smoke-disposal-ok-');
    const templateDir = writeTemplate(path.join(root, 'template'));
    const workDir = path.join(root, 'work');
    fs.mkdirSync(workDir, { recursive: true });
    const result = runScaffoldSmoke({ templateDir, workDir, packName: 'fake-pack-not-real' });
    assert.equal(result.ok, true);
    assert.equal(typeof result.mountPoint, 'string');
    assert.ok(fs.existsSync(result.mountPoint), `success mountPoint must remain live: ${result.mountPoint}`);
    assert.ok(fs.statSync(result.mountPoint).isDirectory(), 'mountPoint must be a directory');
  });

  it('T3: repeated failing deliveries do not accumulate play-pack-* dirs (single instance, no cache clear)', () => {
    const { runScaffoldSmoke } = loadImplT6();
    // No require.cache clear mid-test: accumulation must be exercised within
    // this single module instance across N calls.
    const before = countPlayPackDirs();
    const N = 5;
    for (let i = 0; i < N; i += 1) {
      const root = mkTmp(`smoke-disposal-n-${i}-`);
      const templateDir = writeTemplate(path.join(root, 'template'));
      const workDir = path.join(root, 'work');
      fs.mkdirSync(workDir, { recursive: true });
      // Unique retry-fixture name per iteration so every call takes the
      // failing-first-attempt path (counters are per name, kept per A2).
      assert.throws(
        () => runScaffoldSmoke({ templateDir, workDir, packName: `fake-pack-retry-accumulate-${i}-not-real` }),
        (err) => err instanceof Error && /^smoke-failed: delivery: \S/.test(err.message)
      );
    }
    const after = countPlayPackDirs();
    assert.equal(after, before, `expected zero net new play-pack-* dirs after ${N} failed deliveries (before=${before} after=${after})`);
  });
});
