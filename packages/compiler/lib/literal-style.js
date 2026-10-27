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

// T3 (AC-TSX-026): literal style values always compile — never a rejection.
// Unlike T8's `validateInlineStyle`, this checker does NOT enforce
// camelCase/units; every literal colour/dimension/style value returns null.
// Non-object input returns null. No I/O, never throws.
function checkLiteralStyle(style, loc) {
  try {
    void toLoc(loc);
    void style;
    return null;
  } catch {
    return null;
  }
}

module.exports = { checkLiteralStyle };
