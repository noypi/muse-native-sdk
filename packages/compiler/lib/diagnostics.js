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

module.exports = { checkLiteralAgainstTokens };
