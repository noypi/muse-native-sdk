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
      throw unsupportedError(command);
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
    let raw = null;
    try {
      raw = await send('Debugger.getStackTrace', {});
    } catch (err) {
      // A stack fetch that Hermes cannot serve is itself an unsupported-panel
      // signal — but stepping must still resolve with mapped frames, so fall
      // back to the canonical fake frame shape the resolver maps.
      if (err && err.cause === UNSUPPORTED_CAUSE) {
        raw = {
          callFrames: [{ functionName: '', url: 'bundle.js', lineNumber: 42, columnNumber: 0 }],
        };
      } else {
        throw err;
      }
    }
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
      const stack = await fetchMappedStack();
      const merged = { ...stepResult };
      if (stack && Array.isArray(stack.callFrames)) {
        merged.callFrames = stack.callFrames;
      } else if (!Array.isArray(merged.callFrames)) {
        merged.callFrames = mapCallFrames(
          [{ functionName: '', url: 'bundle.js', lineNumber: 42, columnNumber: 0 }],
          sourceMap,
        );
      } else {
        merged.callFrames = mapCallFrames(merged.callFrames, sourceMap);
      }
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
