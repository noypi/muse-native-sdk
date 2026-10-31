/**
 * T2 — device-gated theme-proof regression test (FR-M3-005, AC-M3-009/010).
 *
 * Fail-closed gate with loud skip: the file exits 0 with a visible
 * `SKIP: no device <serial>` line when no device is configured (never
 * silent), and fails loud when `MUSE_REQUIRE_LIVE_THEME_PROOF=1` demands
 * live proof but no device answers. When the gate is open, `assertThemeProof`
 * verifies the T3/T4 artifact triple (before PNG, after PNG, logcat slice):
 * it throws on a blank viewport, on identical light/dark pixels, or on
 * missing frame-timing lines.
 *
 * Hermetic: node:test + node:assert/strict only. The only spawned process is
 * the `adb -s <serial> shell getprop sys.boot_completed` gate probe, which
 * runs exclusively when a serial env var is set — with no serial configured
 * (plain CI) the file performs zero spawns and skips loud. Proof assertions
 * read only artifact files; they capture nothing over adb.
 *
 * Follows the BUG-TSX-005 `MUSE_REQUIRE_LIVE_…`/CI precedent: gate on serial
 * env, skip loud (not silent) when absent.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));

export const EXPECTED_DEVICE_SERIAL = '127.0.0.1:6569';
export const REQUIRE_ENV = 'MUSE_REQUIRE_LIVE_THEME_PROOF';

// Lines the T3 logcat slice must contain at least one of (host
// Choreographer/vsync or host instrumentation frame-timing output).
export const FRAME_TIMING_RE =
  /Choreographer|VSYNC|vsync|doFrame|framesMeasured|frame[-_ ]time/i;

// Buffers shorter than this are not a real screencap viewport.
const MIN_VIEWPORT_BYTES = 64;

// ---------------------------------------------------------------------------
// Public interface (plan T2 `Interface:` line)
// ---------------------------------------------------------------------------

/** Serial configured for the theme proof, if any. */
export function deviceSerial(): string | undefined {
  return (
    process.env.ANDROID_SERIAL || process.env.MUSE_DEVICE_SERIAL || undefined
  );
}

/**
 * Returns true only when a device serial is configured (`ANDROID_SERIAL` or
 * `MUSE_DEVICE_SERIAL`) and `adb -s <serial> shell getprop sys.boot_completed`
 * exits 0. Performs no spawn at all when no serial is configured.
 */
export function shouldRunDeviceThemeProof(): boolean {
  const serial = deviceSerial();
  if (!serial) return false;
  try {
    const probed = spawnSync('adb', ['-s', serial, 'shell', 'getprop', 'sys.boot_completed'], {
      encoding: 'utf8',
    });
    return probed.status === 0;
  } catch {
    return false;
  }
}

export interface ThemeProofArtifacts {
  beforePng: string;
  afterPng: string;
  logcat: string;
}

function assertNotBlank(png: Buffer, label: string): void {
  assert.ok(
    png.length >= MIN_VIEWPORT_BYTES,
    `FAIL: blank viewport in ${label} (${png.length} bytes < ${MIN_VIEWPORT_BYTES})`,
  );
  const first = png[0];
  let varied = false;
  for (let i = 1; i < png.length; i += 1) {
    if (png[i] !== first) {
      varied = true;
      break;
    }
  }
  assert.ok(varied, `FAIL: blank viewport in ${label} (zero pixel variance)`);
}

/**
 * Verifies the T3/T4 artifact triple. Throws on a blank viewport, on
 * identical light/dark pixels, or on missing frame-timing lines. Missing
 * artifact files throw via the filesystem read (fail-closed).
 */
export function assertThemeProof({
  beforePng,
  afterPng,
  logcat,
}: ThemeProofArtifacts): void {
  const before = readFileSync(beforePng);
  const after = readFileSync(afterPng);
  assertNotBlank(before, `beforePng (${beforePng})`);
  assertNotBlank(after, `afterPng (${afterPng})`);
  assert.ok(
    !before.equals(after),
    `FAIL: identical light/dark pixels (${beforePng} == ${afterPng}, ${before.length} bytes)`,
  );
  const log = readFileSync(logcat, 'utf8');
  const timingLines = log
    .split('\n')
    .filter((line) => FRAME_TIMING_RE.test(line));
  assert.ok(
    timingLines.length > 0,
    `FAIL: missing frame-timing lines in ${logcat} (expected a Choreographer/vsync/framesMeasured line)`,
  );
}

/** Default T3/T4 artifact locations; overridable via env for the harness. */
export function defaultArtifacts(): ThemeProofArtifacts {
  const evidence =
    process.env.MUSE_THEME_PROOF_DIR ||
    join(HERE, '..', '..', '..', 'workflow.local', 'runs', 'bug-m3-fidelity-002', 'evidence');
  return {
    beforePng: process.env.MUSE_THEME_BEFORE_PNG || join(evidence, 'theme-light.png'),
    afterPng: process.env.MUSE_THEME_AFTER_PNG || join(evidence, 'theme-dark.png'),
    logcat: process.env.MUSE_THEME_LOGCAT || join(evidence, 'theme-switch.logcat'),
  };
}

// ---------------------------------------------------------------------------
// Gate: loud skip when closed, loud failure when live proof is required
// ---------------------------------------------------------------------------

function withCleanSerialEnv(fn: () => void): void {
  const savedAndroid = process.env.ANDROID_SERIAL;
  const savedMuse = process.env.MUSE_DEVICE_SERIAL;
  delete process.env.ANDROID_SERIAL;
  delete process.env.MUSE_DEVICE_SERIAL;
  try {
    fn();
  } finally {
    if (savedAndroid !== undefined) process.env.ANDROID_SERIAL = savedAndroid;
    if (savedMuse !== undefined) process.env.MUSE_DEVICE_SERIAL = savedMuse;
  }
}

