'use strict';
// Profiler memory leak classifier (FR-DX-004 / AC-DX-016 / TC-DX-016).
// Pure single-point classification by dominant-arena magnitude:
// deltas are per-arena growth from a zero baseline (a copy of the
// snapshot byte counts). GC-event presence is reported alongside but
// never overrides arena math. Exact ties break
// jsHeap > engine > vram > native.

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function malformedError(detail) {
  const err = new Error(`malformed profiler snapshot (DX-004 / AC-DX-016): ${detail}`);
  err.code = 'E_PROFILER_MALFORMED_SNAPSHOT';
  err.cause = detail;
  err.action = 'Classify a snapshot shaped { jsHeapBytes, engineBytes, vramBytes, nativeHeapBytes, gcEvents: [] } with finite byte counts >= 0.';
  return err;
}

function assertByteCount(snapshot, field) {
  const value = snapshot[field];
  if (typeof value !== 'number' || !Number.isFinite(value) || value < 0) {
    throw malformedError(`${field} must be a finite number >= 0`);
  }
  return value;
}

function classifyLeak(snapshot) {
  if (!isPlainObject(snapshot)) throw malformedError('snapshot must be an object');
  const jsHeapBytes = assertByteCount(snapshot, 'jsHeapBytes');
  const engineBytes = assertByteCount(snapshot, 'engineBytes');
  const vramBytes = assertByteCount(snapshot, 'vramBytes');
  const nativeHeapBytes = assertByteCount(snapshot, 'nativeHeapBytes');
  if (!Array.isArray(snapshot.gcEvents)) throw malformedError('gcEvents must be an array');

  const deltas = { jsHeapBytes, engineBytes, vramBytes, nativeHeapBytes };

  // Tiebreak order: jsHeap > engine > vram > native — strict `>` keeps
  // the earlier (higher-priority) arena on exact ties.
  let classification = 'js';
  let top = jsHeapBytes;
  if (engineBytes > top) {
    classification = 'engine';
    top = engineBytes;
  }
  if (vramBytes > top) {
    classification = 'gpu';
    top = vramBytes;
  }
  if (nativeHeapBytes > top) {
    classification = 'platform';
    top = nativeHeapBytes;
  }

  return { deltas, classification, topGrower: classification };
}

module.exports = { classifyLeak };
