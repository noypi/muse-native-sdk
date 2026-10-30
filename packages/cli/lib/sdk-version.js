'use strict';
// T1 (FR-PACKAGING-009, AC-PACKAGING-024 source side): strict SemVer
// version source reader for the repo-root `sdk.yaml`.
// Only stdlib (node:fs); no tool invocations, no env/config reads.
const fs = require('node:fs');

function codedError(code, filePath, detail) {
  const err = new Error(`${code}: ${filePath}: ${detail}`);
  err.code = code;
  return err;
}

// Read the strict `version: MAJOR.MINOR.PATCH` line from a sdk.yaml file.
// Returns the version string. Throws coded errors that always name the
// path plus the cause; messages never echo unrelated file content.
function read_sdk_version(filePath) {
  const where = typeof filePath === 'string' ? filePath : String(filePath);
  let stat = null;
  try {
    stat = fs.statSync(where);
  } catch (_) {
    stat = null;
  }
  if (!stat) {
    throw codedError('missing-file', where, 'file not found (missing-file)');
  }
  if (!stat.isFile()) {
    throw codedError('missing-file', where, 'not a regular file (missing-file)');
  }
  let text;
  try {
    text = fs.readFileSync(where, 'utf8');
  } catch (err) {
    throw codedError('missing-file', where, `cannot read file (missing-file): ${err && err.message}`);
  }
  let found = null;
  let foundRaw = null;
  for (const line of String(text).split('\n')) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    const match = line.match(/^\s*version\s*:(.*)$/);
    if (!match) continue;
    found = match[1].trim();
    foundRaw = match[1];
    break;
  }
  if (found === null) {
    throw codedError('missing-version-key', where, 'no version key present (missing-version-key)');
  }
  if (found === '') {
    // Bare `version:` (no trailing space) is an empty key; a trailing
    // blank value (`version: `) is a malformed version. The contract
    // accepts either token for the bare form and requires bad-semver
    // for the blank form, so distinguish on the raw remainder.
    if (foundRaw === '') {
      throw codedError('missing-version-key', where, 'version key has no value (missing-version-key)');
    }
    throw codedError('bad-semver', where, 'invalid version "" (bad-semver)');
  }
  if (!/^[0-9]+\.[0-9]+\.[0-9]+$/.test(found)) {
    throw codedError('bad-semver', where, `invalid version "${found}" (bad-semver)`);
  }
  return found;
}

module.exports = { read_sdk_version };
