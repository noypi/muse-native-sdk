'use strict';
// T12 — Implement `muse-native renderer-prims --assert-m3-set --assert-fail-closed`
// (TC-GRAPHICS-029 Check). Spec refs: FR-GRAPHICS-010, AC-GRAPHICS-029.
// Interface (plan verbatim):
//   `muse-native renderer-prims --assert-m3-set --assert-fail-closed` — exit 0
//   iff the retained 11-kind set holds (all of Rect, RoundedRect, Image, Text,
//   StateLayer, Ripple, ElevationShadow, CircleArc, VectorIcon, ShapedText,
//   OverlayScrim submit) AND undeclared kinds fail closed; exit nonzero
//   otherwise. Hermetic: no device, no network. Passes when run with
//   `--code-root` pointed at the run checkout.
// Detail under test: thin wrapper over the existing hermetic render_smoke_test
// binary/assertions (reuse, not reimplementation); per-command module
// `packages/cli/lib/renderer-prims.js` required from `packages/cli/lib/cli.js`
// argv dispatch; RULE-RUNTIME-001 fail-closed (unknown kind → error, records
// nothing); accepts `--code-root <run-checkout>`.
// Hermetic: node:test + node:assert/strict + stdlib (fs/os/path/child_process)
// only. Spawns `node bin/muse-native.js` against tmpdir fixtures (commands
// .test.js pattern); no device, no network, no secrets, no live services.
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A-P1: the wrapper consults the tree under `--code-root` (sources and/or
//   the hermetic render_smoke_test binary built from them). The kind-removed
//   negative is therefore simulated with a fixture code-root whose
//   `engine/runtime/include/runtime/renderer.h` lacks one of the 11
//   enumerators, without touching production code. If the coder wires the
//   wrapper to a prebuilt binary that ignores source text, reconcile this
//   case to a fixture whose binary exits nonzero — same red, same gate.
// - A-P2: omitting either `--assert-m3-set` or `--assert-fail-closed`, or
//   pointing `--code-root` at a nonexistent dir, exits nonzero (usage/config
//   failure, never a silent pass).

const { describe, it, afterEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const CLI_DIR = path.resolve(__dirname, '..');
const BIN_NATIVE = path.join(CLI_DIR, 'bin', 'muse-native.js');
const REPO_ROOT = path.resolve(__dirname, '..', '..', '..');
const CODE_ROOT = process.env.MUSE_CODE_ROOT || REPO_ROOT;

const ELEVEN_KINDS = [
  'Rect',
  'RoundedRect',
  'Image',
  'Text',
  'StateLayer',
  'Ripple',
  'ElevationShadow',
  'CircleArc',
  'VectorIcon',
  'ShapedText',
  'OverlayScrim',
];

let rendererPrims = null;
try {
  // eslint-disable-next-line global-require
  rendererPrims = require('../lib/renderer-prims.js');
} catch {
  rendererPrims = null;
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

function runCli(args, options) {
  return spawnSync(process.execPath, [BIN_NATIVE, ...args], {
    encoding: 'utf8',
    ...options,
  });
}

// Fixture code-root whose renderer.h carries exactly the given kind names.
// Never touches production code: a tmp tree only.
function writeKindFixture(root, kinds) {
  const incDir = path.join(root, 'engine', 'runtime', 'include', 'runtime');
  fs.mkdirSync(incDir, { recursive: true });
  const body = `enum class PrimitiveKind { ${kinds.join(', ')} };\n`;
  fs.writeFileSync(path.join(incDir, 'renderer.h'), body);
}

describe('T12 — renderer-prims module surface (AC-GRAPHICS-029)', () => {
  it('packages/cli/lib/renderer-prims.js exists and loads', () => {
    assert.ok(rendererPrims !== null, 'lib/renderer-prims.js must exist (thin wrapper module)');
  });

  it('wrapper invokes the hermetic binary path (no device, no network)', () => {
    assert.ok(rendererPrims, 'lib/renderer-prims.js must load');
    const exported = typeof rendererPrims === 'function' ? rendererPrims : rendererPrims.run;
    assert.equal(typeof exported, 'function', 'renderer-prims module must export a callable entry point');
    const src = [rendererPrims.toString(), typeof rendererPrims === 'object' ? JSON.stringify(Object.keys(rendererPrims)) : ''].join('\n');
    assert.ok(
      /render_smoke_test|code-root|codeRoot/i.test(src),
      'wrapper must reference the hermetic render_smoke_test binary/assertions or --code-root (thin reuse, not a reimplementation)',
    );
  });
});

describe('T12 — renderer-prims CLI exit contract (AC-GRAPHICS-029)', () => {
  it('exit 0 with both flags and --code-root at the run checkout', () => {
    const res = runCli(['renderer-prims', '--assert-m3-set', '--assert-fail-closed', '--code-root', CODE_ROOT]);
    assert.equal(
      res.status,
      0,
      `expected exit 0 on the run checkout, got ${res.status}; stderr: ${(res.stderr || '').slice(0, 500)}`,
    );
  });

  it('nonzero against a fixture code-root with a kind removed (set breaks)', () => {
    const root = mkTmp('muse-prims-bad-');
    writeKindFixture(root, ELEVEN_KINDS.filter((k) => k !== 'Ripple'));
    const res = runCli(['renderer-prims', '--assert-m3-set', '--assert-fail-closed', '--code-root', root]);
    assert.notEqual(res.status, 0, 'removing any of the 11 kinds must flip the check nonzero');
  });

  it('nonzero against a fixture code-root with fail-closed broken (unknown kind submits)', () => {
    const root = mkTmp('muse-prims-nofc-');
    writeKindFixture(root, [...ELEVEN_KINDS, 'BogusExtra']);
    const res = runCli(['renderer-prims', '--assert-m3-set', '--assert-fail-closed', '--code-root', root]);
    assert.notEqual(res.status, 0, 'an undeclared kind that does not fail closed must flip the check nonzero');
  });

  it('nonzero when --assert-fail-closed is omitted', () => {
    const res = runCli(['renderer-prims', '--assert-m3-set', '--code-root', CODE_ROOT]);
    assert.notEqual(res.status, 0, 'omitting --assert-fail-closed must not pass');
  });

  it('nonzero when --code-root points at a nonexistent dir', () => {
    const res = runCli([
      'renderer-prims',
      '--assert-m3-set',
      '--assert-fail-closed',
      '--code-root',
      path.join(os.tmpdir(), 'muse-prims-does-not-exist-12345'),
    ]);
    assert.notEqual(res.status, 0, 'nonexistent --code-root must exit nonzero, never a silent pass');
  });
});
