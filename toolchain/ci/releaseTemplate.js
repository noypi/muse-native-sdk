'use strict';
// T11 — Release-template end-to-end gate (NFR-BUILD-015 / AC-056).
// Pure in-memory composition gate: evaluates upload → compliance → perf →
// provenance in order and short-circuits at the first failing stage.
// Sibling gates are NOT imported; callers inject plain result objects.
// AC-053 (staged rollout + crash/ANR halt via Play console) is out of scope.

function checkUpload(upload) {
  const source = upload && typeof upload === 'object' ? upload : {};
  const reasons = [];
  if (!source.signed) {
    reasons.push('build is unsigned: release must be signed with a release key');
  }
  if (source.debugSigned) {
    reasons.push('build is debug-signed: release must not use a debug key');
  }
  if (!source.reproducible) {
    reasons.push('build is not reproducible: release must be reproducible');
  }
  return reasons;
}

function checkCompliance(compliance) {
  if (compliance && compliance.ok) {
    return [];
  }
  if (compliance && Array.isArray(compliance.violations) && compliance.violations.length > 0) {
    return compliance.violations.map(String);
  }
  return ['compliance gate failed'];
}

function checkPerf(perf) {
  if (!Array.isArray(perf) || perf.length === 0) {
    return ['no performance results: failing closed'];
  }
  const reasons = [];
  for (const entry of perf) {
    if (!entry || entry.passed !== true) {
      const suite = entry && entry.suite ? String(entry.suite) : 'unknown suite';
      reasons.push(`performance budget failed: ${suite}`);
    }
  }
  return reasons;
}

function checkProvenance(provenance) {
  const source = provenance && typeof provenance === 'object' ? provenance : {};
  const { artifactHash, attestedHash } = source;
  if (
    typeof artifactHash === 'string' &&
    artifactHash.length > 0 &&
    typeof attestedHash === 'string' &&
    attestedHash.length > 0 &&
    artifactHash === attestedHash
  ) {
    return [];
  }
  return ['provenance mismatch: artifactHash does not match attestedHash'];
}

function releaseTemplate(args) {
  const source = args && typeof args === 'object' ? args : {};

  const uploadReasons = checkUpload(source.upload);
  if (uploadReasons.length > 0) {
    return { ok: false, stage: 'upload', reasons: uploadReasons };
  }

  const complianceReasons = checkCompliance(source.compliance);
  if (complianceReasons.length > 0) {
    return { ok: false, stage: 'compliance', reasons: complianceReasons };
  }

  const perfReasons = checkPerf(source.perf);
  if (perfReasons.length > 0) {
    return { ok: false, stage: 'perf', reasons: perfReasons };
  }

  const provenanceReasons = checkProvenance(source.provenance);
  if (provenanceReasons.length > 0) {
    return { ok: false, stage: 'provenance', reasons: provenanceReasons };
  }

  return { ok: true, stage: null, reasons: [] };
}

module.exports = { releaseTemplate };
