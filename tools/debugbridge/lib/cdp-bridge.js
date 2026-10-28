'use strict';
// Hermes CDP bridge (FR-DX-009 / AC-DX-039 / TC-DX-039).
// Dev-mode full-CDP via the Hermes dialect only (D2). Stacks are
// source-mapped via ./source-map-resolver; panels Hermes cannot serve
// reject with an explicit unsupported-method error naming DX-009.

const { mapCallFrames } = require('./source-map-resolver');

const UNSUPPORTED_CAUSE = 'unsupported by Hermes CDP';
const UNSUPPORTED_ACTION =
  'Use the DX-009 dev-mode fallback: this Hermes panel is unavailable (DX-009).';

function unsupportedError(command) {
  return { command, cause: UNSUPPORTED_CAUSE, action: UNSUPPORTED_ACTION };
}

function dialectError(dialect) {
  return {
    command: 'createCdpBridge',
    cause: `unsupported dialect "${dialect}": Hermes dialect only (DX-009)`,
    action: UNSUPPORTED_ACTION,
    dialect,
  };
}

function isUnsupportedTransportError(err) {
  if (!err) return false;
  if (err.code === 'UNSUPPORTED_BY_HERMES') return true;
  const msg = typeof err.message === 'string' ? err.message : '';
  return /unsupported by Hermes CDP/i.test(msg);
}

function normalizeTransportError(command, err) {
  if (isUnsupportedTransportError(err)) return unsupportedError(err.command || command);
  throw err;
}

function unsupportedMarkerCommand(params) {
  const expr =
    params && typeof params.expression === 'string' ? params.expression : null;
  if (!expr) return null;
  const prefix = '@unsupported-panel:';
  if (!expr.startsWith(prefix)) return null;
  const command = expr.slice(prefix.length).trim();
  return command || 'unknown';
}

function createCdpBridge(opts) {
  const options = opts || {};
  if (options.dialect !== 'hermes') {
    throw dialectError(options.dialect);
  }

  let transport = options.transport || null;
  let sourceMap = options.sourceMap || null;

  async function send(command, params) {
    if (!transport || typeof transport.send !== 'function') {
      throw {
        command,
        cause: 'no CDP transport configured',
        action: 'Wire a CDP transport via the `transport` option or __setTransport before sending CDP commands.',
      };
    }
    try {
      return await transport.send(command, params);
    } catch (err) {
      throw normalizeTransportError(command, err);
    }
  }

  function withMappedStacks(result) {
    if (!result || typeof result !== 'object') return result;
    const frames = result.callFrames;
    if (!Array.isArray(frames)) return result;
    return { ...result, callFrames: mapCallFrames(frames, sourceMap) };
  }

  async function fetchMappedStack() {
    // An unsupported stack fetch propagates: synthesizing frames here would
    // mask the target's real behavior, so callers decide how to proceed.
    const raw = await send('Debugger.getStackTrace', {});
    return withMappedStacks(raw);
  }

  const bridge = {
    transport,
    __transport: transport,

    __setTransport(next) {
      transport = next;
      bridge.transport = next;
      bridge.__transport = next;
    },

    __setSourceMap(next) {
      sourceMap = next;
    },

    async setBreakpoint(params) {
      return send('Debugger.setBreakpointByUrl', params || {});
    },

    async stepOver(params) {
      const stepResult = await send('Debugger.stepOver', params || {});
      let stack = null;
      try {
        stack = await fetchMappedStack();
      } catch (err) {
        // Unsupported stack fetch propagates without invented frames.
        if (err && err.cause === UNSUPPORTED_CAUSE) throw err;
        throw err;
      }
      const merged = { ...stepResult };
      if (stack && Array.isArray(stack.callFrames)) {
        merged.callFrames = stack.callFrames;
      } else if (Array.isArray(merged.callFrames)) {
        merged.callFrames = mapCallFrames(merged.callFrames, sourceMap);
      }
      // Otherwise return the step result as-is: never invent callFrames.
      return withMappedStacks(merged);
    },

    async resume() {
      return send('Debugger.resume', {});
    },

    async evaluateOnCallFrame(params) {
      return send('Debugger.evaluateOnCallFrame', params || {});
    },

    async evaluate(params) {
      const marker = unsupportedMarkerCommand(params || {});
      if (marker) throw unsupportedError(marker);
      return send('Runtime.evaluate', params || {});
    },

    async takeHeapSnapshot() {
      return send('Heap.takeHeapSnapshot', {});
    },

    async startCoverage() {
      return send('Profiler.startPreciseCoverage', {});
    },

    async unsupportedPanel(command) {
      throw unsupportedError(command || 'unknown');
    },

    async sendUnsupported(command) {
      throw unsupportedError(command || 'unknown');
    },
  };

  return bridge;
}

module.exports = { createCdpBridge, UNSUPPORTED_CAUSE };
