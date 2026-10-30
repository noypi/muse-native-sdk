'use strict';
// TC-SAMPLE-013 (AC-SAMPLE-013, FR-SAMPLE-006) — Batch 1 (T1) + Batch 2 (T3, T4).
// `muse-native init <dir>` scaffolds a complete TS/TSX project that builds
// and runs with the documented commands and no manual edit first.
// Hermetic: node:test + node:assert/strict + stdlib only. Drives the real
// CLI via spawnSync into fresh os.tmpdir() trees; no network, no secrets,
// no live services, no repo writes. Style mirrors test/help.test.js.
// T1 Interface (plan verbatim):
//   `runInit(dir: string, opts?: { force?: boolean }): void` — copies template
//   tree (muse-native.yaml, package.json with four fixed `@muse/*` deps,
//   tsconfig.json with `jsxImportSource: "@muse/jsx"`, src/App.tsx, README
//   with next commands) into `dir`; exits `2` usage on missing operand, `1`
//   on guard refusal, non-zero with named path on copy failure. Supported
//   `muse` spelling aliases to same path; NO `capo` spelling exists
//   (HIST-0290 / RULE-SAMPLE-004 v2.17: canonical `muse-native` + `muse`
//   alias only).
// T3 Interface (plan verbatim):
//   `templateFile(rel: string): string` — `rel` in
//   `{'Makefile','AndroidManifest.xml','res/values/strings.xml'}` → utf8 file
//   content equal to the corresponding `examples/m3-gallery`-shaped source
//   with `APP_NAME`/label placeholders per A2/A3; missing `rel` throws
//   `ENOENT` — returns file text, throws on missing path
// T4 Interface (plan verbatim):
//   `runInit(dir: string, opts?: { force?: boolean }): void` — copies extended
//   template tree (`muse-native.yaml`, `package.json`, `tsconfig.json`,
//   `README.md`, `src/App.tsx`, `proxy/proxy.config.json`, `Makefile`,
//   `AndroidManifest.xml`, `res/values/strings.xml`) into `dir`; exits `2`
//   usage on missing operand, `1` on guard refusal, non-zero with named path
//   on copy failure; `scaffoldProject(args: { templateDir: string, outDir:
//   string }): { ok: true, projectDir: string }` — same file set, throws
//   `scaffold-failed: <reason>` on invalid input or copy failure
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T1a: the public boundary is the CLI (`muse-native init <dir>`); the
//   `runInit` export shape is asserted, exit codes via the CLI.
// - A-T1b: the `muse` alias is `bin/muse.js`; per HIST-0290 there is NO
//   `capo` alias — `bin/capo.js` must be absent and the `capo` spelling
//   must not resolve (fails closed / unresolvable). Red until the coder
//   removes the capo surface.
// - A-T3a: the test-local `templateFile(rel)` helper below IS the T3
//   boundary proxy: it reads `templates/<rel>` as utf8 and lets the stdlib
//   raise `ENOENT` for a missing `rel`. The coder satisfies T3 by providing
//   the three template sources (or a remap note for the manifest lane); no
//   production helper is required for these assertions to pass.
// - A-T3b: the coder takes the new-file lane
//   (`templates/AndroidManifest.xml` at the scaffold root). If the coder
//   instead reuses `templates/android/AndroidManifest.xml` with a remap note
//   in `lib/init.js`, the manifest-source case below accepts that lane.
// - A-T4a: the scaffolded `Makefile` may differ from the template on exactly
//   the single `APP_NAME =` line (D4 substitution at init). All other
//   scaffolded files must be byte-equal to their template sources.
// - A-T4b: the scaffolded manifest/strings carry the template's placeholder
//   identity verbatim (no per-app label rewrite at init); the per-demo
//   `Flappy Demo` / `Muse Demo` values live in the demo backfills (T1/T2),
//   not in `init` output.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const TEMPLATES_DIR = path.join(CLI_DIR, 'templates');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const BIN_MUSE = path.join(CLI_DIR, 'bin', 'muse.js');
const BIN_CAPO = path.join(CLI_DIR, 'bin', 'capo.js');

// Full template tree T3+T4 must copy (relative to the scaffold root).
const EXPECTED_TREE = [
  'muse-native.yaml',
  'package.json',
  'tsconfig.json',
  path.join('src', 'App.tsx'),
  'README.md',
  path.join('proxy', 'proxy.config.json'),
  'Makefile',
  'AndroidManifest.xml',
  path.join('res', 'values', 'strings.xml'),
];

