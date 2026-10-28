'use strict';

// Per-ABI coverage and engine ABI hash checks (FR-PKG-002).
// Pure manifest-vs-list comparisons; never touches native `.so` / `dlopen`.

const { PkgError } = require('./errors.js');

function checkAbiCoverage(manifest, appAbis) {
  const covered = Array.isArray(manifest && manifest.abis) ? manifest.abis : [];
  const requested = Array.isArray(appAbis) ? appAbis : [];
  const coveredList = covered.join(', ');
  for (const abi of requested) {
    if (!covered.includes(abi)) {
      throw new PkgError(
        'E_ABI_COVERAGE',
        String(abi),
        `one of: ${coveredList}`,
      );
    }
  }
}

function checkEngineAbi(manifest, runningEngineAbi) {
  const expected = manifest && manifest.engineAbi;
  if (typeof expected !== 'string' || expected.length === 0) {
    throw new PkgError(
      'E_ENGINE_ABI',
      'engineAbi',
      'engine ABI hash declared by the package manifest',
    );
  }
  if (typeof runningEngineAbi !== 'string' || runningEngineAbi.length === 0) {
    throw new PkgError(
      'E_ENGINE_ABI',
      'engineAbi',
      expected,
      `E_ENGINE_ABI at engineAbi: expected "${expected}" but running engine reported none`,
    );
  }
  if (runningEngineAbi !== expected) {
    throw new PkgError(
      'E_ENGINE_ABI',
      'engineAbi',
      expected,
      `E_ENGINE_ABI at engineAbi: expected "${expected}" but running engine reported "${runningEngineAbi}"`,
    );
  }
}

module.exports = { checkAbiCoverage, checkEngineAbi };
