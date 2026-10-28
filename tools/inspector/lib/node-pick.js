'use strict';
// Node pick composition (FR-DX-003 / AC-DX-014 / TC-DX-014).
// Composes the T1 layout seam (getComputedBox / getWinningDeclaration) with
// the T2 signal adapter (`signalOf`) into a single read-only pick. No
// logging, no device, no dev-channel access. Unknown node rejects with a
// structured NFR-DX-002 diagnostic.

const BLOCKED_IDS = new Set(['__proto__', 'prototype', 'constructor']);

function unknownNodeError() {
  const err = new Error('unknown node in inspector pick (DX-003 / AC-DX-014)');
  err.code = 'E_INSPECTOR_UNKNOWN_NODE';
  err.cause = 'unknown node in inspector pick (DX-003 / AC-DX-014)';
  err.action = 'Select a known node id from the inspector tree (DX-003).';
  return err;
}

function pickNode(nodeId, deps) {
  if (typeof nodeId !== 'string' || nodeId.length === 0 || BLOCKED_IDS.has(nodeId)) {
    throw unknownNodeError();
  }
  const layoutSource = deps && deps.layoutSource;
  const signalOf = deps && deps.signalOf;
  if (
    !layoutSource
    || typeof layoutSource.getComputedBox !== 'function'
    || typeof layoutSource.getWinningDeclaration !== 'function'
    || typeof signalOf !== 'function'
  ) {
    throw unknownNodeError();
  }
  const computedBox = layoutSource.getComputedBox(nodeId);
  const winning = layoutSource.getWinningDeclaration(nodeId);
  if (computedBox === undefined || winning === undefined) {
    throw unknownNodeError();
  }
  let lastInvalidation = null;
  try {
    const mapped = signalOf(nodeId);
    if (mapped !== undefined && mapped !== null) {
      lastInvalidation = { handle: mapped.handle, atSeq: mapped.atSeq };
    }
  } catch (e) {
    void e;
    lastInvalidation = null;
  }
  return {
    nodeId,
    computedBox: {
      x: computedBox.x,
      y: computedBox.y,
      width: computedBox.width,
      height: computedBox.height,
    },
    winningDeclaration: { declaration: winning.declaration, origin: winning.origin },
    lastInvalidation,
  };
}

module.exports = { pickNode };
