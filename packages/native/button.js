'use strict';
// FR-M3-013 / AC-M3-033 + AC-M3-036 — fail-closed Button event-handle surface
// (T4 canonical delivery-lane copy; combined press contract per plan T7).
//
// Real adapter over injected deps only: `createButton({ enabled, onPress })`
// mints single-use lease tickets (`{ tapId }`) from a per-button pending
// registry and delivers them fail-closed through `press(lease)`:
// fire-once (consume-before-notify), drag/slop/CANCEL revocation silent,
// foreign-handle leases silent, disabled inert (never mints, delivery-time
// veto via `setEnabled`), throwing listeners contained and recorded on the
// result (`errors`), one-frame repaint with no re-instantiation
// (`{ fired, frames: 1, reinstantiated: 0 }`). Forged leases (never minted
// by any handle) reject honestly with a `TypeError` — never a silent fake.
//
// Arbitration is exactly-once per tap: consumption is recorded in a
// module-global idempotency-key set, so one tapId presented to two handles
// fires once total. The module-global sets hold idempotency keys only —
// never component state and never a lease registry; the pending lease
// registry itself is scoped per-button (one Map per handle, created by
// `createButton`, emptied by `dispose()`). Lifecycle: `revoke(lease)`
// spends a live lease (drag/slop/CANCEL path), `dispose()` models unmount
// by spending + dropping every pending lease of that handle (press after
// dispose is silent, per the unmounted-target rule), and
// `resetButtonTapState()` resets the tapId source + both key sets (test and
// hot-reload boundary — the only operation that clears global keys, so a
// session's key growth is bounded by taps-since-reset).
//
// tapId IDENTITY (ASSUMPTION BRIDGE-1 — half settled, half open):
// AC-INPUT-024 settles that JNI carries only the `tapId` integer identity
// across the boundary (never a raw pointer). The JS-lane ticket spelling is
// the test-pinned opaque string (`tap-N`); ingress (`press`,
// `foldButtonFrames`, `onButtonPress`) additionally accepts the integer
// identity so a bridge-carried ticket is never misrejected as forged. The
// canonical JS spelling of a bridge-minted ticket is UNSETTLED — flagged to
// specs-creator; this file invents no second ticket truth (one opaque
// `{ tapId }` shape throughout).
//
// Disabled-at-delivery is a veto, NOT a spend (no-consume): a tap delivered
// while disabled fires nothing and the lease stays live, so re-enabling and
// pressing the same lease may still fire. Terminal states (fired, revoked,
// no-listener delivery) spend the lease: exactly-once per tapId, replays
// silent. `onButtonPress(query)` is a pure preview, not a delivery: an
// enabled query with a live (minted, unspent, unrevoked) tap previews 1 and
// anything else previews 0; it consumes nothing, repaints nothing, and
// carries no coordinates. Agreement with `press()` is exact for fresh leases
// on enabled handles (the pinned parity case); without a ticket it answers
// the coarse "would an enabled handle fire" (1/0 by `enabled`).
//
// The minimal surface exposes no `pressed` prop and touches no binding-like
// surface (Button has no two-way prop; write-through N/A per contract M-3 —
// the write-through pattern itself lives in selection.js Checkbox/Radio).
// No engine logic lives here and no engine paths are imported.

// Resettable tapId source: monotonic per-process counter. Growth of the
// global key sets is bounded by taps-since-`resetButtonTapState()`; callers
// that mint in a loop without delivery must `dispose()` the handle.
let nextTapId = 1;
const mintedTapIds = new Set();
const consumedTapIds = new Set();

function tapIdOf(lease) {
  if (lease !== null && typeof lease === 'object') return lease.tapId;
  return lease;
}

function isKnownTapId(tapId) {
  return (
    (typeof tapId === 'string' || typeof tapId === 'number') &&
    mintedTapIds.has(tapId)
  );
}

// Test / hot-reload boundary: clears both idempotency-key sets and restarts
// the tapId source. Production never calls this mid-session (it would
// re-arm spent leases); suites call it to bound key growth between cases.
function resetButtonTapState() {
  nextTapId = 1;
  mintedTapIds.clear();
  consumedTapIds.clear();
}

