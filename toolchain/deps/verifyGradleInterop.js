'use strict';

function verifyGradleInterop(input) {
  if (input === null || input === undefined || typeof input !== 'object' || Array.isArray(input)) {
    throw new TypeError('verifyGradleInterop: input must be an object');
  }
  const direct = input.direct;
  const violations = [];

  const coord = direct !== null && direct !== undefined && typeof direct === 'object' && !Array.isArray(direct)
    ? direct.coord
    : undefined;
  if (typeof coord !== 'string' || coord.length === 0) {
    violations.push('direct: missing coord');
  }

  let libs = [];
  if (direct !== null && direct !== undefined && typeof direct === 'object' && !Array.isArray(direct)) {
    if (direct.transitiveNativeLibs === undefined) {
      libs = [];
    } else if (!Array.isArray(direct.transitiveNativeLibs)) {
      throw new TypeError('verifyGradleInterop: transitiveNativeLibs must be an array');
    } else {
      libs = direct.transitiveNativeLibs;
    }
  } else {
    libs = [];
  }

  libs.forEach((lib, i) => {
    const label = lib !== null && lib !== undefined && typeof lib === 'object' && !Array.isArray(lib)
      && typeof lib.coord === 'string' && lib.coord.length > 0
      ? lib.coord
      : `transitiveNativeLibs[${i}]`;
    const aligned = lib !== null && lib !== undefined && typeof lib === 'object' && !Array.isArray(lib)
      ? lib.elfAligned16k
      : undefined;
    const listed = lib !== null && lib !== undefined && typeof lib === 'object' && !Array.isArray(lib)
      ? lib.sbomListed
      : undefined;
    if (aligned !== true) {
      violations.push(`${label}: not 16k-aligned (elfAligned16k)`);
    }
    if (listed !== true) {
      violations.push(`${label}: missing from SBOM (sbomListed)`);
    }
  });

  return { ok: violations.length === 0, violations };
}

module.exports = { verifyGradleInterop };
