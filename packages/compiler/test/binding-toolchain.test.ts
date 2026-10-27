'use strict';
// T1 toolchain contract (FR-TSX-006, AC-TSX-018, RULE-TSX-003).
// Hermetic: node:test + node:assert/strict + node:fs/path/child_process only,
// local files, no network, no secrets. Fail-closed: fails (not skips) when
// node_modules/.bin/tsc is absent, so a missing T1 declaration is loud.
const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

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
  it('provides local tsc for the TC-TSX-018 live gate', () => {
    const bin = localTscBin();
    assert.ok(
      fs.existsSync(bin),
      'typescript devDependency (T1) must provide node_modules/.bin/tsc — run npm install',
    );
  });

  it('local tsc --version reports pinned 5.x', () => {
    const bin = localTscBin();
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

  it('resolveLocalTscBin provides an executable tsc for the live gate', () => {
    const bin = resolveLocalTscBin(path.join(__dirname, '..', '..', '..'));
    assert.ok(
      bin !== null,
      'typescript devDependency (T1) must provide node_modules/.bin/tsc — run npm install',
    );
    const r = spawnSync(bin, ['--version'], { encoding: 'utf8' });
    assert.equal(r.status, 0, 'tsc --version must exit 0');
  });
});