// T3 template-source rels (relative to templates/).
const TEMPLATE_RELS = [
  'Makefile',
  'AndroidManifest.xml',
  path.join('res', 'values', 'strings.xml'),
];

// T3 public-boundary proxy: returns utf8 template text, throws ENOENT on a
// missing rel (stdlib readFileSync raises it; unknown rels resolve under
// templates/ and raise the same way).
function templateFile(rel) {
  return fs.readFileSync(path.join(TEMPLATES_DIR, rel), 'utf8');
}

function manifestSourceText() {
  const rootManifest = path.join(TEMPLATES_DIR, 'AndroidManifest.xml');
  if (fs.existsSync(rootManifest)) {
    return { file: rootManifest, text: fs.readFileSync(rootManifest, 'utf8') };
  }
  // Remap lane: coder reuses templates/android/AndroidManifest.xml with a
  // remap note in lib/init.js (task-allowed alternative to the new file).
  const initSrc = fs.readFileSync(path.join(CLI_DIR, 'lib', 'init.js'), 'utf8');
  assert.ok(
    initSrc.includes(path.join('android', 'AndroidManifest.xml')) ||
      initSrc.includes('android/AndroidManifest.xml'),
    'scaffold-root manifest must come from templates/AndroidManifest.xml or a remap note in lib/init.js',
  );
  const androidManifest = path.join(TEMPLATES_DIR, 'android', 'AndroidManifest.xml');
  return { file: androidManifest, text: fs.readFileSync(androidManifest, 'utf8') };
}

// Normalize only the single D4 `APP_NAME =` line so the scaffolded Makefile
// compares equal modulo the init-time substitution (A-T4a).
function normalizeAppNameLine(text) {
  return text
    .split('\n')
    .map((line) => (/^APP_NAME\s*=/.test(line) ? 'APP_NAME = <init-substituted>' : line))
    .join('\n');
}

let scratch = null;

beforeEach(() => {
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-init-013-'));
});

afterEach(() => {
  if (scratch) {
    fs.rmSync(scratch, { recursive: true, force: true });
    scratch = null;
  }
});

function runCli(bin, args) {
  return spawnSync(process.execPath, [bin, ...args], { encoding: 'utf8' });
}

describe('TC-SAMPLE-013 — init scaffolds a complete project (AC-SAMPLE-013)', () => {
  it('lib/init.js exports runInit as its module function', () => {
    const runInit = require('../lib/init.js');
    assert.equal(typeof runInit, 'function', 'lib/init.js must export runInit(dir, opts?) as a function');
  });

  it('init <dir> exits 0 and copies the full template tree', () => {
    const dir = path.join(scratch, 'my-app');
    const r = runCli(BIN_NATIVE, ['init', dir]);
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    for (const rel of EXPECTED_TREE) {
      assert.ok(
        fs.existsSync(path.join(dir, rel)),
        `scaffolded project must contain ${rel}`,
      );
    }
  });

  it('scaffolded tree matches the templates/ source tree file-for-file', () => {
    const dir = path.join(scratch, 'my-app');
    const r = runCli(BIN_NATIVE, ['init', dir]);
    assert.equal(r.status, 0, `init must exit 0, got ${r.status}: ${r.stderr}`);
    const templatesDir = path.join(CLI_DIR, 'templates');
    for (const rel of EXPECTED_TREE) {
      const want = fs.readFileSync(path.join(templatesDir, rel), 'utf8');
      const got = fs.readFileSync(path.join(dir, rel), 'utf8');
      if (rel === 'Makefile') {
        assert.equal(
          normalizeAppNameLine(got),
          normalizeAppNameLine(want),
          'scaffolded Makefile must equal templates/Makefile modulo the single APP_NAME line (D4)',
        );
      } else {
        assert.equal(got, want, `scaffolded ${rel} must equal templates/${rel}`);
      }
    }
  });

  it('init with no <dir> operand is usage error with exit 2', () => {
    const r = runCli(BIN_NATIVE, ['init']);
    assert.equal(r.status, 2, `missing operand must exit 2, got ${r.status}: ${r.stdout}`);
    assert.ok(
      r.stderr.includes('Usage: muse-native init <dir>'),
      `missing operand must print usage to stderr, got: ${r.stderr}`,
    );
  });

  it('copy failure exits non-zero and names the path', () => {
    // A regular file where the scaffold root must go: mkdir fails, and the
    // failure must name the path. (dir exists as non-dir -> guard refusal
    // also names the path with exit 1; either way the path is named.)
    const blocker = path.join(scratch, 'blocker');
    fs.writeFileSync(blocker, 'x');
    const r = runCli(BIN_NATIVE, ['init', blocker]);
    assert.notEqual(r.status, 0, 'copy/guard failure must exit non-zero');
    assert.ok(
      r.stderr.includes(blocker),
      `failure must name the path '${blocker}', got: ${r.stderr}`,
    );
  });

  it('supported `muse` spelling aliases to the same scaffold path', () => {
    const dir = path.join(scratch, 'alias-app');
    const r = runCli(BIN_MUSE, ['init', dir]);
    assert.equal(r.status, 0, `muse init must exit 0, got ${r.status}: ${r.stderr}`);
    for (const rel of EXPECTED_TREE) {
      assert.ok(
        fs.existsSync(path.join(dir, rel)),
        `muse-alias scaffold must contain ${rel}`,
      );
    }
  });

  it('`capo` bin is absent (HIST-0290: no capo spelling)', () => {
    assert.ok(
      !fs.existsSync(BIN_CAPO),
      'bin/capo.js must NOT exist (canonical muse-native + muse alias only)',
    );
  });

  it('`capo` spelling is not resolved (fails closed)', () => {
    // The capo bin must be unresolvable: invoking it via node fails closed
    // (non-zero, ENOENT-style error) and scaffolds nothing.
    const dir = path.join(scratch, 'capo-app');
    const r = runCli(BIN_CAPO, ['init', dir]);
    assert.notEqual(r.status, 0, 'capo init must fail closed (non-zero exit)');
    assert.ok(
      !fs.existsSync(path.join(dir, 'muse-native.yaml')),
      'capo invocation must not scaffold a project',
    );
  });
});

