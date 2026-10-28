'use strict';
// LLDB attach + symbolicated stack (FR-DX-009 / AC-DX-040 / TC-DX-040).
// Headless fixture traces only — no device, no LLDB binary, no socket.

function defaultFixtureTrace() {
  return [
    { frame: '#0 0x7f001122 in facebook::jsi::Function::call', symbol: 'jsi::Function::call', jsiCall: { jsFunction: 'onPress', kind: 'js-to-native' } },
    { frame: '#1 0x7f0010aa in HermesRuntime::callFunction', symbol: 'HermesRuntime::callFunction', jsiCall: null },
    { frame: '#2 0x7f000f01 in __libc_start_main', symbol: '__libc_start_main', jsiCall: null },
  ];
}

let traceSource = async (pid) => {
  if (pid === 1234) return defaultFixtureTrace();
  const err = new Error(`no such process: ${pid}`);
  err.command = 'lldbAttach';
  throw err;
};

let symbolicator = async (frame) => ({ symbol: (frame && frame.symbol) || (frame && frame.frame) || '' });

function attachError(cause) {
  return {
    command: 'lldbAttach',
    cause: String(cause || 'lldb attach failed'),
    action: 'Check the pid and retry attach via the DX-009 LLDB flow.',
  };
}

function toStructuredAttachError(err) {
  if (err && err.command === 'lldbAttach' && typeof err.cause === 'string' && err.cause.length > 0
    && typeof err.action === 'string' && err.action.length > 0) {
    return err;
  }
  const command = (err && err.command === 'lldbAttach') ? 'lldbAttach' : 'lldbAttach';
  let cause = '';
  if (err && typeof err.cause === 'string' && err.cause.length > 0) {
    cause = err.cause;
  } else if (err && typeof err.message === 'string' && err.message.length > 0) {
    cause = err.message;
  } else {
    try {
      cause = String(err);
    } catch {
      cause = 'lldb attach failed';
    }
  }
  let action = (err && typeof err.action === 'string' && err.action.length > 0)
    ? err.action
    : 'Check the pid and retry attach via the DX-009 LLDB flow.';
  void command;
  return { command: 'lldbAttach', cause, action };
}

async function attachLldb(pid, deps) {
  const source = (deps && typeof deps.traceSource === 'function') ? deps.traceSource : traceSource;
  const symbolicate = (deps && (typeof deps.symbolicator === 'function' || typeof deps.symbolicate === 'function'))
    ? (deps.symbolicator || deps.symbolicate)
    : symbolicator;
  let frames;
  try {
    frames = await source(pid);
  } catch (err) {
    throw toStructuredAttachError(err);
  }
  if (!Array.isArray(frames)) {
    throw attachError('lldb attach returned a malformed stack');
  }
  try {
    const out = [];
    for (const entry of frames) {
      const rawFrame = (entry && typeof entry.frame === 'string') ? entry.frame : String((entry && entry.frame) || '');
      let symbol = '';
      try {
        const res = await symbolicate(entry);
        if (typeof res === 'string') symbol = res;
        else if (res && typeof res.symbol === 'string') symbol = res.symbol;
      } catch {
        symbol = '';
      }
      if (!symbol) symbol = (entry && entry.symbol) || rawFrame;
      symbol = String(symbol);
      if (!symbol) throw attachError('symbolication produced an empty symbol');
      const jsiCall = (entry && 'jsiCall' in entry) ? entry.jsiCall : null;
      out.push({ frame: rawFrame, symbol, jsiCall: jsiCall == null ? null : jsiCall });
    }
    return out;
  } catch (err) {
    if (err && err.command === 'lldbAttach') throw err;
    throw toStructuredAttachError(err);
  }
}

function __setTraceSource(fn) {
  traceSource = fn;
}

function __setSymbolicator(fn) {
  symbolicator = fn;
}

module.exports = { attachLldb, __setTraceSource, __setSymbolicator };
