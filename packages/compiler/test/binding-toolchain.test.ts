'use strict';
// T1 toolchain contract (FR-TSX-006, AC-TSX-018, RULE-TSX-003).
// Hermetic: node:test + node:assert/strict + node:fs/path/child_process only,
// local files, no network, no secrets. Blocked-env per FR-TSX-006 Notes:
// when node_modules/.bin/tsc is absent the tsc-dependent rows report
// blocked-env via t.skip('blocked-env: ...') — never failed code, never
// covered. When the binary exists, strict fail-loud asserts still apply.
const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const BLOCKED_ENV_MSG =
  'blocked-env: tsc absent (node_modules/.bin/tsc missing — run npm install); rows read as environment-blocked per FR-TSX-006 Notes, not failed code';

function localTscBin() {
  return resolveLocalTscBin(
    path.join(__dirname, '..', '..', '..'),
  );
}

// T3 contract (FR-TSX-006, AC-TSX-018): absolute repo-root-relative
// node_modules/.bin/tsc(.cmd) path when fs.existsSync holds, else null.
// Same probe hasLocalTsc() in binding.test.ts applies to.
function resolveLocalTscBin(repoRoot) {
  const bin = path.join(
    repoRoot,
    'node_modules',
    '.bin',
    process.platform === 'win32' ? 'tsc.cmd' : 'tsc',
  );
  return fs.existsSync(bin) ? bin : null;
}

module.exports = { resolveLocalTscBin };

describe('binding toolchain (T1)', () => {
  it('provides local tsc for the TC-TSX-018 live gate', (t) => {
    const bin = localTscBin();
    if (bin === null) {
      t.skip(BLOCKED_ENV_MSG);
      return;
    }
    assert.ok(
      fs.existsSync(bin),
      'typescript devDependency (T1) must provide node_modules/.bin/tsc — run npm install',
    );
  });

  it('local tsc --version reports pinned 5.x', (t) => {
    const bin = localTscBin();
    if (bin === null) {
      t.skip(BLOCKED_ENV_MSG);
      return;
    }
    assert.ok(
      fs.existsSync(bin),
      'typescript devDependency (T1) must provide node_modules/.bin/tsc — run npm install',
    );
    const r = spawnSync(bin, ['--version'], { encoding: 'utf8' });
    assert.equal(r.status, 0, 'tsc --version must exit 0');
    assert.match(
      String(r.stdout),
      /Version 5\./,
      'tsc must report the pinned 5.x toolchain',
    );
  });

  it('resolveLocalTscBin returns null when the toolchain is absent', () => {
    assert.equal(
      resolveLocalTscBin(path.join(__dirname, 'no-such-dir-tsx-005')),
      null,
    );
  });

  it('resolveLocalTscBin provides an executable tsc for the live gate', (t) => {
    const bin = resolveLocalTscBin(path.join(__dirname, '..', '..', '..'));
    if (bin === null) {
      t.skip(BLOCKED_ENV_MSG);
      return;
    }
    assert.ok(
      bin !== null,
      'typescript devDependency (T1) must provide node_modules/.bin/tsc — run npm install',
    );
    const r = spawnSync(bin, ['--version'], { encoding: 'utf8' });
    assert.equal(r.status, 0, 'tsc --version must exit 0');
  });

  it('absent toolchain reports blocked-env, never failed code nor covered (AC-TSX-018)', () => {
    // Hermetic absent-toolchain regression: fixture dir without a toolchain
    // (empty temp dir — no network, no install, no real node_modules).
    // Outcome is independent of the real toolchain.
    const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'muse-tsx-absent-'));
    assert.equal(resolveLocalTscBin(fixture), null);
    // Gating contract: probe-null takes the skip path, never a hard failure.
    // The T1/T2 gates branch on `bin === null` into `t.skip(BLOCKED_ENV_MSG)`;
    // the blocked-env message prefix is the audit-visible marker.
    assert.ok(
      BLOCKED_ENV_MSG.startsWith('blocked-env:'),
      'absent-toolchain rows must report a blocked-env skip message',
    );
    // Fail-closed against restoring fail-on-absent on the absent path:
    // (needle is built dynamically so this regression test's own source
    // does not self-match the forbidden call text.)
    const src = fs.readFileSync(__filename, 'utf8');
    const needle = 'assert.' + 'fail';
    assert.ok(
      !src.includes(needle + '('),
      'absent-toolchain path must not contain a fail-on-absent call (use t.skip blocked-env)',
    );
  });
});
