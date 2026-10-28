'use strict';
// JSI boundary stepping (FR-DX-009 / AC-DX-040 / TC-DX-040).
// Headless mapping only — no device, no LLDB binary.

const JS_TO_NATIVE = {
  onPress: 'facebook::jsi::Function::call',
};

const NATIVE_TO_JS = {};
for (const [js, native] of Object.entries(JS_TO_NATIVE)) {
  NATIVE_TO_JS[native] = js;
}

function stepError(cause) {
  return {
    command: 'jsiStep',
    cause: String(cause || 'unknown JS frame'),
    action: 'Use a JS function known to cross the JSI boundary (e.g. onPress).',
  };
}

function stepAcrossJsi(jsFrame) {
  if (typeof jsFrame !== 'string' || jsFrame.length === 0) {
    throw new TypeError('jsFrame must be a non-empty string');
  }
  const nativeSymbol = JS_TO_NATIVE[jsFrame];
  if (!nativeSymbol) {
    throw stepError(`unknown JS frame "${jsFrame}"`);
  }
  return { jsFunction: jsFrame, nativeSymbol };
}

function stepAcrossJsiBack(nativeSymbol) {
  if (typeof nativeSymbol !== 'string' || nativeSymbol.length === 0) {
    throw new TypeError('nativeSymbol must be a non-empty string');
  }
  const jsFunction = NATIVE_TO_JS[nativeSymbol];
  if (!jsFunction) {
    throw stepError(`unknown native symbol "${nativeSymbol}"`);
  }
  return { jsFunction, nativeSymbol };
}

function stepToJs(nativeSymbol) {
  return stepAcrossJsiBack(nativeSymbol);
}

module.exports = { stepAcrossJsi, stepAcrossJsiBack, stepToJs };
