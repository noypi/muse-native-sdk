'use strict';
// T5a — render-path exception-ban gate (NFR-BUILD-016 / AC-BUILD-059).
// Consumes a T4-shaped flags manifest (one flag per line, `#` comments;
// see toolchain/ndk/release-flags.txt) and fails any render-path translation
// unit that enables exceptions. The ban is per-TU: a single enabling token
// anywhere in the file fails the gate, even alongside `-fno-exceptions`.

const fs = require('node:fs');
const path = require('node:path');

// Exceptions-enabling tokens (lowercased for case-insensitive match).
// NOTE: `-fno-exceptions` is the clean token and must never match here.
const ENABLING_TOKENS = new Set([
  '-fexceptions',
  '-fobjc-exceptions',
  '-fcxx-exceptions',
  '-fseh-exceptions',
]);

function stripComments(text) {
  return text
    .split('\n')
    .map((line) => {
      const hash = line.indexOf('#');
      return hash === -1 ? line : line.slice(0, hash);
    })
    .join('\n');
}

function findEnablingTokens(text) {
  const found = [];
  for (const raw of stripComments(text).split(/\s+/)) {
    const token = raw.trim();
    if (!token) continue;
    if (ENABLING_TOKENS.has(token.toLowerCase())) found.push(token);
  }
  return found;
}

// verifyRenderPathFlags(flagsPath: string): { ok: boolean, violations: string[] }
// Fails any render-path translation unit enabling exceptions; violations name
// the file and the offending flag. Unreadable/missing inputs fail closed.
function verifyRenderPathFlags(flagsPath) {
  const name = path.basename(String(flagsPath));
  let text;
  try {
    text = fs.readFileSync(flagsPath, 'utf8');
  } catch (err) {
    return {
      ok: false,
      violations: [`${name}: cannot read render-path flags file '${flagsPath}' (${err.code || err.message})`],
    };
  }
  const violations = findEnablingTokens(text).map(
    (token) => `${name}: render-path enables exceptions via '${token}'`
  );
  return { ok: violations.length === 0, violations };
}

module.exports = { verifyRenderPathFlags };
