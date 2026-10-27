"use strict";

// Conditional IR shape classification (AC-TSX-011).
//
// `compileConditional` is a pure shape check: it reports which of the three
// allowed conditional forms a source string holds, or `null` for anything
// else. Boolean-ness of an `&&` left operand is NOT decided here — T5's
// `validateConditional` owns the non-boolean rejection, so every `&&`
// expression shape-matches as `boolean-and` regardless of operand type.

function stripStrings(src) {
  return String(src)
    .replace(/'(?:[^'\\\n]|\\.)*'/g, (m) => " ".repeat(m.length))
    .replace(/"(?:[^"\\\n]|\\.)*"/g, (m) => " ".repeat(m.length))
    .replace(/`(?:[^`\\]|\\.)*`/g, (m) => " ".repeat(m.length));
}

// Minimal forward-compatible early-return detection: a whole function body
// holding zero or more `if (cond) return <X/>;` guards followed by exactly one
// final `return <Y/>;`. Statement-position aware at the function-body top
// level only. When T5 lands `validateEarlyReturns`, rewire to it ONLY if the
// tests require it; otherwise this stays.
function isGuardedEarlyReturnBody(src) {
  const text = String(src ?? "");
  if (!/\bfunction\b/.test(stripStrings(text))) return false;
  // Body brace is the `{` opening the parameter list's close `)` — not the
  // first `{` in the source (destructured params like `({ ok })` hold braces).
  const openMatch = text.match(/\)\s*\{/);
  const close = text.lastIndexOf("}");
  if (!openMatch || close <= openMatch.index) return false;
  const open = openMatch.index + openMatch[0].length - 1;
  const code = stripStrings(text.slice(open + 1, close));

  // Consume leading `if (cond) return ...;` guards. Conditions in this
  // position are simple slot-level predicates (no braces/semicolons inside).
  let rest = code;
  const guardRe = /^\s*if\s*\([^;{}]*\)\s*return\b[^;]*;\s*/;
  while (guardRe.test(rest)) {
    rest = rest.replace(guardRe, "");
  }

  // What remains must be exactly one final `return ...;` and nothing else.
  if (!/^\s*return\b[^;]*;\s*$/.test(rest)) return false;
  // At least the final return must exist (guards are optional).
  return /\breturn\b/.test(code);
}

function compileConditional(slotSource) {
  const src = String(slotSource ?? "");

  // Whole-function shape first: it is disjoint from slot expressions.
  if (isGuardedEarlyReturnBody(src)) {
    return { kind: "early-return" };
  }

  const code = stripStrings(src);

  // Ternary: `?` + `:` present at slot level (light check, strings blanked).
  if (/\?/.test(code) && /:/.test(code)) {
    return { kind: "ternary" };
  }

  // Boolean-`&&`: shape match only — no type-check of the left operand.
  if (/&&/.test(code)) {
    return { kind: "boolean-and" };
  }

  return null;
}

// --- T5: non-boolean `&&` rejection + early-return validation (AC-TSX-012/013)
//
// `formatGrammarDiagnostic` is pinned by T1 as the one-line diagnostic
// renderer; diagnostics below carry the full `GrammarDiagnostic` shape
// (`SlotLocation & { form, fix }`) so the formatter names the file.
const { formatGrammarDiagnostic } = require("./grammar.js");

function validateConditional(slotSource, ctx) {
  const code = stripStrings(String(slotSource ?? ""));
  // T4's domain: slots without an `&&` expression always pass here.
  if (!/&&/.test(code)) return null;
  const loc = ctx && typeof ctx === "object" ? ctx : {};
  const leftType = typeof loc.leftType === "string" ? loc.leftType : "";
  // Caller-supplied type string per plan A4 — no type inference here.
  if (leftType.trim().toLowerCase() === "boolean") return null;
  return {
    file: typeof loc.file === "string" ? loc.file : "unknown",
    line: typeof loc.line === "number" ? loc.line : 0,
    column: typeof loc.column === "number" ? loc.column : 0,
    nodePath: typeof loc.nodePath === "string" ? loc.nodePath : "",
    form: `non-boolean && left operand of type ${loc.leftType}`,
    fix: "coerce the left operand to a boolean (e.g. Boolean(x) or x != null) before using &&",
  };
}

function validateEarlyReturns(functionSource, filePath) {
  const file = typeof filePath === "string" ? filePath : "unknown";
  const fail = (form, fix) => ({
    file,
    line: 1,
    column: 1,
    nodePath: "",
    form,
    fix,
  });
  const guardsOnlyFix =
    "keep only `if (cond) return <X/>` guards before a single final `return <Y/>`";
  const text = String(functionSource ?? "");
  const openMatch = text.match(/\)\s*\{/);
  const close = text.lastIndexOf("}");
  if (!openMatch || close <= openMatch.index) {
    return fail("missing final return", guardsOnlyFix);
  }
  const open = openMatch.index + openMatch[0].length - 1;
  const code = stripStrings(text.slice(open + 1, close));

  // Consume leading `if (cond) return ...;` guards — same shape T4 accepts.
  let rest = code;
  const guardRe = /^\s*if\s*\([^;{}]*\)\s*return\b[^;]*;\s*/;
  while (guardRe.test(rest)) {
    rest = rest.replace(guardRe, "");
  }

  // What remains must be exactly one final `return ...;` and nothing else.
  if (/^\s*return\b[^;]*;\s*$/.test(rest)) return null;
  if (rest.trim() === "" || !/\breturn\b/.test(code)) {
    return fail("missing final return", guardsOnlyFix);
  }
  return fail(
    "misplaced return: only `if (cond) return <X/>` guards may precede a single final return",
    guardsOnlyFix,
  );
}

module.exports = { compileConditional, validateConditional, validateEarlyReturns };
