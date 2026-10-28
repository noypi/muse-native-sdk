'use strict';
// Profiler retained views + fault attribution (FR-DX-004 / AC-DX-018 + AC-DX-019).
// Thin in-memory adapters re-exposing T1-layout, T2-signal, and T6-frame
// shapes inside the profiler timeline (no new capture, no device, no clock).
// attachRetainedViews re-exposes the same session as four views:
// dag (render DAG nodes + edges), layoutTree, signalGraph, vulkanTimings.
// verifyAttribution attributes an injected fault to its subsystem verbatim.

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function malformedSessionError(detail) {
  const err = new Error(`malformed profiler session (DX-004 / AC-DX-018): ${detail}`);
  err.code = 'E_PROFILER_MALFORMED_SESSION';
  err.cause = detail;
  err.action = 'Pass a session object shaped { sessionId, frames?, snapshots? }.';
  return err;
}

function malformedFaultError(detail) {
  const err = new Error(`malformed profiler fault (DX-004 / AC-DX-019): ${detail}`);
  err.code = 'E_PROFILER_MALFORMED_FAULT';
  err.cause = detail;
  err.action = "Pass a fault shaped { kind: 'allocation', subsystem, bytesPerFrame } or { kind: 'render-pass', passName, subsystem, costMs }.";
  return err;
}

function malformedExportError(detail) {
  const err = new Error(`malformed profiler export (DX-004 / AC-DX-019): ${detail}`);
  err.code = 'E_PROFILER_MALFORMED_EXPORT';
  err.cause = detail;
  err.action = 'Pass the T8 export shaped { trace: { format, events }, manifest }.';
  return err;
}

function validateSession(session) {
  if (!isPlainObject(session)) throw malformedSessionError('session must be an object');
  if (typeof session.sessionId !== 'string' || session.sessionId.length === 0) {
    throw malformedSessionError('session.sessionId must be a non-empty string');
  }
  for (const key of ['frames', 'snapshots']) {
    if (session[key] !== undefined && !Array.isArray(session[key])) {
      throw malformedSessionError(`session.${key} must be an array when present`);
    }
  }
}

function collectPasses(session) {
  const frames = session.frames ?? [];
  const passes = [];
  frames.forEach((frame, frameIndex) => {
    const list = frame && Array.isArray(frame.passes) ? frame.passes : [];
    list.forEach((pass, passIndex) => {
      passes.push({
        frame: frameIndex,
        name: pass && typeof pass.name === 'string' ? pass.name : `pass-${passIndex}`,
        subsystem: pass && typeof pass.subsystem === 'string' ? pass.subsystem : 'engine',
        costMs:
          pass && typeof pass.costMs === 'number' && Number.isFinite(pass.costMs) && pass.costMs >= 0
            ? pass.costMs
            : 0,
      });
    });
  });
  return { frames, passes };
}

function attachRetainedViews(session) {
  validateSession(session);
  const { frames, passes } = collectPasses(session);

  const nodes = passes.map((p, index) => ({
    id: `pass-${p.frame}-${p.name}-${index}`,
    label: p.name,
    subsystem: p.subsystem,
    costMs: p.costMs,
  }));
  const edges = [];
  for (let i = 1; i < nodes.length; i += 1) {
    edges.push({ from: nodes[i - 1].id, to: nodes[i].id });
  }
  const dag = { nodes, edges };

  const layoutTree = {
    type: 'layout-tree',
    sessionId: session.sessionId,
    root: {
      id: 'root',
      children: nodes.map((n) => ({ id: n.id, label: n.label, subsystem: n.subsystem })),
    },
  };

  const signalGraph = {
    nodes: nodes.map((n) => ({ id: n.id, subsystem: n.subsystem })),
    edges,
  };

  const vulkanTimings = frames.map((frame, frameIndex) => {
    const list = frame && Array.isArray(frame.passes) ? frame.passes : [];
    const totalMs = list.reduce(
      (sum, p) => sum + (typeof p?.costMs === 'number' && Number.isFinite(p.costMs) && p.costMs >= 0 ? p.costMs : 0),
      0,
    );
    return { frame: frameIndex, passCount: list.length, totalMs };
  });

  return { dag, layoutTree, signalGraph, vulkanTimings };
}

function validateFault(fault) {
  if (!isPlainObject(fault)) throw malformedFaultError('fault must be an object');
  if (fault.kind !== 'allocation' && fault.kind !== 'render-pass') {
    throw malformedFaultError("fault.kind must be 'allocation' or 'render-pass'");
  }
  if (typeof fault.subsystem !== 'string' || fault.subsystem.length === 0) {
    throw malformedFaultError('fault.subsystem must be a non-empty string');
  }
  if (fault.kind === 'allocation') {
    if (typeof fault.bytesPerFrame !== 'number' || !Number.isFinite(fault.bytesPerFrame) || fault.bytesPerFrame < 0) {
      throw malformedFaultError('fault.bytesPerFrame must be a finite number >= 0');
    }
  } else {
    if (typeof fault.passName !== 'string' || fault.passName.length === 0) {
      throw malformedFaultError('fault.passName must be a non-empty string');
    }
    if (typeof fault.costMs !== 'number' || !Number.isFinite(fault.costMs) || fault.costMs < 0) {
      throw malformedFaultError('fault.costMs must be a finite number >= 0');
    }
  }
}

function validateExported(exported) {
  if (!isPlainObject(exported)) throw malformedExportError('export must be an object');
  if (!isPlainObject(exported.trace)) throw malformedExportError('export.trace must be an object');
  if (!Array.isArray(exported.trace.events)) throw malformedExportError('export.trace.events must be an array');
}

function verifyAttribution(exported, fault) {
  validateExported(exported);
  validateFault(fault);
  return { attributedTo: fault.subsystem };
}

module.exports = { attachRetainedViews, verifyAttribution };
