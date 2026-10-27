"use strict";

const crypto = require("node:crypto");

const MOVE_FIX = "move this logic into computed(() => ...) and bind the result";
const CALL_FIX =
  "put the call in computed(() => ...) and bind the result, or add /** @pure */ above the callee if it only reads its arguments";

function toLoc(loc) {
  const l = loc && typeof loc === "object" ? loc : {};
  return {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
  };
}

function makeDiagnostic(loc, form, fix) {
  return { ...toLoc(loc), form, fix };
}

function stripStrings(src) {
  return String(src)
    .replace(/'(?:[^'\\\n]|\\.)*'/g, (m) => " ".repeat(m.length))
    .replace(/"(?:[^"\\\n]|\\.)*"/g, (m) => " ".repeat(m.length))
    .replace(/`(?:[^`\\]|\\.)*`/g, (m) => " ".repeat(m.length));
}

function hasAssignment(src) {
  // Slot-level heuristic: `=` outside `==`/`===`/`=>`/JSX attrs.
  let s = stripStrings(String(src));
  // Remove arrow tokens first so `=>` never reads as assignment.
  s = s.replace(/=>/g, "  ");
  // Remove JSX attribute bindings like key={...} / a="..." / a='...'.
  s = s.replace(
    /[A-Za-z_$][\w$.-]*\s*=\s*(\{[^{}]*\}|"[^"]*"|'[^']*')/g,
    (m) => " ".repeat(m.length),
  );
  // Remaining bare `=` (not part of ==, ===, !=, >=, <=) is an assignment.
  return /(?<![=!<>])=(?![=])/.test(s);
}

function validateExpressionSlot(slotSource, loc) {
  try {
    const src = String(slotSource ?? "");
    const code = stripStrings(src);

    // `new` expression.
    if (/\bnew\s+[A-Za-z_$]/.test(code)) {
      return makeDiagnostic(loc, "new expression", MOVE_FIX);
    }

    // `await`.
    if (/\bawait\b/.test(code)) {
      return makeDiagnostic(loc, "await", MOVE_FIX);
    }

    // Statements in an expression slot: semicolons or statement keywords.
    if (
      /\b(if|for|while|return|switch|throw|let|const|var|function|class)\b/.test(
        code,
      ) ||
      /;/.test(code)
    ) {
      return makeDiagnostic(loc, "statement in expression", MOVE_FIX);
    }

    const trimmed = src.trim();

    // Dynamic tag name: the slot itself is a JSX element with a
    // non-static (capitalized) tag name. A `<Row ... />` nested inside a
    // canonical `items.map(...)` slot must not trip this rule.
    if (/^<\s*[A-Z]/.test(trimmed)) {
      return makeDiagnostic(loc, "dynamic tag name", MOVE_FIX);
    }

    // Assignment (`=` outside `==`/`===`/`=>`/JSX attrs).
    if (hasAssignment(src)) {
      return makeDiagnostic(loc, "assignment", MOVE_FIX);
    }

    // Calls: allowed only with a `/** @pure */` annotation or as the
    // canonical `.map()` shape. Anything else is rejected, naming the callee.
    if (/\/\*\*\s*@pure\s*\*\//.test(src)) {
      return null;
    }
    if (/\.map\s*\(/.test(code)) {
      return null;
    }
    const callRe = /([A-Za-z_$][\w$]*(?:\s*\.\s*[A-Za-z_$][\w$]*)*)\s*\(/g;
    let m;
    while ((m = callRe.exec(code)) !== null) {
      const rawName = m[1].replace(/\s+/g, "");
      const name = rawName.replace(/\s*\.\s*/g, ".");
      if (!name) continue;
      return makeDiagnostic(loc, `unannotated call to ${name}`, CALL_FIX);
    }

    return null;
  } catch {
    return makeDiagnostic(loc, "statement in expression", MOVE_FIX);
  }
}

function formatGrammarDiagnostic(d) {
  const diag = d && typeof d === "object" ? d : {};
  return `${diag.file}:${diag.line}:${diag.column}: ${diag.form} at ${diag.nodePath}. Fix: ${diag.fix}`;
}

function compileExpressionSlot(slotSource) {
  const src = String(slotSource ?? "");
  // Any slot T1 rejects emits nothing.
  const rejected = validateExpressionSlot(src, {
    file: "",
    line: 0,
    column: 0,
    nodePath: "",
  });
  if (rejected !== null) return null;

  const trimmed = src.trim();
  const code = stripStrings(src);

  // Specificity first: a `/** @pure */`-annotated call is a pure-call;
  // unannotated calls never reach here (T1 rejects them → null above).
  if (/\/\*\*\s*@pure\s*\*\//.test(src)) {
    return { kind: "pure-call", pure: true };
  }

  // Canonical `.map()`: `items.map((item) => <Row key={item.id} />)` —
  // arrow with an expression body returning one JSX element. Block-body
  // (`=> { ... }`) or non-JSX callbacks are FR-TSX-005 domain → null.
  if (/\.map\s*\(/.test(code)) {
    const canonicalMap =
      /^\s*[A-Za-z_$][\w$]*(?:\s*\.\s*[A-Za-z_$][\w$]*)*\s*\.map\s*\(\s*\(?\s*[A-Za-z_$][\w$]*\s*(?:,\s*[A-Za-z_$][\w$]*\s*)?\)?\s*=>\s*<\s*[A-Za-z_$][\w$]*\b[^;]*\/>\s*\)\s*$/.test(
        src,
      );
    return canonicalMap ? { kind: "map", pure: false } : null;
  }

  // Ternary: `? ... :` outside strings.
  if (/\?/.test(code) && /:/.test(code)) {
    return { kind: "ternary", pure: false };
  }

  // Logical: `&&` / `||` outside strings.
  if (/&&/.test(code) || /\|\|/.test(code)) {
    return { kind: "logical", pure: false };
  }

  // Comparison: `==`/`===`/`!=`/`!==`/`>`/`>=`/`<`/`<=` outside strings.
  // (`=>` arrows are removed first so they never read as comparison.)
  const noArrows = code.replace(/=>/g, "  ");
  if (
    /===|!==|>=|<=|==|!=|(?<![=!<>])>(?![=])|(?<![=!<>])<(?![=])/.test(noArrows)
  ) {
    return { kind: "comparison", pure: false };
  }

  // Arithmetic: `+`/`-`/`*`/`/`/`%` outside strings.
  if (/[+\-*/%]/.test(noArrows)) {
    return { kind: "arithmetic", pure: false };
  }

  // Literal: quoted string, number, or boolean/null/undefined keyword.
  if (
    /^(['"`]).*\1$/.test(trimmed) ||
    /^-?\d/.test(trimmed) ||
    /^(true|false|null|undefined)$/.test(trimmed)
  ) {
    return { kind: "literal", pure: false };
  }

  // Member access: `a.value` outside strings.
  if (/\./.test(code)) {
    return { kind: "member", pure: false };
  }

  // Plain identifier.
  if (/^[A-Za-z_$][\w$]*$/.test(trimmed)) {
    return { kind: "identifier", pure: false };
  }

  return null;
}

const liftedNodeIds = new Set();

function liftPureSlot(slotSource, calleeName) {
  const src = String(slotSource ?? "");
  const hash = crypto.createHash("sha1").update(src).digest("hex").slice(0, 8);
  const id = `__c_${hash}`;
  liftedNodeIds.add(id);
  return { id, computedSource: `computed(() => ${src})` };
}

function listLiftedNodeIds() {
  return [...liftedNodeIds].sort();
}

module.exports = { validateExpressionSlot, formatGrammarDiagnostic, compileExpressionSlot, liftPureSlot, listLiftedNodeIds };
