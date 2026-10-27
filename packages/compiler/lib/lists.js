"use strict";

const { formatGrammarDiagnostic } = require("./grammar.js");

// T6: canonical .map() keyed list (AC-TSX-014).
//
// Parses the strict canonical shape:
//   receiver.map((param) => <Tag key={...} />)
// expression-body single self-closing JSX. Returns the keyed-list node,
// or a diagnostic naming the missing `key` when the mapped element carries
// none. Non-canonical shapes (block bodies, non-JSX) return `null` —
// T7 owns the callback-shape rejection. No I/O.

function toLoc(loc) {
  const l = loc && typeof loc === "object" ? loc : {};
  return {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
  };
}

function missingKeyDiagnostic(loc) {
  const diag = {
    ...toLoc(loc),
    form: "map() element missing key",
    fix: "add key={...} on the mapped element",
  };
  // Keep the shared formatter as the single rendering path.
  void formatGrammarDiagnostic(diag);
  return diag;
}

function compileMapList(sourceText, loc) {
  const src = String(sourceText ?? "");
  const m =
    /^\s*([A-Za-z_$][\w$]*(?:\s*\.\s*[A-Za-z_$][\w$]*)*)\s*\.map\s*\(\s*\(?\s*([A-Za-z_$][\w$]*)\s*(?:,\s*[A-Za-z_$][\w$]*\s*)?\)?\s*=>\s*([\s\S]*?)\s*\)\s*$/.exec(
      src,
    );
  if (!m) return null;
  const each = m[1].replace(/\s+/g, "");
  const body = m[3].trim();
  // Block bodies (`=> { ... }`) and non-JSX bodies are non-canonical —
  // T7's domain. Return null here, never a diagnostic.
  if (!/^<[\s\S]*\/>$/.test(body)) return null;
  if (/^\{/.test(body)) return null;
  const keyMatch = /\bkey\s*=\s*\{([^}]*)\}/.exec(body);
  if (!keyMatch) return missingKeyDiagnostic(loc);
  const key = keyMatch[1].trim();
  if (key.length === 0) return missingKeyDiagnostic(loc);
  return { kind: "keyed-list", each, key };
}

module.exports = { compileMapList, validateMapCallback };

// T7: callback-shape rejection (AC-TSX-015).
//
// Accepts only the canonical expression-body single JSX element:
//   receiver.map((param) => <Tag key={...} />)
// Block bodies (`=> { ... }`) and non-single-JSX expression bodies return a
// diagnostic naming the shape; canonical shape returns null. No I/O.
function validateMapCallback(sourceText, loc) {
  const src = String(sourceText ?? "");
  const arrow = src.indexOf("=>");
  if (arrow < 0) {
    return callbackDiagnostic(loc, "non-JSX body");
  }
  let body = src.slice(arrow + 2).trim();
  // Strip the closing paren of the `.map(...)` call.
  if (/\)\s*$/.test(body)) body = body.replace(/\)\s*$/, "").trim();
  // Block body (`=> { ... }`).
  if (/^\{/.test(body)) {
    return callbackDiagnostic(loc, "block body");
  }
  // Single self-closing JSX element.
  if (/^<[A-Za-z_$][\w$]*\b[\s\S]*\/>$/.test(body)) return null;
  // Single simple (paired) JSX element.
  if (
    /^<[A-Za-z_$][\w$]*\b[^>]*>[\s\S]*<\/\s*[A-Za-z_$][\w$]*\s*>$/.test(body)
  ) {
    return null;
  }
  return callbackDiagnostic(loc, "non-JSX body");
}

function callbackDiagnostic(loc, shape) {
  const diag = {
    ...toLoc(loc),
    form: `map() callback must be a single JSX element (${shape})`,
    fix: "use (x) => <Tag key={...} />",
  };
  // Keep the shared formatter as the single rendering path.
  void formatGrammarDiagnostic(diag);
  return diag;
}