describe('T3 — scaffold template sources (AC-SAMPLE-013, AC-PACKAGING-020/023)', () => {
  it('templateFile returns utf8 text for every template rel', () => {
    for (const rel of TEMPLATE_RELS) {
      const text = templateFile(rel);
      assert.ok(typeof text === 'string' && text.length > 0, `templateFile('${rel}') must return non-empty text`);
    }
  });

  it('templates/Makefile carries the full five-target recipe with the MUSE_NATIVE_HOME gate', () => {
    const text = templateFile('Makefile');
    for (const target of ['apk:', 'sign:', 'verify:', 'install:', 'clean:']) {
      assert.ok(text.includes(target), `template Makefile must define the '${target}' target`);
    }
    assert.ok(text.includes('ifndef MUSE_NATIVE_HOME'), 'template Makefile must carry the MUSE_NATIVE_HOME ifndef gate');
    assert.ok(text.includes('MUSE_NATIVE_HOME is not set'), 'template Makefile must name MUSE_NATIVE_HOME when unset');
    assert.ok(/^APP_NAME\s*=/m.test(text), 'template Makefile must carry a single APP_NAME = line (D4 substitution point)');
    for (const token of ['aapt2', 'javac', 'd8', 'zipalign', 'apksigner', 'adb']) {
      assert.ok(text.includes(token), `template Makefile must reference '${token}'`);
    }
  });

  it('templates/Makefile has no parent-dir traversal', () => {
    const text = templateFile('Makefile');
    assert.ok(!text.includes('../../'), 'template Makefile must not contain ../../ traversal');
    const lines = text.split('\n').filter((l) => !l.includes('AC-023') && !l.includes('NOTE (T2'));
    assert.ok(
      !lines.some((l) => l.includes('../build')),
      'template Makefile must not escape into ../build (AC-PACKAGING-023)',
    );
  });

  it('scaffold-root manifest source has the gallery/android shape (package com.muse.host, SDK 26/34, .MainActivity)', () => {
    const { file, text } = manifestSourceText();
    assert.ok(text.includes('package="com.muse.host"'), `manifest source ${file} must declare package com.muse.host (D2)`);
    assert.ok(text.includes('android:minSdkVersion="26"'), 'manifest source must set minSdk 26');
    assert.ok(text.includes('android:targetSdkVersion="34"'), 'manifest source must set targetSdk 34');
    assert.ok(text.includes('.MainActivity'), 'manifest source must declare .MainActivity');
  });

  it('templates/res/values/strings.xml carries exactly one app_name string (not the gallery value)', () => {
    const text = templateFile(path.join('res', 'values', 'strings.xml'));
    const matches = text.match(/<string\s+name="app_name">/g) || [];
    assert.equal(matches.length, 1, `strings template must carry exactly one app_name string, found ${matches.length}`);
    assert.ok(!text.includes('M3 UI Gallery'), 'strings template must NOT carry the gallery app_name value');
  });

  it('templateFile throws ENOENT for a missing rel', () => {
    assert.throws(
      () => templateFile(path.join('res', 'values', 'does-not-exist.xml')),
      (err) => err && err.code === 'ENOENT',
      'templateFile for a missing rel must throw ENOENT',
    );
  });
});

