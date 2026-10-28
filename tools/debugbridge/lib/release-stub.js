'use strict';
// Release entry point (FR-DX-009 / AC-DX-042 + FR-DX-001 exclusion).
// Compiled without the debug path: refuses inbound debug use and never
// opens any inbound path. Dev use returns a fresh in-memory session.

let nextSessionId = 0;

function refused() {
  return {
    status: 'refused',
    reason: 'debug channel not compiled in',
    action: 'Rebuild with the dev flavor to enable debugging.',
  };
}

function connect(buildType) {
  if (buildType !== 'dev') {
    return refused();
  }
  nextSessionId += 1;
  return {
    status: 'connected',
    session: { buildType: 'dev', id: nextSessionId },
  };
}

module.exports = { connect };
