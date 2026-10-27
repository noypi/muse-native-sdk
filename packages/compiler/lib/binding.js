"use strict";

const { formatGrammarDiagnostic } = require("./grammar.js");

// T1: bare writable `value` → two-way binding (AC-TSX-016).
//
// `compileValueBinding` is a pure shape check: a bare identifier-shape
// `value` (echoed, never evaluated) with `isBareWritable === true` lowers
// to a two-way node with read+write edges on the target. Anything else
// falls through to the T2 gate: `hasOnChange === true` lowers to a
// one-way node echoing the value expression, otherwise a diagnostic naming
// the value form (with file, line, column from `loc`) whose shape
// round-trips through the shared `formatGrammarDiagnostic`.
const BARE_WRITABLE_RE = /^[A-Za-z_$][\w$]*$/;

function toLoc(loc) {
  const l = loc && typeof loc === "object" ? loc : {};
  return {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
  };
}

function compileValueBinding(args, loc) {
  const a = args && typeof args === "object" ? args : {};
  const value = typeof a.value === "string" ? a.value : "";
  const v = String(value).trim();
  if (BARE_WRITABLE_RE.test(v) && a.isBareWritable === true) {
    return {
      kind: "binding",
      mode: "two-way",
      target: v,
      reads: [v],
      writes: [v],
    };
  }
  if (a.hasOnChange === true) {
    return { kind: "binding", mode: "one-way", expr: value };
  }
  const diag = {
    ...toLoc(loc),
    form: `value "${value}" requires onChange`,
    fix: "add onChange={...} alongside value={...}, or bind a bare writable identifier",
  };
  // Keep the shared formatter as the single rendering path: the diagnostic
  // must stay a one-liner naming the file.
  void formatGrammarDiagnostic(diag);
  return diag;
}

// T2: one-way `valueReadonly` + non-bare `value` `onChange` gate (AC-TSX-017).
//
// Pure shape check: `expr` is echoed verbatim (never evaluated, no I/O).
// `hasOnChange === true` lowers to a one-way node; otherwise a diagnostic
// naming the missing `onChange` (file, line, column from `loc`) whose shape
// round-trips through the shared `formatGrammarDiagnostic`.
function compileReadonlyBinding(args, loc) {
  const a = args && typeof args === "object" ? args : {};
  const expr = typeof a.expr === "string" ? a.expr : "";
  if (a.hasOnChange === true) {
    return { kind: "binding", mode: "one-way", expr };
  }
  const diag = {
    ...toLoc(loc),
    form: `valueReadonly "${expr}" requires onChange`,
    fix: "add onChange={...} alongside valueReadonly={...}",
  };
  // Keep the shared formatter as the single rendering path: the diagnostic
  // must stay a one-liner naming the file.
  void formatGrammarDiagnostic(diag);
  return diag;
}

// T3: spread inert + AOT `model` rejection (AC-TSX-018 AOT half).
//
// Spread never synthesizes two-way: always inert (`null`), even when the
// spread carries a `value`. `validateBindingProp` rejects only `model` with
// a diagnostic naming the unknown prop (file, line, column from `loc`)
// whose shape round-trips through the shared `formatGrammarDiagnostic`;
// known `value`/`valueReadonly`/`onChange` (and any other prop) pass.
// The permissive default is deliberate: non-binding unknown props are
// rejected by the generic unknown-prop path, not by this gate.
function compileSpreadBinding(args, loc) {
  void args;
  void loc;
  return null;
}

function validateBindingProp(prop, loc) {
  if (prop !== "model") {
    return null;
  }
  const diag = {
    ...toLoc(loc),
    form: "unknown prop model",
    fix: "remove model={...}; use value={...} with onChange={...} or valueReadonly={...} with onChange={...}",
  };
  // Keep the shared formatter as the single rendering path: the diagnostic
  // must stay a one-liner naming the file.
  void formatGrammarDiagnostic(diag);
  return diag;
}

module.exports = { compileValueBinding, compileReadonlyBinding, compileSpreadBinding, validateBindingProp };
