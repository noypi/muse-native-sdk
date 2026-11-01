'use strict';
// BUG-SAMPLE-007 Batch 1 (T2) — proof advance-leg regression test.
// AC-M3-020 (TC-M3-021 regression).
//
// Hermetic: node:test + node:assert/strict + node:fs/node:path only. No adb
// spawn, no ANDROID_SERIAL use, no secrets, no config, no downloads, no
// device. Every advance/readIndex is an in-memory stub injected through the
// public `pageAllAndVerifyWrap` interface; sleeps resolve immediately; the
// only filesystem reads are static source pins. No shared mutable state.
//
// Binds ONLY to the plan interface:
//   T2: `assertAdvanceProducesMotion(advance: (opts: object) => Promise<unknown>,
//       readIndex: () => Promise<number>, pageCount: number): Promise<{ sawMotion: boolean }>`
//       — test-only helper mirroring `pageAllAndVerifyWrap` semantics in
//       `packages/cli/lib/gallery-proof.js` (rejects with `counts-mismatch`/
//       `no-motion` when the advance path is inert; resolves with
//       `sawMotion: true` when the index moves; never silently passes on zero
//       motion).
//
// The helper is defined in this file (test-only, per the plan) and delegates
// to the production `pageAllAndVerifyWrap` import where importable, tracking
// the indices observed mid-cycle to report `sawMotion` honestly.
//
// ASSUMPTIONS:
//   A-T2a: `pageAllAndVerifyWrap` stays importable from
//       `packages/cli/lib/gallery-proof.js` with its current injectable shape
//       `{ pageCount, advance, readIndex, sleep, settleMs }` and its
//       `failWith('counts-mismatch', ...)` rejection contract. T4 must not
//       change this production logic (plan acceptance); the source-guard test
//       below pins it.
//   A-T2b (red pin): the "non-neutralized path" assertion is a static source
//       pin on `examples/m3-gallery/src/MainActivity.java`: the
//       `advanceReceiver.onReceive` body must contain a call-shaped
//       `nativeAdvancePage(engineHandle)` (fail-closed on missing origin
//       stays a separate T4 coder obligation). Pre-fix the receiver is an
//       intentional no-op, so this test fails until T4 re-arms it.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { join, resolve } = require('node:path');

const REPO_ROOT = resolve(__dirname, '..', '..', '..');
const PROOF_LIB = join(REPO_ROOT, 'packages', 'cli', 'lib', 'gallery-proof.js');
const MAIN_ACTIVITY = join(__dirname, '..', 'src', 'MainActivity.java');

function tryLoadProof() {
  try {
    return { mod: require(PROOF_LIB), error: null };
  } catch (err) {
    return { mod: null, error: err };
  }
}

function requireProof() {
  const { mod, error } = tryLoadProof();
  assert.equal(error, null, `gallery-proof.js must be importable at ${PROOF_LIB}: ${error && error.message}`);
  assert.equal(
    typeof mod.pageAllAndVerifyWrap,
    'function',
    'gallery-proof.js must export pageAllAndVerifyWrap',
  );
  return mod;
}

// Test-only helper (plan T2 Interface): mirrors pageAllAndVerifyWrap motion
// semantics and reports the motion it observed. Rejects exactly when the
// production verifier rejects (counts-mismatch / no-motion on an inert
// path); resolves `{ sawMotion }` otherwise.
async function assertAdvanceProducesMotion(proofMod, advance, readIndex, pageCount) {
  const startIndex = await readIndex();
  const seen = [startIndex];
  const trackingRead = async () => {
    const index = await readIndex();
    seen.push(index);
    return index;
  };
  await proofMod.pageAllAndVerifyWrap({
    pageCount,
    advance,
    readIndex: trackingRead,
    sleep: () => Promise.resolve(),
    settleMs: 0,
  });
  const sawMotion = seen.some((index) => index !== startIndex);
  return { sawMotion };
}

function inertAdvance(state) {
  return async () => {
    // Neutralized-receiver model: resolves without moving the index.
    state.calls += 1;
    return { nativeAdvancePage: true };
  };
}

