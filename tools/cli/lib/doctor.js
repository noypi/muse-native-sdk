'use strict';
// T2 — FR-DX-008 / AC-DX-033 / TC-DX-033.
// Doctor command: evaluates the eight toolchain/environment checks and
// returns one entry per check. Every failing entry names the owning
// subsystem + spec id (AC-DX-033) and gives an exact fix command
// (NFR-DX-002). Hermetic: probes read process.env (MUSE_*) only.

const { CHECKS } = require('./doctor-checks');

function runDoctor() {
  return CHECKS.map((check) => {
    const raw = process.env[check.envVar];
    const found = typeof raw === 'string' ? raw.trim() : '';
    if (found) {
      return { name: check.name, ok: true, found };
    }
    return {
      name: check.name,
      ok: false,
      remediation:
        `[${check.subsystem} AC-DX-033] ${check.label} not found. Run: ${check.fix}`,
    };
  });
}

module.exports = { runDoctor };
