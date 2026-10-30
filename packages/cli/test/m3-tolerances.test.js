'use strict';
// T11 — Fix review minors: trailing newline + variance justification for m3-tolerances.
// Spec refs: FR-GRAPHICS-010, AC-GRAPHICS-030.
// Interface (plan verbatim):
//   file content shape only — `m3-tolerances.json` keeps schema
//   `{"perPixel": <uint>, "maxFailPct": <float>}` with values `perPixel 64 /
//   maxFailPct 0.12`, file ends with exactly one trailing newline;
//   `m3-tolerances.README.md` records the variance justification (T9
//   retained-vs-CPU diff: worst page 8.36%, p99 band — values chosen to bound
//   observed drift with headroom; no spec edit, no tolerance change).
// Hermetic: node:test + node:assert/strict + fs/os/path only. Reads the golden
// dir filesystem-local to the checkout (MUSE_CODE_ROOT or repo root); negative
// cases use fs.mkdtemp fixtures. No device, no network, no secrets, no live
// services, no port binding, no child processes.

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const CODE_ROOT = process.env.MUSE_CODE_ROOT || REPO_ROOT;
const GOLDEN_DIR = path.join(CODE_ROOT, 'examples', 'm3-gallery', 'build', 'goldens');
const JSON_NAME = 'm3-tolerances.json';
const README_NAME = 'm3-tolerances.README.md';

// Pure checker so the same assertions run against the real checkout dir and
// against tmpdir fixtures. Throws on the first violation with a message naming
// the file and the violated rule.
function checkGoldenDir(dir) {
  const jsonPath = path.join(dir, JSON_NAME);
  const readmePath = path.join(dir, README_NAME);
  assert.ok(fs.existsSync(jsonPath), `${JSON_NAME} must exist in ${dir}`);
  const raw = fs.readFileSync(jsonPath, 'utf8');
  assert.ok(raw.endsWith('\n'), `${JSON_NAME} must end with a trailing newline`);
  assert.ok(!raw.endsWith('\n\n'), `${JSON_NAME} must end with exactly one trailing newline (no blank line)`);
  let parsed;
  try {
    parsed = JSON.parse(raw);
  } catch (err) {
    assert.fail(`${JSON_NAME} must parse as JSON: ${err && err.message}`);
  }
  assert.equal(parsed.perPixel, 64, `${JSON_NAME} perPixel must stay 64`);
  assert.equal(parsed.maxFailPct, 0.12, `${JSON_NAME} maxFailPct must stay 0.12`);
  assert.ok(fs.existsSync(readmePath), `${README_NAME} must exist beside ${JSON_NAME}`);
  const readme = fs.readFileSync(readmePath, 'utf8');
  assert.ok(/8\.36%/.test(readme), `${README_NAME} must cite the worst-page 8.36% diff`);
  assert.ok(/p99/i.test(readme), `${README_NAME} must cite the p99 band`);
  assert.ok(/64/.test(readme) && /0\.12/.test(readme), `${README_NAME} must record the chosen values perPixel 64 / maxFailPct 0.12`);
  return parsed;
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

function writeFixture(dir, { jsonContent, readmeContent }) {
  if (jsonContent !== null) {
    fs.writeFileSync(path.join(dir, JSON_NAME), jsonContent);
  }
  if (readmeContent !== null) {
    fs.writeFileSync(path.join(dir, README_NAME), readmeContent);
  }
}

const GOOD_JSON = '{"perPixel": 64, "maxFailPct": 0.12}\n';
const GOOD_README = [
  '# m3-tolerances justification',
  '',
  'T9 retained-vs-CPU diff: worst page 8.36%, p99 band.',
  'Values perPixel 64 / maxFailPct 0.12 bound observed drift with headroom.',
  '',
].join('\n');

describe('T11 — m3-tolerances.json shape in the checkout (AC-GRAPHICS-030)', () => {
  it('m3-tolerances.json exists in the golden dir', () => {
    assert.ok(
      fs.existsSync(path.join(GOLDEN_DIR, JSON_NAME)),
      `${JSON_NAME} must exist in ${GOLDEN_DIR}`,
    );
  });

  it('m3-tolerances.json ends with exactly one trailing newline', () => {
    const raw = fs.readFileSync(path.join(GOLDEN_DIR, JSON_NAME), 'utf8');
    assert.ok(raw.endsWith('\n'), 'tolerances JSON must end with a trailing newline');
    assert.ok(!raw.endsWith('\n\n'), 'tolerances JSON must not end with a blank line');
  });

  it('m3-tolerances.json parses with perPixel 64 / maxFailPct 0.12', () => {
    const parsed = JSON.parse(fs.readFileSync(path.join(GOLDEN_DIR, JSON_NAME), 'utf8'));
    assert.equal(parsed.perPixel, 64);
    assert.equal(parsed.maxFailPct, 0.12);
  });

  it('m3-tolerances.README.md exists beside it and records the variance justification', () => {
    const readmePath = path.join(GOLDEN_DIR, README_NAME);
    assert.ok(fs.existsSync(readmePath), `${README_NAME} must exist beside ${JSON_NAME}`);
    const readme = fs.readFileSync(readmePath, 'utf8');
    assert.ok(/8\.36%/.test(readme), 'README must cite worst-page 8.36%');
    assert.ok(/p99/i.test(readme), 'README must cite the p99 band');
  });

  it('checkout golden dir passes the full shape check', () => {
    checkGoldenDir(GOLDEN_DIR);
  });
});

describe('T11 — fixture negatives prove the checker is not vacuous', () => {
  it('missing trailing newline fails the shape check', () => {
    const dir = mkTmp('m3-tol-nonl-');
    writeFixture(dir, { jsonContent: '{"perPixel": 64, "maxFailPct": 0.12}', readmeContent: GOOD_README });
    assert.throws(() => checkGoldenDir(dir), /trailing newline/);
  });

  it('changed tolerance values fail the shape check', () => {
    const dir = mkTmp('m3-tol-vals-');
    writeFixture(dir, { jsonContent: '{"perPixel": 32, "maxFailPct": 0.05}\n', readmeContent: GOOD_README });
    assert.throws(() => checkGoldenDir(dir), /perPixel must stay 64/);
  });

  it('missing README fails the shape check', () => {
    const dir = mkTmp('m3-tol-noreadme-');
    writeFixture(dir, { jsonContent: GOOD_JSON, readmeContent: null });
    assert.throws(() => checkGoldenDir(dir), /README\.md must exist/);
  });

  it('README without the 8.36% citation fails the shape check', () => {
    const dir = mkTmp('m3-tol-nojust-');
    writeFixture(dir, { jsonContent: GOOD_JSON, readmeContent: '# tolerances\nsome words\n' });
    assert.throws(() => checkGoldenDir(dir), /8\.36%/);
  });

  it('good fixture passes the shape check', () => {
    const dir = mkTmp('m3-tol-good-');
    writeFixture(dir, { jsonContent: GOOD_JSON, readmeContent: GOOD_README });
    assert.deepEqual(checkGoldenDir(dir), { perPixel: 64, maxFailPct: 0.12 });
  });
});
