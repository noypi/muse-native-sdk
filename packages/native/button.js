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
// Arbitration is exactly-once per tap: consumption is recorded in a
// module-global set, so one tapId presented to two handles fires once total.
// The minimal surface exposes no `pressed` prop and touches no binding-like
// surface (Button has no two-way prop; write-through N/A per contract M-3 —
// the write-through pattern itself lives in selection.js Checkbox/Radio).
// No engine logic lives here and no engine paths are imported.

let nextTapId = 1;
const mintedTapIds = new Set();
const consumedTapIds = new Set();

function createButton(deps) {
  const safe = deps === undefined || deps === null ? {} : deps;
  // `enabled: false` means disabled; absent `enabled` means enabled
  // (consistent with the selection.js Checkbox/Radio convention).
  let enabled = safe.enabled !== false;
  const onPress = typeof safe.onPress === 'function' ? safe.onPress : null;
  // Per-button pending registry (FR-M3-013 Notes): tapId -> lease for
  // leases minted by this handle and not yet consumed here.
  const pending = new Map();

  // Disabled-never-mint (AC-M3-036): a disabled button mints no lease.
  function mint() {
    if (!enabled) {
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

  function press(lease) {
    // Forgery rejects honestly: only leases minted by a real handle carry a
    // known tapId. A forged ticket must never become a live fire.
    if (
      lease === undefined ||
      lease === null ||
      typeof lease !== 'object' ||
      typeof lease.tapId !== 'string' ||
      !mintedTapIds.has(lease.tapId)
    ) {
      throw new TypeError('Unknown lease: press requires a lease minted by a Button handle');
    }
    // Disabled at delivery fires nothing (inert, touches no listener).
    if (!enabled) {
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    // A drag/slop/CANCEL-revoked lease never fires tap (drag-no-fire).
    if (lease.revoked) {
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    // Outside-silent + fire-once: a lease this handle did not mint, or one
    // already consumed (replay, reentrancy, or arbitration won elsewhere),
    // fires nothing here.
    if (!pending.has(lease.tapId) || consumedTapIds.has(lease.tapId)) {
      return { fired: 0, frames: 1, reinstantiated: 0 };
    }
    // Consume-before-notify: the tap is spent before the listener runs, so
    // reentrant press of the same lease and cross-handle arbitration each
    // fire at most once in total.
    pending.delete(lease.tapId);
    consumedTapIds.add(lease.tapId);
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

  return { mint, press, setEnabled };
}

// Pure fold over raw frames (contract FR-1): maps any raw frame list to
// exactly 0|1. A frame counts as a tap when it carries `tap === true`, or
// when it carries a lease `tapId` with no cancel/drag marker; revoked,
// cancelled, or dragged frames fold to 0. Agrees with press() on the same
// taps (parity): any tapped frame folds to 1, an untapped stream to 0.
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
    if (typeof frame.tapId === 'string') {
      return 1;
    }
  }
  return 0;
}

// Pure preview agreeing with press() on the same tap: an enabled handle
// with a tap fires once, a disabled handle fires nothing. Carries no
// coordinates and performs no consumption or repaint itself.
function onButtonPress(query) {
  const safe = query === undefined || query === null ? {} : query;
  if (safe.enabled === false) {
    return 0;
  }
  return 1;
}

module.exports = { createButton, foldButtonFrames, onButtonPress };
