'use strict';
// TC-TSX-022 (T6 init, T7 build) + TC-TSX-023 (T7 artefacts) — Batch 3 of FR-TSX-008.
// Hermetic: node:test + node:assert + stdlib (fs/os/path/child_process) only.
// Spawns `node bin/muse-native.js` against fs.mkdtemp dirs; no network,
// no secrets, no live services, no examples/gallery dependency (T10 owns it;
// Batch 4 adds the gallery test — here T7 uses a PRE-WRITTEN inline minimal
// 2-screen tmp fixture).
// Batch 4 (T8, T9, T10) appends NEW describe blocks below the marker at the
// end of this file. Do not edit the Batch-3 blocks above the marker.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');

const tmpRoots = [];
function mkTmp(prefix) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), prefix));
  tmpRoots.push(dir);
  return dir;
}
afterEach(() => {
  while (tmpRoots.length) {
    const dir = tmpRoots.pop();
    fs.rmSync(dir, { recursive: true, force: true });
  }
});

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

// Pre-written inline minimal app (2 screens) for T7: written into a tmp dir
// because examples/gallery (T10) does not exist yet.
function writeMinimalApp(appDir) {
  fs.mkdirSync(path.join(appDir, 'src', 'screens'), { recursive: true });
  fs.writeFileSync(
    path.join(appDir, 'muse-native.yaml'),
    'name: minimal-fixture\nversion: 0.0.0\n',
  );
  fs.writeFileSync(
    path.join(appDir, 'src', 'App.tsx'),
    [
      "import { View, Text } from '@muse/native';",
      "import { state } from '@muse/core';",
      'import { HomeScreen } from "./screens/HomeScreen";',
      'export default function App() {',
      '  const title = state("hi");',
      '  return (<View><Text>{title.value}</Text><HomeScreen /></View>);',
      '}',
      '',
    ].join('\n'),
  );
  fs.writeFileSync(
    path.join(appDir, 'src', 'screens', 'HomeScreen.tsx'),
    [
      "import { View, Text } from '@muse/native';",
      'export default function HomeScreen() {',
      '  return (<View><Text>home</Text></View>);',
      '}',
      '',
    ].join('\n'),
  );
}

