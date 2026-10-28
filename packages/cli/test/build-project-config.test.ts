'use strict';
// TC-SAMPLE-035 (AC-SAMPLE-035, FR-SAMPLE-006) — Batch 1 (T3).
// Every buildable project owns its own `muse-native.yaml`; a missing
// per-project file fails that target's build with the path named, and no
// root file substitutes for it.
// Hermetic: node:test + node:assert/strict + stdlib only. Uses fresh
// os.tmpdir() fixtures plus the committed examples/m3-gallery fixture (read
// only); no network, no secrets, no live services, no repo writes.
// T3 Interface (plan verbatim):
//   `resolveProjectConfig(target: string): string` — returns
//   `<target>/muse-native.yaml` path; when absent, build fails with stderr
//   naming the missing per-project path (e.g. `Error: missing
//   muse-native.yaml for target '<target>' at
//   '<target>/muse-native.yaml'`) and exit non-zero; never falls back to a
//   workspace-root file.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-T3a: `resolveProjectConfig` is exported from `packages/cli/lib/build.js`
//   (the module T3 names); the plan does not name another home for it.
// - A-T3b: the return is the literal join `<target>/muse-native.yaml`
//   (path.join(target, 'muse-native.yaml')); existence is NOT checked by the
//   resolver itself — absence is reported by the build failure path.
// - A-T3c: this file is not listed in the plan batch table (TC-SAMPLE-035 is
//   recorded as a `file`-type check), but the T3 Interface line names a
//   unit-testable function, so these unit tests own it; the committed
//   `examples/m3-gallery/muse-native.yaml` remains the positive fixture.

const { describe, it, beforeEach, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const build = require('../lib/build.js');

let scratch = null;

beforeEach(() => {
  scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-projcfg-'));
});

afterEach(() => {
  if (scratch) {
    fs.rmSync(scratch, { recursive: true, force: true });
    scratch = null;
  }
});

function resolveProjectConfig(target) {
  assert.equal(
    typeof build.resolveProjectConfig,
    'function',
    'lib/build.js must export resolveProjectConfig(target)',
  );
  return build.resolveProjectConfig(target);
}

describe('TC-SAMPLE-035 — per-project muse-native.yaml only (AC-SAMPLE-035)', () => {
  it('lib/build.js exports resolveProjectConfig(target)', () => {
    assert.equal(
      typeof build.resolveProjectConfig,
      'function',
      'lib/build.js must export resolveProjectConfig(target: string): string',
    );
  });

  it('returns <target>/muse-native.yaml for a target that owns one', () => {
    const target = path.join(scratch, 'my-app');
    fs.mkdirSync(target, { recursive: true });
    fs.writeFileSync(path.join(target, 'muse-native.yaml'), 'name: my-app\n');
    assert.equal(
      resolveProjectConfig(target),
      path.join(target, 'muse-native.yaml'),
    );
  });

  it('positive fixture: examples/m3-gallery owns its muse-native.yaml', () => {
    const gallery = path.resolve(CLI_DIR, '..', '..', 'examples', 'm3-gallery');
    assert.ok(
      fs.existsSync(path.join(gallery, 'muse-native.yaml')),
      'examples/m3-gallery/muse-native.yaml must exist (positive fixture)',
    );
    assert.equal(
      resolveProjectConfig(gallery),
      path.join(gallery, 'muse-native.yaml'),
    );
  });

  it('build fails naming the missing per-project path and exits non-zero', () => {
    const target = path.join(scratch, 'no-config-app');
    fs.mkdirSync(path.join(target, 'src'), { recursive: true });
    fs.writeFileSync(path.join(target, 'src', 'App.tsx'), 'export default function App() {}\n');
    const missing = path.join(target, 'muse-native.yaml');
    assert.ok(!fs.existsSync(missing), 'fixture must lack its own muse-native.yaml');
    const r = spawnSync(process.execPath, [BIN_NATIVE, 'build', target], { encoding: 'utf8' });
    assert.notEqual(r.status, 0, `build without per-project config must exit non-zero, got ${r.status}`);
    assert.ok(
      r.stderr.includes(missing),
      `build stderr must name the missing per-project path '${missing}', got: ${r.stderr}`,
    );
  });

  it('never falls back to a workspace-root (parent-dir) muse-native.yaml', () => {
    // A config file ABOVE the target must not substitute for the missing
    // per-project file: the build still fails naming the target's own path.
    fs.writeFileSync(path.join(scratch, 'muse-native.yaml'), 'name: root-decoy\n');
    const target = path.join(scratch, 'no-config-app');
    fs.mkdirSync(path.join(target, 'src'), { recursive: true });
    fs.writeFileSync(path.join(target, 'src', 'App.tsx'), 'export default function App() {}\n');
    const missing = path.join(target, 'muse-native.yaml');
    const r = spawnSync(process.execPath, [BIN_NATIVE, 'build', target], { encoding: 'utf8' });
    assert.notEqual(r.status, 0, 'build must exit non-zero despite the parent-dir file');
    assert.ok(
      r.stderr.includes(missing),
      `build stderr must still name the target path '${missing}', got: ${r.stderr}`,
    );
  });
});