function createButton(deps) {
  const safe = deps === undefined || deps === null ? {} : deps;
  // `enabled: false` means disabled; absent `enabled` means enabled
  // (consistent with the selection.js Checkbox/Radio convention).
  let enabled = safe.enabled !== false;
  const onPress = typeof safe.onPress === 'function' ? safe.onPress : null;
  // Per-button pending registry (FR-M3-013 Notes): tapId -> lease for
  // leases minted by this handle and not yet terminally spent here.
  const pending = new Map();
  let disposed = false;

  // Spends a pending lease without firing: removes it from this handle's
  // registry and records it consumed so no replay (here or cross-handle)
  // can ever fire it.
  function spend(tapId) {
    pending.delete(tapId);
    consumedTapIds.add(tapId);
  }

  // Disabled-never-mint (AC-M3-036): a disabled button mints no lease.
  function mint() {
    if (!enabled || disposed) {
      return null;
    }
    const tapId = `tap-${nextTapId++}`;
    const lease = { tapId };
    mintedTapIds.add(tapId);
    pending.set(tapId, lease);
    return lease;
  }

  // Delivery-time veto path (contract EN-1): `enabled` is re-checkable at
  // delivery, so disabling before press vetoes the fire.
  function setEnabled(value) {
    enabled = value !== false;
  }

  // Drag/slop/CANCEL path: revokes a live lease minted by this handle. The
  // lease is spent (never fires again); a forged or foreign lease rejects
  // with the same honest `TypeError` as `press()`.
  function revoke(lease) {
    const tapId = tapIdOf(lease);
    if (!isKnownTapId(tapId) || !pending.has(tapId)) {
      throw new TypeError('Unknown lease: revoke requires a live lease minted by this Button handle');
    }
    if (lease !== null && typeof lease === 'object') {
      lease.revoked = lease.revoked === undefined ? true : lease.revoked;
    }
    spend(tapId);
    return { fired: 0, frames: 1, reinstantiated: 0 };
  }

  // Unmount path (AC-INPUT-024 entry-deleted-on-unmount): spends + drops
  // every pending lease of this handle so no stale ticket can fire a
  // remounted handle, and marks the handle so later `press()` is silent
  // (unmounted-target silent) and later `mint()` returns null.
  function dispose() {
    for (const tapId of pending.keys()) {
      consumedTapIds.add(tapId);
    }
    pending.clear();
    disposed = true;
    return { fired: 0, frames: 1, reinstantiated: 0 };
  }

  function press(lease) {
    const tapId = tapIdOf(lease);
    // Forgery rejects honestly: only leases minted by a real handle carry a
    // known tapId (string JS-lane spelling or integer bridge identity per
    // ASSUMPTION BRIDGE-1). A forged ticket must never become a live fire.
    if (!isKnownTapId(tapId)) {
      throw new TypeError('Unknown lease: press requires a lease minted by a Button handle');
    }
    // Unmounted handle is silent (entry deleted on unmount).
    if (disposed) {
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    // Disabled at delivery vetoes the fire but does NOT spend the lease
    // (no-consume): re-enabling then pressing the same lease may still fire.
    if (!enabled) {
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    // A drag/slop/CANCEL-revoked lease never fires tap (drag-no-fire); the
    // revoked ticket is terminally spent.
    const leaseObj = lease !== null && typeof lease === 'object' ? lease : null;
    if (leaseObj !== null && leaseObj.revoked) {
      spend(tapId);
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    // Outside-silent + fire-once: a lease this handle did not mint, or one
    // already consumed (replay, reentrancy, or arbitration won elsewhere),
    // fires nothing here.
    if (!pending.has(tapId) || consumedTapIds.has(tapId)) {
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    // Consume-before-notify: the tap is spent before the listener runs, so
    // reentrant press of the same lease and cross-handle arbitration each
    // fire at most once in total.
    spend(tapId);
    // No declared onPress means there is nothing to fire (an undeclared
    // event key such as onTap never becomes a live listener).
    if (onPress === null) {
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    try {
      onPress();
    } catch (err) {
      // A throwing listener is recorded on the result and never escapes
      // press(); the tap still counts as fired exactly once.
      return { fired: 1, frames: 1, reinstantiated: 0, errors: [err] };
    }
    return { fired: 1, frames: 1, reinstantiated: 0 };
  }

  return { mint, press, setEnabled, revoke, dispose };
}

// Pure fold over raw frames (contract FR-1): maps any raw frame list to
// exactly 0|1. A frame counts as a tap when it carries `tap === true`, or
// when it carries a lease `tapId` (string JS-lane spelling or integer
// bridge identity) with no cancel/drag marker; revoked, cancelled, or
// dragged frames fold to 0. Agrees with press() on the same taps
// (parity): any tapped frame folds to 1, an untapped stream to 0.
function foldButtonFrames(rawFrames) {
  const frames = Array.isArray(rawFrames) ? rawFrames : [];
  for (const frame of frames) {
    if (frame === undefined || frame === null || typeof frame !== 'object') {
      continue;
    }
    if (frame.tap === true) {
      return 1;
    }
    if (frame.cancel === true || frame.drag === true || frame.revoked) {
      continue;
    }
    if (typeof frame.tapId === 'string' || typeof frame.tapId === 'number') {
      return 1;
    }
  }
  return 0;
}

// Pure preview agreeing with press() on the same tap: an enabled query
// carrying a live (minted, unspent, unrevoked) tap previews 1; a disabled
// query, or one carrying a forged/spent/revoked tap, previews 0. With no
// ticket it answers the coarse enabled question. Carries no coordinates
// and performs no consumption or repaint itself.
function onButtonPress(query) {
  const safe = query === undefined || query === null ? {} : query;
  if (safe.enabled === false) {
    return 0;
  }
  if (safe.revoked) {
    return 0;
  }
  const tapId = safe.tapId;
  if (tapId !== undefined) {
    if (!isKnownTapId(tapId) || consumedTapIds.has(tapId)) {
      return 0;
    }
  }
  return 1;
}

module.exports = { createButton, foldButtonFrames, onButtonPress, resetButtonTapState };
