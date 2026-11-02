'use strict';

// Batch 2 (T2) — bridge transport adapter (FR-NAV-014, AC-NAV-041).
// Binds RouterBridge.invoke once at boot; forwards Transport.invoke to it.
// No global reads here — transport is injected.

function createBridgeTransport(bridge) {
  const invokeBridge = bridge.invoke.bind(bridge);
  async function invoke(method, payload) {
    return invokeBridge(method, payload);
  }
  return { invoke };
}

module.exports = { createBridgeTransport };