// ---------------------------------------------------------------------------
// T6 — `muse-native init` scaffolds a project.
// Interface: `muse-native init <dir>` → creates `<dir>/` with template
// files, stdout names the dir, exit 0; existing non-empty `<dir>` → stderr
// error + exit 1; missing `<dir>` → usage + exit 2.
// ---------------------------------------------------------------------------
describe('TC-TSX-022 Batch 3 — T6 init scaffolds a project', () => {
  it('init into a fresh dir creates the template tree, names the dir on stdout, exit 0', () => {
    const parent = mkTmp('muse-init-');
    const dir = path.join(parent, 'hello-app');
    const r = runCli(['init', dir]);
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(r.stdout.includes(dir) || r.stdout.includes('hello-app'), 'stdout must name the dir');
    for (const rel of ['muse-native.yaml', 'src/App.tsx', 'package.json']) {
      assert.ok(fs.statSync(path.join(dir, rel)).isFile(), `${rel} must be created`);
    }
  });

  it('init into an existing empty dir succeeds with exit 0', () => {
    const parent = mkTmp('muse-init-');
    const dir = path.join(parent, 'empty-app');
    fs.mkdirSync(dir);
    const r = runCli(['init', dir]);
    assert.equal(r.status, 0, `init into an empty dir must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(fs.statSync(path.join(dir, 'src', 'App.tsx')).isFile(), 'src/App.tsx must be created');
  });

  it('template src/App.tsx imports from @muse/native + @muse/core only', () => {
    const parent = mkTmp('muse-init-');
    const dir = path.join(parent, 'import-app');
    const r = runCli(['init', dir]);
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    const text = fs.readFileSync(path.join(dir, 'src', 'App.tsx'), 'utf8');
    const specifiers = [...text.matchAll(/from\s+['"]([^'"]+)['"]/g)].map((m) => m[1]);
    assert.ok(specifiers.length > 0, 'App.tsx must have at least one import');
    for (const spec of specifiers) {
      if (spec.startsWith('.')) continue; // relative imports within the template are fine
      assert.ok(
        spec === '@muse/native' || spec.startsWith('@muse/native/') || spec === '@muse/core' || spec.startsWith('@muse/core/'),
        `App.tsx must import from @muse/native + @muse/core only, got '${spec}'`,
      );
    }
  });

  it('re-init into a non-empty dir exits 1 with an error on stderr and preserves contents', () => {
    const parent = mkTmp('muse-init-');
    const dir = path.join(parent, 'taken-app');
    fs.mkdirSync(dir);
    fs.writeFileSync(path.join(dir, 'keep.txt'), 'do not clobber');
    const r = runCli(['init', dir]);
    assert.equal(r.status, 1, `init into non-empty dir must exit 1, got ${r.status}`);
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
    assert.equal(fs.readFileSync(path.join(dir, 'keep.txt'), 'utf8'), 'do not clobber', 'existing contents must be preserved');
  });

  it('init with a missing <dir> prints usage and exits 2', () => {
    const r = runCli(['init']);
    assert.equal(r.status, 2, `init with no dir must exit 2, got ${r.status}`);
    assert.ok(/usage/i.test(r.stderr), 'must print usage to stderr');
  });
});

// ---------------------------------------------------------------------------
// T7 — `muse-native build` emits UI IR + binary UI tree.
// Interface: `muse-native build <appDir>` → writes `<appDir>/dist/ir.json`
// (syntax-neutral IR: `{ version: 1, roots: [...] }` with static-schema
// nodes + dynamic value slots) and `<appDir>/dist/tree.mui.bin`
// (length-prefixed binary encoding of the same tree, layout documented in
// `packages/cli/README.md`), stdout names both artefacts, exit 0; missing
// dir / no `.tsx` sources → stderr + exit 1.
// ---------------------------------------------------------------------------
describe('TC-TSX-022/023 Batch 3 — T7 build emits IR + binary tree', () => {
  it('build of the inline 2-screen app writes ir.json + tree.mui.bin, stdout names both, exit 0', () => {
    const appDir = mkTmp('muse-build-');
    writeMinimalApp(appDir);
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 0, `build must exit 0, got ${r.status}: ${r.stderr}`);
    const irPath = path.join(appDir, 'dist', 'ir.json');
    const binPath = path.join(appDir, 'dist', 'tree.mui.bin');
    assert.ok(r.stdout.includes('ir.json'), 'stdout must name ir.json');
    assert.ok(r.stdout.includes('tree.mui.bin'), 'stdout must name tree.mui.bin');
    assert.ok(fs.statSync(irPath).isFile(), 'dist/ir.json must be written');
    assert.ok(fs.statSync(binPath).isFile(), 'dist/tree.mui.bin must be written');
  });

  it('ir.json parses as { version: 1, roots: [...] } with static-schema nodes', () => {
    const appDir = mkTmp('muse-build-');
    writeMinimalApp(appDir);
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 0, `build must exit 0, got ${r.status}: ${r.stderr}`);
    const ir = JSON.parse(fs.readFileSync(path.join(appDir, 'dist', 'ir.json'), 'utf8'));
    assert.equal(ir.version, 1, 'IR version must be 1');
    assert.ok(Array.isArray(ir.roots) && ir.roots.length > 0, 'IR must carry a non-empty roots array');
  });

  it('tree.mui.bin is a non-empty length-prefixed blob with the MUI1 magic header', () => {
    const appDir = mkTmp('muse-build-');
    writeMinimalApp(appDir);
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 0, `build must exit 0, got ${r.status}: ${r.stderr}`);
    const buf = fs.readFileSync(path.join(appDir, 'dist', 'tree.mui.bin'));
    assert.ok(buf.length > 4, 'binary tree must be non-empty');
    assert.equal(buf.subarray(0, 4).toString('ascii'), 'MUI1', 'binary tree must start with the MUI1 magic header');
  });

  it('the binary layout is documented in packages/cli/README.md', () => {
    const readme = fs.readFileSync(path.join(CLI_DIR, 'README.md'), 'utf8');
    assert.ok(readme.includes('MUI1'), 'README must document the MUI1 magic header');
    assert.ok(/length-prefix/i.test(readme), 'README must document the length-prefixed record layout');
    assert.ok(readme.includes('tree.mui.bin'), 'README must name tree.mui.bin');
  });

  it('build of a missing dir exits 1 with an error on stderr', () => {
    const missing = path.join(mkTmp('muse-build-'), 'no-such-app');
    const r = runCli(['build', missing]);
    assert.equal(r.status, 1, `build of a missing dir must exit 1, got ${r.status}`);
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
  });

  it('build with no .tsx sources exits 1 with an error on stderr and writes no artefacts', () => {
    const appDir = mkTmp('muse-build-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: empty\n');
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 1, `build with no .tsx must exit 1, got ${r.status}`);
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
    assert.ok(!fs.existsSync(path.join(appDir, 'dist', 'ir.json')), 'must not write ir.json on failure');
  });

  it('build with a missing <appDir> exits 1', () => {
    const r = runCli(['build']);
    assert.equal(r.status, 1, `build with no appDir must exit 1, got ${r.status}`);
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
  });
});

// === Batch 4 (T8 dev, T9 run, T10 fixture) appends NEW describe blocks below. ===

// ---------------------------------------------------------------------------
// T8 — `muse-native dev` validates + serves (minimal).
// Interface: `muse-native dev <appDir>` → validates sources (same loader as
// build), stdout `serving <appDir> at http://127.0.0.1:<port>/` (default port
// documented, overridable later), stays alive until SIGINT; invalid tree →
// stderr + exit 1 before serving.
// ---------------------------------------------------------------------------
describe('TC-TSX-022 Batch 4 — T8 dev validates + serves', () => {
  const devChildren = [];
  afterEach(() => {
    while (devChildren.length) {
      const child = devChildren.pop();
      try {
        child.kill('SIGINT');
      } catch {
        // already exited — nothing to kill
      }
    }
  });

  it('dev on the inline fixture prints the serve line and stays alive until SIGINT', async () => {
    const { spawn } = require('node:child_process');
    const appDir = mkTmp('muse-dev-');
    writeMinimalApp(appDir);
    const child = spawn(process.execPath, [BIN_NATIVE, 'dev', appDir]);
    devChildren.push(child);
    let stdout = '';
    child.stdout.setEncoding('utf8');
    child.stdout.on('data', (d) => {
      stdout += d;
    });
    const serveRe = /serving .* at http:\/\/127\.0\.0\.1:\d+\//;
    const seen = await new Promise((resolve) => {
      const timer = setTimeout(() => resolve(false), 5000);
      const poll = setInterval(() => {
        if (serveRe.test(stdout)) {
          clearTimeout(timer);
          clearInterval(poll);
          resolve(true);
        }
      }, 50);
      child.on('exit', () => {
        clearTimeout(timer);
        clearInterval(poll);
        resolve(false);
      });
    });
    assert.ok(seen, `dev must print 'serving <appDir> at http://127.0.0.1:<port>/', got stdout: ${JSON.stringify(stdout)}`);
    assert.ok(stdout.includes(appDir), 'serve line must name the appDir');
    child.kill('SIGINT');
    await new Promise((resolve) => {
      const timer = setTimeout(resolve, 2000);
      child.on('exit', () => {
        clearTimeout(timer);
        resolve();
      });
    });
  });

  it('dev on a missing dir exits 1 without serving', () => {
    const missing = path.join(mkTmp('muse-dev-'), 'no-such-app');
    const r = runCli(['dev', missing]);
    assert.equal(r.status, 1, `dev on a missing dir must exit 1, got ${r.status}`);
    assert.ok(!r.stdout.includes('serving'), 'must not print a serve line on failure');
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
  });
});

// ---------------------------------------------------------------------------
// T9 — `muse-native run` builds then launches (stub launcher).
// Interface: `muse-native run <appDir>` → runs the build pipeline
// in-memory, then stdout `launching <appDir> (<target>)` with default target
// `host`, exit 0; build failure → stderr + exit 1 without launching.
// ---------------------------------------------------------------------------
describe('TC-TSX-022 Batch 4 — T9 run builds then launches', () => {
  it('run on the inline fixture prints the launch line with default target host, exit 0', () => {
    const appDir = mkTmp('muse-run-');
    writeMinimalApp(appDir);
    const r = runCli(['run', appDir]);
    assert.equal(r.status, 0, `run must exit 0, got ${r.status}: ${r.stderr}`);
    assert.ok(r.stdout.includes(`launching ${appDir} (host)`), `stdout must be 'launching <appDir> (host)', got: ${JSON.stringify(r.stdout)}`);
  });

  it('run on a broken tree exits 1 with no launch line', () => {
    const appDir = mkTmp('muse-run-');
    fs.writeFileSync(path.join(appDir, 'muse-native.yaml'), 'name: broken\n');
    const r = runCli(['run', appDir]);
    assert.equal(r.status, 1, `run on a broken tree must exit 1, got ${r.status}`);
    assert.ok(!r.stdout.includes('launching'), 'must not print a launch line on build failure');
    assert.ok(r.stderr.length > 0, 'must print an error to stderr');
  });
});

// ---------------------------------------------------------------------------
// T10 — Minimal local fixture app for TC-TSX-023 (this run owns it;
// NOT FR-SAMPLE scope).
// Interface: none — fixture data only (two screens importing
// `@muse/native` + `@muse/core`, static props/bindings only, no illegal
// forms).
// The committed `examples/gallery` tree does not exist yet (T10 creates it),
// so the second case below builds the REAL committed path and is expected to
// fail RED with ENOENT until T10 lands; the first case builds an equivalent
// tmp tree to pin the build contract now.
// ---------------------------------------------------------------------------
describe('TC-TSX-023 Batch 4 — T10 gallery fixture builds both artefacts', () => {
  it('build of a tmp gallery-shaped app exits 0 with ir.json + tree.mui.bin carrying MUI1', () => {
    const appDir = mkTmp('muse-gallery-');
    writeMinimalApp(appDir);
    const r = runCli(['build', appDir]);
    assert.equal(r.status, 0, `build must exit 0, got ${r.status}: ${r.stderr}`);
    const ir = JSON.parse(fs.readFileSync(path.join(appDir, 'dist', 'ir.json'), 'utf8'));
    assert.equal(ir.version, 1, 'IR version must be 1');
    assert.ok(Array.isArray(ir.roots) && ir.roots.length > 0, 'IR must carry a non-empty roots array');
    const buf = fs.readFileSync(path.join(appDir, 'dist', 'tree.mui.bin'));
    assert.ok(buf.length > 4, 'binary tree must be non-empty');
    assert.equal(buf.subarray(0, 4).toString('ascii'), 'MUI1', 'binary tree must start with the MUI1 magic header');
  });

  it('build of the committed examples/gallery exits 0 with both artefacts carrying MUI1', () => {
    const gallery = path.resolve(CLI_DIR, '..', '..', 'examples', 'gallery');
    const r = runCli(['build', gallery]);
    assert.equal(r.status, 0, `build examples/gallery must exit 0, got ${r.status}: ${r.stderr}`);
    const ir = JSON.parse(fs.readFileSync(path.join(gallery, 'dist', 'ir.json'), 'utf8'));
    assert.equal(ir.version, 1, 'IR version must be 1');
    assert.ok(Array.isArray(ir.roots) && ir.roots.length > 0, 'IR must carry a non-empty roots array');
    const buf = fs.readFileSync(path.join(gallery, 'dist', 'tree.mui.bin'));
    assert.ok(buf.length > 4, 'binary tree must be non-empty');
    assert.equal(buf.subarray(0, 4).toString('ascii'), 'MUI1', 'binary tree must start with the MUI1 magic header');
    fs.rmSync(path.join(gallery, 'dist'), { recursive: true, force: true });
  });
});
