'use strict';

function applyComponentRefresh(tree, edit) {
  const componentId = edit && edit.componentId;
  const target = (tree.components || []).find((c) => c.id === componentId);
  if (!target) {
    throw new Error(`Unknown component id: ${componentId}`);
  }

  target.output = edit.nextOutput;

  const preserved = (tree.components || [])
    .filter((c) => c.id !== componentId)
    .map((c) => c.id);

  const localState = target.localState || {};
  const signalKeys = new Set([
    ...Object.keys(target.signals || {}),
    ...Object.keys(tree.signals || {}),
  ]);
  const reset = Object.keys(localState).filter((k) => !signalKeys.has(k));

  if (target.localState && reset.length > 0) {
    for (const key of reset) {
      delete target.localState[key];
    }
  }

  const overlayNotice =
    reset.length > 0
      ? `Component '${componentId}' state reset: ${reset.join(', ')}`
      : null;

  return { preserved, reset, overlayNotice };
}

module.exports = { applyComponentRefresh };
