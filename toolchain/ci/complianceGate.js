'use strict';
// T2 — SDK policy compliance gate (NFR-BUILD-009 / AC-BUILD-023, AC-BUILD-025).
// Node-runnable: complianceGate(catalogPath) reads the T1 version catalog
// plus the app manifest and fails closed on policy drift.

const fs = require('node:fs');
const path = require('node:path');

const FALLBACK_ENFORCED_TARGET_SDK = 35;

function loadEnforcedTargetSdk() {
  try {
    const policyPath = path.join(__dirname, 'sdk-policy.json');
    const policy = JSON.parse(fs.readFileSync(policyPath, 'utf8'));
    const level = Number(policy.enforcedTargetSdk);
    if (Number.isInteger(level)) {
      return level;
    }
  } catch (_) {
    // Fail closed: fall through to the pinned Play floor.
  }
  return FALLBACK_ENFORCED_TARGET_SDK;
}

function parseTargetSdk(catalogText) {
  const match = catalogText.match(/^\s*targetSdk\s*=\s*"?(\d+)"?\s*$/m);
  if (!match) {
    return null;
  }
  const level = Number(match[1]);
  return Number.isInteger(level) ? level : null;
}

function complianceGate(catalogPath) {
  const violations = [];
  const enforcedTargetSdk = loadEnforcedTargetSdk();

  let catalogText = null;
  try {
    catalogText = fs.readFileSync(catalogPath, 'utf8');
  } catch (_) {
    catalogText = null;
  }

  if (catalogText === null) {
    violations.push(`catalog missing targetSdk: ${catalogPath}`);
  } else {
    const targetSdk = parseTargetSdk(catalogText);
    if (targetSdk === null) {
      violations.push(`catalog missing targetSdk: ${catalogPath}`);
    } else if (targetSdk < enforcedTargetSdk) {
      violations.push(
        `targetSdk ${targetSdk} below enforced ${enforcedTargetSdk} in libs.versions.toml: ${catalogPath}`
      );
    }
  }

  const repoRoot = path.resolve(path.dirname(catalogPath), '..');
  const manifestPath = path.join(repoRoot, 'app', 'src', 'main', 'AndroidManifest.xml');

  let manifestText = null;
  try {
    manifestText = fs.readFileSync(manifestPath, 'utf8');
  } catch (_) {
    manifestText = null;
  }

  if (manifestText === null) {
    violations.push(`missing AndroidManifest.xml: ${manifestPath}`);
  } else {
    if (!manifestText.includes('enableOnBackInvokedCallback')) {
      violations.push(
        `AndroidManifest.xml missing predictive-back opt-in (android:enableOnBackInvokedCallback): ${manifestPath}`
      );
    }
    if (!manifestText.includes('foregroundServiceType')) {
      violations.push(
        `AndroidManifest.xml missing foreground-service-type declaration (android:foregroundServiceType): ${manifestPath}`
      );
    }
    if (!manifestText.includes('windowLayoutInDisplayCutoutMode')) {
      violations.push(
        `AndroidManifest.xml missing edge-to-edge declaration (android:windowLayoutInDisplayCutoutMode="shortEdges"): ${manifestPath}`
      );
    }
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { complianceGate };
