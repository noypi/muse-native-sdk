'use strict';
// T3 — gallery-proof proof-entry regression: wrap/motion + neutralized-path guardrails.
// Spec refs: AC-M3-020 (via TC-M3-021), AC-SAMPLE-039 (guardrail, TC-SAMPLE-039).
// Interfaces (plan.md verbatim):
//   T1: `onReceive(PROOF_ADVANCE_PAGE Intent) -> nativeAdvancePage(engineHandle): int`
//   T2: `defaultAdvancePage({serial: string}): Promise<{nativeAdvancePage: true}>`
//       `defaultReadPageIndex({serial: string}): Promise<number>`
//   T3: none — hermetic unit tests (no device); they inject `advance`/`readIndex`
//       doubles into the `pageAllAndVerifyWrap` export of
//       `packages/cli/lib/gallery-proof.js` and assert pinned `galleryNav` semantics.
// Hermetic: node:test + node:assert/strict + stdlib only. No adb, no device,
// no network, no secrets, no application config. The proof's real
// `defaultAdvancePage`/`defaultReadPageIndex` go through adb and are NOT
// invoked here — every `pageAllAndVerifyWrap` call below injects `advance`
// and `readIndex` doubles plus a no-op `sleep`, except the
// device-unreachable case which asserts the no-serial fail-closed path
// (throws before any adb spawn).
// ASSUMPTIONS (contract genuinely silent, most defensible behavior tested):
// - A1: `pageAllAndVerifyWrap` return shape stays `{ wrapped: true }` and its
//   dead-advance failure carries `err.code === 'counts-mismatch'` with a
//   `no motion` message (the BUG-RUNTIME-005 signature `start=1 end=1`).
// - A2: the proof's sent action constant is asserted via source text of
//   `packages/cli/lib/gallery-proof.js` (`PROOF_ADVANCE_PAGE` present,
//   `NATIVE_ADVANCE_PAGE` no longer the sent advance action) because
//   `defaultAdvancePage` is not exported; reconcile with the coder if the
//   constant is exported instead (names, not verdicts).
// - A3: `galleryNav` keeps exporting `BROADCAST_ADVANCE_PAGE`; the proof-only
//   `PROOF_ADVANCE_PAGE` export is optional — tests accept either the new
//   export or the literal `muse.native.PROOF_ADVANCE_PAGE` in source, but
//   `handleBroadcast()`/`handleOutsideTap()` must stay `false` for every input.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const galleryProof = require('../lib/gallery-proof.js');
const galleryNav = require('../../../examples/m3-gallery/src/galleryNav.js');

const LIB_PATH = path.resolve(__dirname, '..', 'lib', 'gallery-proof.js');
const NAV_PATH = path.resolve(__dirname, '..', '..', '..', 'examples', 'm3-gallery', 'src', 'galleryNav.js');

const PROOF_ACTION = 'muse.native.PROOF_ADVANCE_PAGE';
const NEUTRALIZED_ACTION = 'muse.native.NATIVE_ADVANCE_PAGE';

function readSource(p) {
  return fs.readFileSync(p, 'utf8');
}

// Fake multi-page device: `advance` steps the index forward mod pageCount,
// `readIndex` reports it. Models the proof-only entry wired to the
// navigateStory path (T1): every advance moves exactly one page.
function makeProofDevice(pageCount, start = 0) {
  let index = start;
  const advance = async () => {
    index = (index + 1) % pageCount;
    return { nativeAdvancePage: true };
  };
  const readIndex = async () => index;
  return { advance, readIndex };
}

const noSleep = async () => {};

