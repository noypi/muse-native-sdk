'use strict';
// T9 — Consolidated release compliance gate (NFR-BUILD-015 / AC-052).
// Pure in-memory gate: no I/O, no network, no secrets.

function releaseCompliance(input) {
  const violations = [];
  const source = input && typeof input === 'object' ? input : {};
  const { targetSdk, enforcedTargetSdk, manifest, nativeLibs } = source;

  if (!Number.isInteger(targetSdk) || !Number.isInteger(enforcedTargetSdk) || targetSdk < enforcedTargetSdk) {
    violations.push(`stale targetSdk ${String(targetSdk)} below enforced targetSdk ${String(enforcedTargetSdk)}`);
  }

  const manifestObj = manifest && typeof manifest === 'object' ? manifest : {};
  if (manifestObj.predictiveBack !== true) {
    violations.push('AndroidManifest missing predictive-back opt-in (predictiveBack)');
  }
  if (manifestObj.foregroundServiceTypes !== true) {
    violations.push('AndroidManifest missing foreground-service-types declaration (foregroundServiceTypes)');
  }

  if (!Array.isArray(nativeLibs) || nativeLibs.length === 0) {
    violations.push('no native-lib alignment evidence: nativeLibs is empty');
  } else {
    nativeLibs.forEach((lib, index) => {
      const entry = lib && typeof lib === 'object' ? lib : {};
      const label = typeof entry.path === 'string' && entry.path.length > 0 ? entry.path : `#${index}`;
      if (entry.elfAligned16k !== true) {
        violations.push(`native lib not 16 KB ELF-aligned: ${label}`);
      }
    });
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { releaseCompliance };