describe('T4 — init copies the extended nine-file tree with rollback (AC-SAMPLE-013, AC-PACKAGING-020)', () => {
  it('scaffoldProject emits the extended nine-file tree byte-equal to the template sources', () => {
    const { scaffoldProject } = require('../lib/init.js');
    const outDir = path.join(scratch, 'proj');
    const res = scaffoldProject({ templateDir: TEMPLATES_DIR, outDir });
    assert.equal(res.ok, true, 'scaffoldProject must return { ok: true, ... }');
    assert.equal(res.projectDir, outDir, 'scaffoldProject must return the output directory');
    for (const rel of EXPECTED_TREE) {
      const want = fs.readFileSync(path.join(TEMPLATES_DIR, rel), 'utf8');
      const got = fs.readFileSync(path.join(outDir, rel), 'utf8');
      if (rel === 'Makefile') {
        assert.equal(
          normalizeAppNameLine(got),
          normalizeAppNameLine(want),
          'scaffolded Makefile must equal the template modulo the single APP_NAME line (D4)',
        );
      } else {
        assert.equal(got, want, `scaffolded ${rel} must equal the template source`);
      }
    }
  });

  it('scaffoldProject throws scaffold-failed on invalid input', () => {
    const { scaffoldProject } = require('../lib/init.js');
    assert.throws(
      () => scaffoldProject({ templateDir: '', outDir: path.join(scratch, 'bad-a') }),
      /scaffold-failed: invalid templateDir/,
      'empty templateDir must throw scaffold-failed: invalid templateDir',
    );
    assert.throws(
      () => scaffoldProject({ templateDir: TEMPLATES_DIR, outDir: '' }),
      /scaffold-failed: invalid outDir/,
      'empty outDir must throw scaffold-failed: invalid outDir',
    );
  });

  it('scaffoldProject throws scaffold-failed on copy failure and leaves no res/ remnant', () => {
    const { scaffoldProject } = require('../lib/init.js');
    // A sparse template dir: the first files copy, then a missing rel fails
    // the loop mid-copy (mirrors a partial-failure rollback).
    const sparse = path.join(scratch, 'sparse-templates');
    fs.mkdirSync(path.join(sparse, 'src'), { recursive: true });
    fs.copyFileSync(path.join(TEMPLATES_DIR, 'muse-native.yaml'), path.join(sparse, 'muse-native.yaml'));
    fs.copyFileSync(path.join(TEMPLATES_DIR, 'src', 'App.tsx'), path.join(sparse, 'src', 'App.tsx'));
    const outDir = path.join(scratch, 'partial');
    fs.mkdirSync(outDir, { recursive: true });
    assert.throws(
      () => scaffoldProject({ templateDir: sparse, outDir }),
      /scaffold-failed: copy failed/,
      'copy failure must throw scaffold-failed: copy failed',
    );
    assert.ok(
      !fs.existsSync(path.join(outDir, 'res')),
      'rollback must leave no res/ remnant after a partial-failure copy',
    );
  });

  it('init into a non-empty dir without --force refuses with exit 1 and adds no res/ tree', () => {
    const dir = path.join(scratch, 'existing');
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'keep.txt'), 'keep');
    const r = runCli(BIN_NATIVE, ['init', dir]);
    assert.equal(r.status, 1, `guard refusal must exit 1, got ${r.status}: ${r.stderr}`);
    assert.ok(
      !fs.existsSync(path.join(dir, 'res')),
      'guard refusal must not scaffold a res/ tree',
    );
    assert.ok(fs.existsSync(path.join(dir, 'keep.txt')), 'guard refusal must leave existing content alone');
  });
});
