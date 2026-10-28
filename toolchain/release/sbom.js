'use strict';

// T2 — deterministic SBOM emission + strict parse (AC-BUILD-002 SBOM half,
// AC-SECURITY-035, AC-BUILD-046/050, AC-BUILD-060). Pure functions;
// fail closed with Error naming the offending entry on unpinned or
// missing version/integrity. Canonical JSON: keys sorted recursively,
// stable dep order (T1 byte-compare ordering), no timestamps.

const PINNED_VERSION_RE = /^[0-9]+\.[0-9]+\.[0-9]+(?:[-+][0-9A-Za-z.-]+)?$/;

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

function assertBuildMeta(buildMeta) {
  if (!buildMeta || typeof buildMeta !== 'object' || Array.isArray(buildMeta)) {
    throw new Error('sbom: buildMeta must be an object with { ndkVersion, commit }');
  }
  if (typeof buildMeta.ndkVersion !== 'string' || buildMeta.ndkVersion.length === 0) {
    throw new Error('sbom: buildMeta.ndkVersion must be a non-empty string');
  }
  if (typeof buildMeta.commit !== 'string' || buildMeta.commit.length === 0) {
    throw new Error('sbom: buildMeta.commit must be a non-empty string');
  }
}

function depLabel(dep, index) {
  if (dep && typeof dep.name === 'string' && dep.name.length > 0) {
    return dep.name;
  }
  return `#${index}`;
}

function assertDep(dep, index) {
  const label = depLabel(dep, index);
  if (!dep || typeof dep !== 'object' || Array.isArray(dep)) {
    throw new Error(`sbom: invalid dep entry '${label}': must be an object`);
  }
  if (typeof dep.name !== 'string' || dep.name.length === 0) {
    throw new Error(`sbom: invalid dep entry '${label}': name must be a non-empty string`);
  }
  if (typeof dep.version !== 'string' || !PINNED_VERSION_RE.test(dep.version)) {
    throw new Error(
      `sbom: unpinned or missing version for entry '${dep.name}': ${JSON.stringify(dep.version)}`
    );
  }
  if (typeof dep.integrity !== 'string' || dep.integrity.length === 0) {
    throw new Error(`sbom: missing integrity for entry '${dep.name}'`);
  }
  if (typeof dep.ecosystem !== 'string' || dep.ecosystem.length === 0) {
    throw new Error(`sbom: missing ecosystem for entry '${dep.name}'`);
  }
  return {
    name: dep.name,
    version: dep.version,
    integrity: dep.integrity,
    ecosystem: dep.ecosystem,
  };
}

function compareDeps(a, b) {
  return (
    compareUtf8(a.name, b.name) ||
    compareUtf8(a.version, b.version) ||
    compareUtf8(a.ecosystem, b.ecosystem) ||
    compareUtf8(a.integrity, b.integrity)
  );
}

function emitSbom(lockedDeps, buildMeta) {
  if (!Array.isArray(lockedDeps)) {
    throw new Error('sbom: lockedDeps must be an array');
  }
  assertBuildMeta(buildMeta);
  const deps = lockedDeps.map((dep, index) => assertDep(dep, index));
  deps.sort(compareDeps);
  const doc = {
    bomFormat: 'CycloneDX',
    buildMeta: { commit: buildMeta.commit, ndkVersion: buildMeta.ndkVersion },
    deps,
    specVersion: '1.5',
  };
  return canonicalStringify(doc);
}

function parseSbom(json) {
  if (typeof json !== 'string') {
    throw new Error('sbom: input must be a JSON string');
  }
  let parsed;
  try {
    parsed = JSON.parse(json);
  } catch {
    throw new Error('sbom: invalid JSON');
  }
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    throw new Error('sbom: SBOM must be a JSON object');
  }
  if (!Array.isArray(parsed.deps)) {
    throw new Error('sbom: SBOM must contain a deps array');
  }
  assertBuildMeta(parsed.buildMeta);
  const deps = parsed.deps.map((dep, index) => assertDep(dep, index));
  return {
    deps,
    buildMeta: { ndkVersion: parsed.buildMeta.ndkVersion, commit: parsed.buildMeta.commit },
  };
}

module.exports = { emitSbom, parseSbom };
