'use strict';

// T3 — deterministic provenance attestation + fail-closed verification
// (AC-BUILD-002 provenance half, AC-SECURITY-035). Pure functions;
// canonical JSON: keys sorted recursively, stable subject order, no timestamps.

const SHA256_RE = /^[0-9a-f]{64}$/;

function compareUtf8(a, b) {
  return Buffer.compare(Buffer.from(a, 'utf8'), Buffer.from(b, 'utf8'));
}

function sortValue(value) {
  if (Array.isArray(value)) {
    return value.map(sortValue);
  }
  if (value !== null && typeof value === 'object') {
    const out = {};
    for (const key of Object.keys(value).sort(compareUtf8)) {
      out[key] = sortValue(value[key]);
    }
    return out;
  }
  return value;
}

function canonicalStringify(value) {
  return JSON.stringify(sortValue(value));
}

function assertSubject(entry) {
  if (!entry || typeof entry !== 'object' || Array.isArray(entry)) {
    throw new Error('provenance: subject entry must be an object with { path, sha256 }');
  }
  if (typeof entry.path !== 'string' || entry.path.length === 0) {
    throw new Error('provenance: subject entry path must be a non-empty string');
  }
  if (typeof entry.sha256 !== 'string' || !SHA256_RE.test(entry.sha256)) {
    throw new Error(
      `provenance: invalid sha256 for subject '${entry.path}': ${JSON.stringify(entry.sha256)}`
    );
  }
  return { path: entry.path, sha256: entry.sha256 };
}

function assertBuildMeta(buildMeta) {
  if (!buildMeta || typeof buildMeta !== 'object' || Array.isArray(buildMeta)) {
    throw new Error('provenance: buildMeta must be an object with { builder, commit, ndkVersion, lockedInputsDigest }');
  }
  for (const field of ['builder', 'commit', 'ndkVersion', 'lockedInputsDigest']) {
    if (typeof buildMeta[field] !== 'string' || buildMeta[field].length === 0) {
      throw new Error(`provenance: buildMeta.${field} must be a non-empty string`);
    }
  }
  return {
    builder: buildMeta.builder,
    commit: buildMeta.commit,
    ndkVersion: buildMeta.ndkVersion,
    lockedInputsDigest: buildMeta.lockedInputsDigest,
  };
}

function emitProvenance(subjectDigests, buildMeta) {
  if (!Array.isArray(subjectDigests)) {
    throw new Error('provenance: subjectDigests must be an array');
  }
  assertBuildMeta(buildMeta);
  const subjects = subjectDigests.map(assertSubject);
  subjects.sort((a, b) => compareUtf8(a.path, b.path) || compareUtf8(a.sha256, b.sha256));
  const doc = {
    builder: buildMeta.builder,
    commit: buildMeta.commit,
    lockedInputsDigest: buildMeta.lockedInputsDigest,
    ndkVersion: buildMeta.ndkVersion,
    subjects,
  };
  return canonicalStringify(doc);
}

function parseAttestation(attestationJson) {
  let parsed;
  try {
    parsed = JSON.parse(attestationJson);
  } catch {
    return { error: 'provenance: attestation is not valid JSON' };
  }
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    return { error: 'provenance: attestation must be a JSON object' };
  }
  try {
    assertBuildMeta(parsed);
  } catch (err) {
    return { error: `provenance: invalid attestation buildMeta: ${err.message}` };
  }
  if (!Array.isArray(parsed.subjects)) {
    return { error: 'provenance: attestation must contain a subjects array' };
  }
  try {
    const subjects = parsed.subjects.map(assertSubject);
    return {
      doc: {
        builder: parsed.builder,
        commit: parsed.commit,
        ndkVersion: parsed.ndkVersion,
        lockedInputsDigest: parsed.lockedInputsDigest,
        subjects,
      },
    };
  } catch (err) {
    return { error: `provenance: invalid attestation subject: ${err.message}` };
  }
}

function verifyProvenance(attestationJson, actualDigests) {
  if (typeof attestationJson !== 'string') {
    return { ok: false, failures: ['provenance: attestation must be a JSON string'] };
  }
  const parsed = parseAttestation(attestationJson);
  if (parsed.error) {
    return { ok: false, failures: [parsed.error] };
  }
  if (!Array.isArray(actualDigests)) {
    return { ok: false, failures: ['provenance: actualDigests must be an array'] };
  }
  const expectedByPath = new Map();
  for (const s of parsed.doc.subjects) {
    expectedByPath.set(s.path, s.sha256);
  }
  const failures = [];
  const seen = new Set();
  for (const entry of actualDigests) {
    if (!entry || typeof entry.path !== 'string' || typeof entry.sha256 !== 'string') {
      failures.push('provenance: actual subject entry must have { path, sha256 }');
      continue;
    }
    seen.add(entry.path);
    if (!expectedByPath.has(entry.path)) {
      failures.push(`provenance: unexpected subject '${entry.path}' not in attestation`);
    } else if (expectedByPath.get(entry.path) !== entry.sha256) {
      failures.push(`provenance: digest mismatch for subject '${entry.path}'`);
    }
  }
  for (const [path] of expectedByPath) {
    if (!seen.has(path)) {
      failures.push(`provenance: missing subject '${path}' not in build outputs`);
    }
  }
  if (failures.length > 0) {
    return { ok: false, failures };
  }
  return { ok: true, failures: [] };
}

module.exports = { emitProvenance, verifyProvenance };