function movingAdvance(state, pageCount) {
  return async () => {
    // Live-receiver model: advances one page and wraps at the end.
    state.calls += 1;
    state.index = (state.index + 1) % pageCount;
    return { nativeAdvancePage: true };
  };
}

function readStateIndex(state) {
  return async () => state.index;
}

function advanceReceiverBlock() {
  const text = readFileSync(MAIN_ACTIVITY, 'utf8');
  const start = text.indexOf('advanceReceiver = new BroadcastReceiver');
  assert.notEqual(start, -1, 'MainActivity.java must still register advanceReceiver');
  const end = text.indexOf('themeReceiver', start);
  assert.notEqual(end, -1, 'MainActivity.java must still contain themeReceiver after advanceReceiver');
  return text.slice(start, end);
}

describe('T2 assertAdvanceProducesMotion proof advance leg (AC-M3-020)', () => {
  it('inert advance stub rejects counts-mismatch / no-motion (fail-closed F6 guard)', async () => {
    const proof = requireProof();
    const state = { index: 0, calls: 0 };
    await assert.rejects(
      () => assertAdvanceProducesMotion(proof, inertAdvance(state), readStateIndex(state), 3),
      (err) => {
        assert.equal(err.code, 'counts-mismatch');
        assert.match(err.message, /no motion/);
        return true;
      },
      'an inert advance path must never pass vacuously',
    );
    assert.equal(state.calls, 3, 'all pages attempted before the guard rejects');
  });

  it('moving stub resolves with sawMotion true and correct wrap', async () => {
    const proof = requireProof();
    const pageCount = 3;
    const state = { index: 0, calls: 0 };
    const result = await assertAdvanceProducesMotion(
      proof,
      movingAdvance(state, pageCount),
      readStateIndex(state),
      pageCount,
    );
    assert.deepEqual(result, { sawMotion: true });
    assert.equal(state.calls, pageCount);
    assert.equal(state.index, 0, 'a full cycle wraps back to the start index');
  });

  it('advance that moves but never wraps rejects counts-mismatch (wrap mismatch)', async () => {
    const proof = requireProof();
    const state = { index: 0, calls: 0 };
    const driftingAdvance = async () => {
      state.calls += 1;
      state.index += 1; // moves every time, never wraps back to start
      return { nativeAdvancePage: true };
    };
    await assert.rejects(
      () => assertAdvanceProducesMotion(proof, driftingAdvance, readStateIndex(state), 3),
      (err) => {
        assert.equal(err.code, 'counts-mismatch');
        assert.match(err.message, /wrap mismatch/);
        return true;
      },
    );
  });

  it('single-page gallery resolves without requiring motion (pageCount <= 1 exemption)', async () => {
    const proof = requireProof();
    const state = { index: 0, calls: 0 };
    const result = await assertAdvanceProducesMotion(
      proof,
      inertAdvance(state),
      readStateIndex(state),
      1,
    );
    assert.deepEqual(result, { sawMotion: false });
  });

  it('production pageAllAndVerifyWrap keeps its fail-closed no-motion guard (no logic change)', () => {
    const text = readFileSync(PROOF_LIB, 'utf8');
    assert.match(text, /page advance showed no motion/, 'F6 dead-advance guard must stay in production');
    assert.match(text, /page wrap mismatch/, 'wrap guard must stay in production');
    assert.match(text, /if \(pageCount > 1 && !sawMotion\)/, 'motion requirement shape must stay intact');
  });

  it('advanceReceiver re-armed: onReceive calls nativeAdvancePage(engineHandle) [RED pre-T4]', () => {
    const block = advanceReceiverBlock();
    assert.match(
      block,
      /nativeAdvancePage\s*\(\s*engineHandle\s*\)/,
      'neutralized receiver must be re-armed so the proof advance path moves the native pager',
    );
  });

  it('proof default advance still targets the NATIVE_ADVANCE_PAGE broadcast entry', () => {
    const text = readFileSync(PROOF_LIB, 'utf8');
    assert.match(text, /muse\.native\.NATIVE_ADVANCE_PAGE/, 'proof advance entry action unchanged');
  });
});
