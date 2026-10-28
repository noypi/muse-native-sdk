'use strict';
// Profiler frame timeline with per-pass cost + jank attribution
// (FR-DX-004 / AC-DX-015 / TC-DX-015).
// In-memory session log of frames shaped
//   { sessionId, passes: [{ name, subsystem, costMs }] }
// where `subsystem` is a verbatim caller label (engine/js/gpu/platform).
// attributeJank reports, per janky frame, the single slowest pass whose
// costMs exceeds the caller-supplied budgetMs, with exact
// overByMs = costMs - budgetMs. Frames at or under budget are unattributed.
// Sessions are isolated; an unknown sessionId yields [].
// attributeJank reports only frames recorded since the previous
// attributeJank call for that session (per-session cursor), so repeated
// polls do not re-report the same jank. The underlying log is retained.

const frames = [];
const cursors = new Map();
let nextId = 1;

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function malformedError(detail) {
  return new Error(`malformed profiler frame (DX-004 / AC-DX-015): ${detail}`);
}

function validateFrame(frame) {
  if (!isPlainObject(frame)) throw malformedError('frame must be an object');
  if (typeof frame.sessionId !== 'string' || frame.sessionId.length === 0) {
    throw malformedError('sessionId must be a non-empty string');
  }
  if (!Array.isArray(frame.passes) || frame.passes.length === 0) {
    throw malformedError('passes must be a non-empty array');
  }
  for (const p of frame.passes) {
    if (!isPlainObject(p)) throw malformedError('each pass must be an object');
    if (typeof p.name !== 'string' || p.name.length === 0) {
      throw malformedError('pass.name must be a non-empty string');
    }
    if (typeof p.subsystem !== 'string' || p.subsystem.length === 0) {
      throw malformedError('pass.subsystem must be a non-empty string');
    }
    if (typeof p.costMs !== 'number' || !Number.isFinite(p.costMs) || p.costMs < 0) {
      throw malformedError('pass.costMs must be a finite number >= 0');
    }
  }
}

function recordFrame(frame) {
  validateFrame(frame);
  const id = `frame-${nextId}`;
  nextId += 1;
  frames.push({
    id,
    sessionId: frame.sessionId,
    passes: frame.passes.map((p) => ({ name: p.name, subsystem: p.subsystem, costMs: p.costMs })),
  });
  return { id };
}

function attributeJank(sessionId, budgetMs) {
  if (typeof sessionId !== 'string' || sessionId.length === 0) return [];
  if (typeof budgetMs !== 'number' || !Number.isFinite(budgetMs) || budgetMs < 0) {
    throw malformedError('budgetMs must be a finite number >= 0');
  }
  const start = cursors.get(sessionId) ?? 0;
  const out = [];
  for (let i = start; i < frames.length; i += 1) {
    const f = frames[i];
    if (f.sessionId !== sessionId) continue;
    let slowest = null;
    for (const p of f.passes) {
      if (slowest === null || p.costMs > slowest.costMs) slowest = p;
    }
    if (slowest !== null && slowest.costMs > budgetMs) {
      out.push({
        frameId: f.id,
        slowPass: slowest.name,
        subsystem: slowest.subsystem,
        overByMs: slowest.costMs - budgetMs,
      });
    }
  }
  cursors.set(sessionId, frames.length);
  return out;
}

module.exports = { recordFrame, attributeJank };