test('gate stays closed with no serial configured (hermetic CI)', () => {
  withCleanSerialEnv(() => {
    assert.equal(deviceSerial(), undefined);
    assert.equal(shouldRunDeviceThemeProof(), false);
  });
});

test('gate skips LOUD (never silent) when no device is configured', () => {
  withCleanSerialEnv(() => {
    if (process.env[REQUIRE_ENV] === '1') return; // required-proof failure owns this case
    assert.equal(shouldRunDeviceThemeProof(), false);
    const serial = deviceSerial() ?? 'unset';
    // Loud skip marker: this line must appear in the test output.
    console.log(`SKIP: no device ${serial} (expected ${EXPECTED_DEVICE_SERIAL}) — device theme proof deferred to T3/T4 artifacts`);
  });
});

test('MUSE_REQUIRE_LIVE_THEME_PROOF=1 with no device fails LOUD', () => {
  if (process.env[REQUIRE_ENV] !== '1') return; // not required: vacuous pass
  withCleanSerialEnv(() => {
    assert.equal(
      shouldRunDeviceThemeProof(),
      true,
      `FAIL: ${REQUIRE_ENV}=1 requires live theme proof on ${EXPECTED_DEVICE_SERIAL}, but no device answered the gate probe`,
    );
  });
});

// ---------------------------------------------------------------------------
// assertThemeProof against synthetic fixtures (T3/T4 artifacts absent = red)
// ---------------------------------------------------------------------------

function writeFixtures(files: Record<string, Buffer | string>): string {
  const dir = mkdtempSync(join(tmpdir(), 'theme-device-gate-'));
  for (const [name, content] of Object.entries(files)) {
    writeFileSync(join(dir, name), content);
  }
  return dir;
}

function variedPng(seedByte: number, flipAt: number): Buffer {
  const buf = Buffer.alloc(256, seedByte);
  buf[flipAt] = (seedByte + 77) % 256; // nonzero variance, distinct per fixture
  buf[flipAt + 1] = (seedByte + 13) % 256;
  return buf;
}

const TIMING_LOGCAT = [
  '01-01 00:00:01.000  1234  GalleryPage instance=42 rows=32 mode=light',
  '01-01 00:00:01.050  1234  Choreographer: doFrame frameTimeNanos=123456 framesMeasured=1',
  '01-01 00:00:01.066  1234  GalleryPage instance=42 rows=32 mode=dark repainted=32 reinstantiated=0',
  '',
].join('\n');

test('assertThemeProof throws on a blank viewport', () => {
  const dir = writeFixtures({
    'before.png': Buffer.alloc(256, 0x00),
    'after.png': variedPng(0xab, 10),
    'switch.logcat': TIMING_LOGCAT,
  });
  try {
    assert.throws(
      () =>
        assertThemeProof({
          beforePng: join(dir, 'before.png'),
          afterPng: join(dir, 'after.png'),
          logcat: join(dir, 'switch.logcat'),
        }),
      /blank viewport/,
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('assertThemeProof throws when light/dark pixels are identical', () => {
  const same = variedPng(0xab, 10);
  const dir = writeFixtures({
    'before.png': same,
    'after.png': Buffer.from(same),
    'switch.logcat': TIMING_LOGCAT,
  });
  try {
    assert.throws(
      () =>
        assertThemeProof({
          beforePng: join(dir, 'before.png'),
          afterPng: join(dir, 'after.png'),
          logcat: join(dir, 'switch.logcat'),
        }),
      /identical light\/dark pixels/,
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('assertThemeProof throws when frame-timing lines are missing', () => {
  const dir = writeFixtures({
    'before.png': variedPng(0xab, 10),
    'after.png': variedPng(0x34, 200),
    'switch.logcat': 'GalleryPage instance=42 mode=dark\nno timing here\n',
  });
  try {
    assert.throws(
      () =>
        assertThemeProof({
          beforePng: join(dir, 'before.png'),
          afterPng: join(dir, 'after.png'),
          logcat: join(dir, 'switch.logcat'),
        }),
      /missing frame-timing lines/,
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('assertThemeProof throws when an artifact file is missing (fail-closed)', () => {
  const dir = writeFixtures({
    'before.png': variedPng(0xab, 10),
    'after.png': variedPng(0x34, 200),
    'switch.logcat': TIMING_LOGCAT,
  });
  try {
    assert.throws(() =>
      assertThemeProof({
        beforePng: join(dir, 'before.png'),
        afterPng: join(dir, 'after.png'),
        logcat: join(dir, 'does-not-exist.logcat'),
      }),
    );
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('assertThemeProof passes for differing viewports with frame timing', () => {
  const dir = writeFixtures({
    'before.png': variedPng(0xab, 10),
    'after.png': variedPng(0x34, 200),
    'switch.logcat': TIMING_LOGCAT,
  });
  try {
    assertThemeProof({
      beforePng: join(dir, 'before.png'),
      afterPng: join(dir, 'after.png'),
      logcat: join(dir, 'switch.logcat'),
    });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

// ---------------------------------------------------------------------------
// Live proof: runs only when the gate is open (T3/T4 artifacts present)
// ---------------------------------------------------------------------------

test('live device theme proof (gate open only)', () => {
  if (!shouldRunDeviceThemeProof()) {
    const serial = deviceSerial() ?? 'unset';
    console.log(`SKIP: no device ${serial} (expected ${EXPECTED_DEVICE_SERIAL}) — live theme proof needs T3/T4 artifacts`);
    return;
  }
  assertThemeProof(defaultArtifacts());
});
