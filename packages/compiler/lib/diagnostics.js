"use strict";

function toLoc(loc) {
  const l = loc && typeof loc === "object" ? loc : {};
  return {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
  };
}

// T4 (AC-TSX-027): advisory CAPO0012 on token-value duplication.
//
// Strict-equality match of `literal` against each `value` in
// `knownTokenValues` (number 12 !== string "12"); first match wins.
// Empty/missing list returns null. Match returns a plain-object
// advisory diagnostic with exitCode 0; non-match returns null.
// No I/O, never throws.
function checkLiteralAgainstTokens(literal, loc, knownTokenValues) {
  try {
    const list = Array.isArray(knownTokenValues) ? knownTokenValues : [];
    for (const entry of list) {
      if (!entry || typeof entry !== "object") continue;
      if (typeof entry.token !== "string" || entry.token === "") continue;
      if (entry.value === literal) {
        return {
          code: "CAPO0012",
          literal,
          ...toLoc(loc),
          token: entry.token,
          severity: "advisory",
          exitCode: 0,
        };
      }
    }
    return null;
  } catch {
    return null;
  }
}

// Error thrown for an invalid explicit severity. Mirrors the shape of
// `ConfigError` in `packages/cli/lib/config.js` (the compiler must not
// depend on the CLI layer): carries the diagnostic code, the config label,
// and the accepted values. Message reads
// `Error: <code> in <config>: expected error|warn|off`.
class ConfigError extends Error {
  constructor(key, file, expected) {
    super(`Error: ${key} in ${file}: expected ${expected}`);
    this.name = 'ConfigError';
    this.key = key;
    this.file = file;
    this.expected = expected;
  }
}

const SEVERITIES = new Set(['error', 'warn', 'off']);

// T4 (AC-WORKFLOW-013): effective severity for a diagnostic code.
//
// Precedence: an explicit `configured` value wins when it validates
// (`error|warn|off`, else throws `ConfigError(code, '<config>',
// 'error|warn|off')`); otherwise the default is `error` when
// `packageType === 'library'`, else `warn`. Never throws on unknown
// `code` — the default applies. Pure, no I/O.
function resolveSeverity(code, configured, packageType) {
  if (configured !== undefined) {
    if (!SEVERITIES.has(configured)) {
      throw new ConfigError(code, '<config>', 'error|warn|off');
    }
    return configured;
  }
  return packageType === 'library' ? 'error' : 'warn';
}

module.exports = { checkLiteralAgainstTokens, resolveSeverity, ConfigError };
