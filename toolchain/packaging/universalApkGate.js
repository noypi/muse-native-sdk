'use strict';

// T8 — universal APK fallback gate (NFR-BUILD-010 / AC-BUILD-026).
// Verifies the packaging config declares a bundletool-produced universal APK
// fallback functionally equivalent to the AAB. Fails closed: unreadable or
// unparsable configs are violations, never throws.

const fs = require('node:fs');

function checkUniversalFallback(bundleConfigPath) {
  let raw;
  try {
    raw = fs.readFileSync(bundleConfigPath, 'utf8');
  } catch (err) {
    return {
      ok: false,
      violations: [`universal APK fallback: cannot read bundle config at ${bundleConfigPath}: ${err.code || err.message}`],
    };
  }

  let config;
  try {
    config = JSON.parse(raw);
  } catch (err) {
    return {
      ok: false,
      violations: [`universal APK fallback: bundle config at ${bundleConfigPath} is not valid JSON: ${err.message}`],
    };
  }

  if (config === null || typeof config !== 'object' || Array.isArray(config)) {
    return {
      ok: false,
      violations: [`universal APK fallback: bundle config at ${bundleConfigPath} must be a JSON object declaring universalApk`],
    };
  }

  const fallback = config.universalApk;
  if (fallback === null || typeof fallback !== 'object' || Array.isArray(fallback)) {
    return {
      ok: false,
      violations: [`universal APK fallback: missing universalApk declaration declaring the bundletool universal APK fallback`],
    };
  }

  const violations = [];
  if (fallback.enabled !== true) {
    violations.push('universal APK fallback: universalApk.enabled must be true');
  }
  if (fallback.mode !== 'bundletool') {
    violations.push('universal APK fallback: universalApk.mode must be "bundletool" (bundletool-produced universal APK required)');
  }
  if (fallback.equivalentSplits !== true) {
    violations.push('universal APK fallback: universalApk.equivalentSplits must be true (fallback must be split-equivalent to the AAB)');
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { checkUniversalFallback };
