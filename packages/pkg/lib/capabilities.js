'use strict';

// Capability policy gate (FR-PKG-005, AC-PKG-012).
// Pure manifest-vs-grant comparison: exact string match only, no wildcards.
// The sandbox policy engine itself is owned by the security spec — this
// module only enforces the app grant fail-closed, never contacts it.

const { PkgError } = require('./errors.js');

// MVP never grants graphics surfaces; refusal only (plan Out of Scope,
// T7 owns the no-render symbol scan — this set only gates the grant check).
const UNGRANTABLE_CAPABILITIES = new Set(['gpu-surface', 'window']);

function checkCapabilities(manifest, granted, policyId) {
  const policy = typeof policyId === 'string' ? policyId : '';
  if (policy.length === 0) {
    throw new PkgError(
      'E_CAPABILITY',
      'policy',
      'a non-empty policy id is required to grant package capabilities',
    );
  }
  const required = manifest && Array.isArray(manifest.capabilities) ? manifest.capabilities : [];
  const grants = Array.isArray(granted) ? granted : [];
  for (const capability of required) {
    const name = String(capability);
    if (UNGRANTABLE_CAPABILITIES.has(name)) {
      throw new PkgError(
        'E_CAPABILITY',
        `capabilities.${name}`,
        `capability "${name}" is not grantable under policy "${policy}"`,
      );
    }
    // Exact match only: no prefix, suffix, or "*" wildcard expansion.
    // Capability strings are opaque — compared with Array#includes only.
    if (!grants.includes(name)) {
      throw new PkgError(
        'E_CAPABILITY',
        `capabilities.${name}`,
        `capability "${name}" must be granted by policy "${policy}"`,
      );
    }
  }
}

module.exports = { checkCapabilities, UNGRANTABLE_CAPABILITIES };
