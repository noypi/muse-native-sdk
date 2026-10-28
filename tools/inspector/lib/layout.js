'use strict';
// Layout inspector view (FR-DX-003 / AC-DX-010 / TC-DX-010).
// Read-only view over an injected LayoutSource seam
// (getConstraintInput / getComputedBox / getWinningDeclaration).
// Unknown node rejects with a structured NFR-DX-002 diagnostic.

const BLOCKED_IDS = new Set(['__proto__', 'prototype', 'constructor']);

function unknownNodeError() {
  const err = new Error('unknown layout node in inspector view (DX-003 / AC-DX-010)');
  err.code = 'E_INSPECTOR_UNKNOWN_NODE';
  err.cause = 'unknown layout node in inspector view (DX-003 / AC-DX-010)';
  err.action = 'Select a known node id from the inspector tree (DX-003).';
  return err;
}

function describeLayout(nodeId, source) {
  if (typeof nodeId !== 'string' || nodeId.length === 0 || BLOCKED_IDS.has(nodeId)) {
    throw unknownNodeError();
  }
  if (!source || typeof source.getConstraintInput !== 'function'
    || typeof source.getComputedBox !== 'function'
    || typeof source.getWinningDeclaration !== 'function') {
    throw unknownNodeError();
  }
  const constraintInput = source.getConstraintInput(nodeId);
  const computedBox = source.getComputedBox(nodeId);
  const winning = source.getWinningDeclaration(nodeId);
  if (constraintInput === undefined || computedBox === undefined || winning === undefined) {
    throw unknownNodeError();
  }
  const box = {
    x: computedBox.x,
    y: computedBox.y,
    width: computedBox.width,
    height: computedBox.height,
  };
  return {
    nodeId,
    constraintInput: { ...constraintInput },
    computedBox: box,
    styleProvenance: { declaration: winning.declaration, origin: winning.origin },
  };
}

module.exports = { describeLayout };
