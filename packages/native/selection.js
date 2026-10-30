'use strict';
// AC-M3-023 (T1) — `Checkbox` selection-handle surface over injected deps.
// Constructor-injected state only: the initial `checked` value, the optional
// two-way `binding` (FR-TSX-006 legality, FR-MCORE-007 write-through) and the
// optional `onCheckedChange` listener all travel in `deps`, mirroring the
// `createTextInput(deps)` DI pattern in `packages/native/textinput.js`. No
// engine logic lives here and no engine paths are imported.

function pushBinding(binding, value) {
  if (binding === undefined || binding === null) {
    return;
  }
  try {
    if (typeof binding.set === 'function') {
      binding.set(value);
      return;
    }
  } catch (_) {
    // Fall through to the `value` property write below.
  }
  try {
    binding.value = value;
  } catch (_) {
    // A read-only binding must never break the tap path.
  }
}

function notifyCheckedChange(listener, value) {
  if (typeof listener !== 'function') {
    return;
  }
  try {
    listener(value);
  } catch (_) {
    // A throwing listener must never break the tap path.
  }
}

function createCheckbox(deps) {
  const safe = deps === undefined || deps === null ? {} : deps;
  // Initial state is coerced strictly: only `checked === true` starts
  // checked, so truthy non-boolean input cannot leak into the handle.
  let checked = safe.checked === true;
  // `enabled: false` means disabled; absent `enabled` means enabled
  // (consistent with the existing `Switch`/`Slider` convention).
  const enabled = safe.enabled !== false;
  const binding = safe.binding;
  const onCheckedChange =
    typeof safe.onCheckedChange === 'function' ? safe.onCheckedChange : null;

  // Enabled tap flips `checked`, writes through to the binding, notifies
  // the listener best-effort, and reports a one-frame property-update
  // repaint with no re-instantiation. Disabled tap changes nothing and
  // touches neither the binding nor the listener. Never throws.
  function tap() {
    if (!enabled) {
      return { checked, repainted: 0, reinstantiated: 0, frames: 1 };
    }
    checked = !checked;
    pushBinding(binding, checked);
    notifyCheckedChange(onCheckedChange, checked);
    return { checked, repainted: 1, reinstantiated: 0, frames: 1 };
  }

  const handle = { tap };

  Object.defineProperty(handle, 'checked', {
    enumerable: true,
    get() {
      return checked;
    },
  });

  return handle;
}

function notifySelectedChange(listener, value) {
  if (typeof listener !== 'function') {
    return;
  }
  try {
    listener(value);
  } catch (_) {
    // A throwing listener must never break the tap path.
  }
}

// AC-M3-023 (T2) — `Radio` selection-handle surface over injected deps.
// Constructor-injected state only: the initial `selected` value, the optional
// two-way `binding` and the optional `onSelectedChange` listener all travel in
// `deps`, mirroring `createCheckbox` above. No engine logic lives here and no
// engine paths are imported.
function createRadio(deps) {
  const safe = deps === undefined || deps === null ? {} : deps;
  // Initial state is coerced strictly: only `selected === true` starts
  // selected, so truthy non-boolean input cannot leak into the handle.
  let selected = safe.selected === true;
  // `enabled: false` means disabled; absent `enabled` means enabled
  // (consistent with the existing `Switch`/`Slider` convention).
  const enabled = safe.enabled !== false;
  const binding = safe.binding;
  const onSelectedChange =
    typeof safe.onSelectedChange === 'function' ? safe.onSelectedChange : null;

  // Enabled tap sets `selected` to `true` (idempotent when already
  // selected), writes through to the binding, notifies the listener
  // best-effort, and reports a one-frame property-update repaint with no
  // re-instantiation. Disabled tap changes nothing and touches neither the
  // binding nor the listener. Never throws.
  function tap() {
    if (!enabled) {
      return { selected, repainted: 0, reinstantiated: 0, frames: 1 };
    }
    selected = true;
    pushBinding(binding, selected);
    notifySelectedChange(onSelectedChange, selected);
    return { selected, repainted: 1, reinstantiated: 0, frames: 1 };
  }

  const handle = { tap };

  Object.defineProperty(handle, 'selected', {
    enumerable: true,
    get() {
      return selected;
    },
  });

  return handle;
}

module.exports = { createCheckbox, createRadio };
