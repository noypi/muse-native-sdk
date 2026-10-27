"use strict";

const { formatGrammarDiagnostic } = require("./grammar.js");

// T8: inline style validation (AC-TSX-019).
//
// Every key must be camelCase (no `-`) and every value must be a number
// (dp) or a string. Returns null when valid, otherwise a diagnostic naming
// the offending key. Non-object input returns a diagnostic, never throws.

function toLoc(loc) {
  const l = loc && typeof loc === "object" ? loc : {};
  return {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
  };
}

function styleDiagnostic(loc, form, fix) {
  const diag = { ...toLoc(loc), form, fix };
  // Keep the shared formatter as the single rendering path.
  void formatGrammarDiagnostic(diag);
  return diag;
}

function validateInlineStyle(style, loc) {
  if (style === null || typeof style !== "object" || Array.isArray(style)) {
    return styleDiagnostic(
      loc,
      "invalid inline style object",
      "pass an object with camelCase keys and number (dp) or string values",
    );
  }
  for (const [key, value] of Object.entries(style)) {
    if (key.includes("-")) {
      return styleDiagnostic(
        loc,
        `inline style key must be camelCase: ${key}`,
        "use camelCase style keys",
      );
    }
    if (typeof value !== "number" && typeof value !== "string") {
      return styleDiagnostic(
        loc,
        `inline style value for ${key} must be a number (dp) or string`,
        "use a number (dp) or string value",
      );
    }
  }
  return null;
}

module.exports = { validateInlineStyle, resolveClassNames };

// T9: class resolution vs co-located stylesheets (AC-TSX-020).
//
// Pure set-membership: every entry of `classes` must be in the caller-supplied
// `known` list (parsed co-located CSS3/SASS class list per plan A4). No CSS
// parsing, no fs reads here. Returns null when valid (including `[]`);
// otherwise a diagnostic naming the first unknown class, the component, and
// the stylesheet searched. Defensive coercion: non-array input returns a
// diagnostic, never throws.
function resolveClassNames(classes, ctx) {
  const c = ctx && typeof ctx === "object" ? ctx : {};
  const component = typeof c.component === "string" ? c.component : "unknown";
  const stylesheet =
    typeof c.stylesheet === "string" ? c.stylesheet : "unknown";
  const known = Array.isArray(c.known) ? c.known : [];
  const knownSet = new Set(known.filter((k) => typeof k === "string"));
  const loc = { file: stylesheet, line: 0, column: 0, nodePath: component };
  if (!Array.isArray(classes)) {
    return styleDiagnostic(
      loc,
      `invalid class list for ${component} searched in ${stylesheet}`,
      `check ${stylesheet}`,
    );
  }
  for (const cls of classes) {
    if (typeof cls !== "string" || !knownSet.has(cls)) {
      return styleDiagnostic(
        loc,
        `unknown class ${String(cls)} in ${component} searched in ${stylesheet}`,
        `check ${stylesheet}`,
      );
    }
  }
  return null;
}
