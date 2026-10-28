'use strict';
// Release entry point for the local dev host (FR-DX-009 / AC-DX-042).
// Compiled without the debug path: refuses inbound debug use and never
// opens any inbound path.

function refused() {
  return {
    status: 'refused',
    reason: 'debug channel not compiled in',
    action: 'Rebuild with the dev flavor to enable debugging.',
  };
}

function connect(buildType) {
  void buildType;
  return refused();
}

module.exports = { connect };
