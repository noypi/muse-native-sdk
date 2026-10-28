'use strict';
// T5 — key-scan script and CI gate proving no embedded key.
// Hermetic: node:test + node:assert/strict (+ node:fs/os/path/child_process,
// all local files, no network, no secrets, no live services). Fixtures use
// obvious dummy values only; temp dirs live under os.tmpdir() and are
// removed in teardown. The script under test is scripts/scan-keys.sh.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const REPO_ROOT = path.join(__dirname, '..', '..');
const SCRIPT = path.join(REPO_ROOT, 'scripts', 'scan-keys.sh');

const tmpRoots = [];
afterEach(() => {
  while (tmpRoots.length) {
    fs.rmSync(tmpRoots.pop(), { recursive: true, force: true });
  }
});

function mkDir() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-scan-keys-'));
  tmpRoots.push(dir);
  return dir;
}

// Run the gate against a fixture dir: `sh scan-keys.sh [--strict] <dir>`.
// ASSUMPTION: the script accepts an optional target-dir positional argument
// defaulting to the repo root, so fixtures stay hermetic (see T5.md).
function runGate(args, dir) {
  assert.ok(fs.existsSync(SCRIPT), `scripts/scan-keys.sh must exist (T5 production file)`);
  return spawnSync('sh', [SCRIPT, ...args, dir], { encoding: 'utf8' });
}

describe('T5 static — scan-keys.sh gate shape (AC-MUSE-004)', () => {
  it('script exists and handles a --strict flag', () => {
    assert.ok(fs.existsSync(SCRIPT), 'scripts/scan-keys.sh must exist');
    const src = fs.readFileSync(SCRIPT, 'utf8');
    assert.ok(/--strict/.test(src), 'script must document/handle a --strict flag');
  });

  it('script forbids key patterns (sk-, muse-secret, xai- style tokens, BEGIN PRIVATE KEY)', () => {
    const src = fs.readFileSync(SCRIPT, 'utf8');
    for (const pat of ['sk-', 'muse-secret', 'xai-', 'BEGIN PRIVATE KEY']) {
      assert.ok(
        src.includes(pat),
        `script must forbid the key pattern ${JSON.stringify(pat)}`,
      );
    }
  });

  it('script allows Muse model IDs and excludes proxy/ server env and fixtures', () => {
    const src = fs.readFileSync(SCRIPT, 'utf8');
    assert.ok(/sam-3\.1|muse-spark/i.test(src), 'script must allow-list Muse model IDs');
    assert.ok(/proxy/.test(src), 'script must exclude proxy/ server env from the scan');
  });
});

describe('T5 behavioral — scan-keys.sh --strict exit contract (AC-MUSE-004, AC-MUSE-036)', () => {
  it('fixture key fails --strict: exit 1 naming the offending file+pattern', () => {
    const dir = mkDir();
    const evil = path.join(dir, 'evil-client.js');
    fs.writeFileSync(evil, 'const key = "sk-test-not-real-secret-value";\n');
    const r = runGate(['--strict'], dir);
    assert.equal(r.status, 1, `fixture key must fail --strict, got: ${r.stdout}${r.stderr}`);
    const out = `${r.stdout}${r.stderr}`;
    assert.ok(out.includes('evil-client.js'), 'exit 1 must name the offending file');
    assert.ok(/sk-/.test(out), 'exit 1 must name the offending pattern');
  });

  it('muse-secret style token fails --strict', () => {
    const dir = mkDir();
    fs.writeFileSync(
      path.join(dir, 'bundle.js'),
      'const k = "muse-secret-test-not-real";\n',
    );
    const r = runGate(['--strict'], dir);
    assert.equal(r.status, 1, `muse-secret fixture must fail --strict, got: ${r.stdout}${r.stderr}`);
  });

  it('BEGIN PRIVATE KEY block fails --strict', () => {
    const dir = mkDir();
    fs.writeFileSync(
      path.join(dir, 'key.pem'),
      '-----BEGIN PRIVATE KEY-----\nZHVtbXktbm90LXJlYWw=\n-----END PRIVATE KEY-----\n',
    );
    const r = runGate(['--strict'], dir);
    assert.equal(r.status, 1, `private-key fixture must fail --strict, got: ${r.stdout}${r.stderr}`);
  });

  it('model-ID-only fixture passes --strict (exit 0)', () => {
    const dir = mkDir();
    fs.writeFileSync(
      path.join(dir, 'client.js'),
      '// model IDs are allow-listed, never keys\n' +
        'const models = ["sam-3.1", "muse-spark-1.3", "muse-voice-transcribe-1.0", "muse-image"];\n' +
        'module.exports = models;\n',
    );
    const r = runGate(['--strict'], dir);
    assert.equal(r.status, 0, `model-ID-only fixture must pass --strict, got: ${r.stdout}${r.stderr}`);
  });

  it('clean tree passes without --strict as well (exit 0)', () => {
    const dir = mkDir();
    fs.writeFileSync(path.join(dir, 'clean.js'), 'module.exports = { ok: true };\n');
    const r = runGate([], dir);
    assert.equal(r.status, 0, `clean fixture must pass, got: ${r.stdout}${r.stderr}`);
  });

  it('delimiter-prefixed sk- key still fails --strict post-anchor: key=sk-... exit 1 (SEC-REVIEW)', () => {
    const dir = mkDir();
    fs.writeFileSync(
      path.join(dir, 'leak-env.js'),
      'key=sk-test-not-real-secret-value\n',
    );
    const r = runGate(['--strict'], dir);
    assert.equal(r.status, 1, `delimiter-prefixed sk- fixture must fail --strict, got: ${r.stdout}${r.stderr}`);
    const out = `${r.stdout}${r.stderr}`;
    assert.ok(out.includes('leak-env.js'), 'exit 1 must name the offending file');
    assert.ok(/sk-/.test(out), 'exit 1 must name the offending pattern');
  });

  it('delimiter-prefixed xai- key still fails --strict post-anchor: key=xai-... exit 1 (SEC-REVIEW)', () => {
    const dir = mkDir();
    fs.writeFileSync(
      path.join(dir, 'leak-xai.js'),
      'key=xai-test-not-real-secret-value\n',
    );
    const r = runGate(['--strict'], dir);
    assert.equal(r.status, 1, `delimiter-prefixed xai- fixture must fail --strict, got: ${r.stdout}${r.stderr}`);
    const out = `${r.stdout}${r.stderr}`;
    assert.ok(out.includes('leak-xai.js'), 'exit 1 must name the offending file');
    assert.ok(/xai-/.test(out), 'exit 1 must name the offending pattern');
  });

  it('mid-word sk- words pass --strict: task-affinity + disk-scheduling exit 0 (BUG-MUSE-001)', () => {
    const dir = mkDir();
    fs.writeFileSync(
      path.join(dir, 'lint.py'),
      '"""FR-NAV-010 T11: Launch-mode / task-affinity contradiction lint.\n' +
        'disk-scheduling note: no key material here.\n' +
        '"""\n',
    );
    const r = runGate(['--strict'], dir);
    assert.equal(r.status, 0, `task-affinity fixture must pass --strict, got: ${r.stdout}${r.stderr}`);
  });
});
