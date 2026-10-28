'use strict';
// Release entry point for the local dev host (FR-DX-009 / AC-DX-042).
// Compiled without the debug path: refuses inbound debug use and never
// opens any inbound path.
//
// NOTE: this file is the release-only artifact. `connect` intentionally
// refuses every buildType — including 'dev' — because the debug channel is
// not compiled into this artifact (fail-closed). The `buildType` parameter
// is accepted for signature parity only and is always refused.

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
