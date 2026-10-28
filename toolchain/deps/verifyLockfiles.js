'use strict';

const HASH_RE = /^[A-Za-z0-9]+-[A-Za-z0-9+/=]+$/;
const JS_PINNED_RE = /^[0-9]+\.[0-9]+\.[0-9]+([-+][0-9A-Za-z.-]+)?$/;
const GRADLE_PINNED_RE = /^[0-9]+(\.[0-9]+)*([-+][0-9A-Za-z.-]+)?$/;

function isPinnedJs(version) {
  return typeof version === 'string' && JS_PINNED_RE.test(version);
}

function isPinnedGradle(version) {
  return typeof version === 'string' && GRADLE_PINNED_RE.test(version);
}

function isWellFormedHash(value) {
  return typeof value === 'string' && value.length > 0 && HASH_RE.test(value);
}

function verifyLockfiles(input) {
  if (input === null || input === undefined || typeof input !== 'object' || Array.isArray(input)) {
    throw new TypeError('verifyLockfiles: input must be an object');
  }
  const js = input.js === undefined ? [] : input.js;
  const gradle = input.gradle === undefined ? [] : input.gradle;
  const violations = [];

  const jsEntries = Array.isArray(js) ? js : [];
  const gradleEntries = Array.isArray(gradle) ? gradle : [];
  if (!Array.isArray(js)) {
    violations.push('js: unexpected entry list is not a list');
  }
  if (!Array.isArray(gradle)) {
    violations.push('gradle: unexpected entry list is not a list');
  }

  const jsVersionsByName = new Map();
  for (const entry of jsEntries) {
    if (entry === null || entry === undefined || typeof entry !== 'object' || Array.isArray(entry)) {
      violations.push('js: unknown entry is not an object');
      continue;
    }
    const name = typeof entry.name === 'string' && entry.name.length > 0 ? entry.name : 'unknown';
    const problems = [];
    if (!isPinnedJs(entry.version)) {
      problems.push(`unpinned version ${String(entry.version)}`);
    }
    if (!isWellFormedHash(entry.integrity)) {
      problems.push('missing or malformed digest');
    }
    if (problems.length > 0) {
      violations.push(`js: ${name} ${problems.join('; ')}`);
    }
    if (typeof entry.name === 'string' && entry.name.length > 0) {
      if (!jsVersionsByName.has(entry.name)) {
        jsVersionsByName.set(entry.name, new Set());
      }
      jsVersionsByName.get(entry.name).add(String(entry.version));
    }
  }
  for (const [name, versions] of jsVersionsByName) {
    if (versions.size > 1) {
      violations.push(`js: ${name} has conflicting pinned versions ${[...versions].join(', ')}`);
    }
  }

  const gradleVersionsByCoord = new Map();
  for (const entry of gradleEntries) {
    if (entry === null || entry === undefined || typeof entry !== 'object' || Array.isArray(entry)) {
      violations.push('gradle: unknown entry is not an object');
      continue;
    }
    const coord = typeof entry.coord === 'string' && entry.coord.length > 0 ? entry.coord : 'unknown';
    const problems = [];
    if (!isPinnedGradle(entry.version)) {
      problems.push(`unpinned version ${String(entry.version)}`);
    }
    if (!isWellFormedHash(entry.checksum)) {
      problems.push('missing or malformed digest');
    }
    if (problems.length > 0) {
      violations.push(`gradle: ${coord} ${problems.join('; ')}`);
    }
    if (typeof entry.coord === 'string' && entry.coord.length > 0) {
      if (!gradleVersionsByCoord.has(entry.coord)) {
        gradleVersionsByCoord.set(entry.coord, new Set());
      }
      gradleVersionsByCoord.get(entry.coord).add(String(entry.version));
    }
  }
  for (const [coord, versions] of gradleVersionsByCoord) {
    if (versions.size > 1) {
      violations.push(`gradle: ${coord} has conflicting pinned versions ${[...versions].join(', ')}`);
    }
  }

  return { ok: violations.length === 0, violations };
}

module.exports = { verifyLockfiles };