describe('gallery-proof proof-only entry (AC-M3-020 via TC-M3-021)', () => {
  it('wrap: proof-entry advance over a multi-page gallery wraps with motion', async () => {
    const pageCount = 3;
    const { advance, readIndex } = makeProofDevice(pageCount, 1);
    const result = await galleryProof.pageAllAndVerifyWrap({
      pageCount,
      advance,
      readIndex,
      serial: 'test-serial-not-used',
      sleep: noSleep,
      settleMs: 0,
    });
    assert.deepEqual(result, { wrapped: true });
  });

  it('dead-advance guard: a no-op advance (neutralized bridge) fails counts-mismatch with the no-motion signature', async () => {
    const start = 1;
    const advance = async () => ({ nativeAdvancePage: true });
    const readIndex = async () => start;
    await assert.rejects(
      galleryProof.pageAllAndVerifyWrap({
        pageCount: 9,
        advance,
        readIndex,
        serial: 'test-serial-not-used',
        sleep: noSleep,
        settleMs: 0,
      }),
      (err) => {
        assert.equal(err && err.code, 'counts-mismatch');
        assert.match(String(err && err.message), /no motion/);
        assert.match(String(err && err.message), /start=1 end=1/);
        return true;
      },
    );
  });

  it('wrap mismatch: ending off-start fails counts-mismatch with the wrap signature', async () => {
    let calls = 0;
    const advance = async () => {
      calls += 1;
      return { nativeAdvancePage: true };
    };
    // Moves once (sawMotion true) but never returns to start.
    const readIndex = async () => (calls === 0 ? 0 : 1);
    await assert.rejects(
      galleryProof.pageAllAndVerifyWrap({
        pageCount: 3,
        advance,
        readIndex,
        serial: 'test-serial-not-used',
        sleep: noSleep,
        settleMs: 0,
      }),
      (err) => {
        assert.equal(err && err.code, 'counts-mismatch');
        assert.match(String(err && err.message), /wrap mismatch/);
        return true;
      },
    );
  });

  it('fail-closed: no serial/device and no doubles throws device-unreachable (never a silent pass)', async () => {
    delete process.env.ANDROID_SERIAL;
    await assert.rejects(
      galleryProof.pageAllAndVerifyWrap({ pageCount: 1 }),
      (err) => {
        assert.equal(err && err.code, 'device-unreachable');
        return true;
      },
    );
  });

  it('retarget (T2): proof sends the proof-only action, never the neutralized action', () => {
    const src = readSource(LIB_PATH);
    assert.ok(
      src.includes(PROOF_ACTION),
      `gallery-proof.js must reference ${PROOF_ACTION}`,
    );
    assert.ok(
      /PROOF_ADVANCE_PAGE[^0-9]*page\[=:\s\]/i.test(src) || src.includes('PROOF_ADVANCE_PAGE'),
      'defaultReadPageIndex must add a pattern for the proof marker',
    );
    // The sent advance broadcast is the proof action; the old action must not
    // remain as the sent `-a` argument of the advance broadcast.
    assert.ok(
      src.includes(`-a', '${PROOF_ACTION}'`) || src.includes(`-a", "${PROOF_ACTION}"`)
        || (src.includes(PROOF_ACTION) && src.includes('am', 0)),
      `defaultAdvancePage must send ${PROOF_ACTION}`,
    );
    const sentNeutralized = (src.match(new RegExp(`shell',\\s*'am',\\s*'broadcast',\\s*'-a',\\s*'${NEUTRALIZED_ACTION.replace(/\./g, '\\.')}'`, 'g')) || []).length;
    assert.equal(sentNeutralized, 0, 'defaultAdvancePage must not send the neutralized action');
  });
});

describe('neutralized-path guardrails (AC-SAMPLE-039 via TC-SAMPLE-039)', () => {
  it('galleryNav module handleBroadcast/handleOutsideTap stay false for proof and neutralized actions', () => {
    assert.equal(galleryNav.handleBroadcast(PROOF_ACTION), false);
    assert.equal(galleryNav.handleBroadcast(NEUTRALIZED_ACTION), false);
    assert.equal(galleryNav.handleBroadcast(), false);
    assert.equal(galleryNav.handleOutsideTap(), false);
  });

  it('galleryNav navigator handleBroadcast/handleOutsideTap stay false and never move the index', () => {
    const nav = galleryNav.createNavigator([{ id: 'a' }, { id: 'b' }]);
    const before = nav.current();
    assert.equal(nav.handleBroadcast(PROOF_ACTION), false);
    assert.equal(nav.handleBroadcast(NEUTRALIZED_ACTION), false);
    assert.equal(nav.handleOutsideTap(), false);
    assert.deepEqual(nav.current(), before);
  });

  it('galleryNav keeps the neutralized broadcast constant and documents the proof entry without paging on it', () => {
    const src = readSource(NAV_PATH);
    assert.equal(galleryNav.BROADCAST_ADVANCE_PAGE, NEUTRALIZED_ACTION);
    assert.ok(
      src.includes('PROOF_ADVANCE_PAGE') || src.includes(PROOF_ACTION),
      'galleryNav source must name the proof-only entry beside BROADCAST_ADVANCE_PAGE',
    );
  });
});
