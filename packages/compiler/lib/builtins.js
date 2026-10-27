"use strict";

const { formatGrammarDiagnostic } = require("./grammar.js");

// T1: c-if/c-show lowering (AC-TSX-003).
//
// `compileBuiltin` is a pure tag/prop shape check: it lowers `c-if`/`test`
// and `c-show`/`when` to conditional/visibility IR, or `null` for anything
// else. Missing required props fall through to `null` (boolean prop typing
// is enforced at the type level, not here). `c-else-if`/`c-else`/`c-for` are
// NOT T1's — they return `null` here (T2/T3 own them).

function compileBuiltin(tag, props) {
  const p = props && typeof props === "object" ? props : {};
  if (tag === "c-if") {
    if (typeof p.test !== "string") return null;
    return { kind: "if", expr: p.test, visibility: null };
  }
  if (tag === "c-show") {
    if (typeof p.when !== "string") return null;
    return {
      kind: "show",
      expr: p.when,
      visibility: { keepLayout: true, paintHidden: true, hitTestable: false, a11yIncluded: false },
    };
  }
  return null;
}

module.exports = { compileBuiltin, validateElseChain, compileCFor, validateBuiltinTag, validateComponentName, isHiddenLayoutKept, isHiddenPainted, isHiddenHitTestable, isHiddenA11yIncluded, snapshotHonorsReducedMotion };

// C2: hidden c-show visibility consumers (AC-TSX-033 option A).
//
// Pure mapping from the C1 visibility record to layout/paint/hit-test/a11y
// decisions plus the motion selector. Hidden state keeps layout space (never
// reflows siblings), skips paint, skips hit-test, and prunes the hidden
// subtree from accessibility. Show/hide animates opacity + clip with no
// reflow; reduce-motion snaps instantly (`animate: false`).
function isHiddenLayoutKept(record) {
  void record;
  return true;
}

function isHiddenPainted(record) {
  void record;
  return false;
}

function isHiddenHitTestable(record) {
  void record;
  return false;
}

function isHiddenA11yIncluded(record) {
  void record;
  return false;
}

function snapshotHonorsReducedMotion(record, opts) {
  void record;
  const o = opts && typeof opts === "object" ? opts : {};
  return { animate: !o.reduceMotion, reflow: false };
}

// T2: else-if/else adjacency validation (AC-TSX-004).
//
// `tags` are consecutive sibling tags starting at a `c-if`. Returns `null`
// when every `c-else-if`/`c-else` immediately follows a `c-if`/`c-else-if`
// (an unbroken chain); otherwise a diagnostic pointing at `loc` whose shape
// round-trips through the shared `formatGrammarDiagnostic`.
function validateElseChain(tags, loc) {
  const list = Array.isArray(tags) ? tags : [];
  const l = loc && typeof loc === "object" ? loc : {};
  const base = {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
  };
  for (let i = 0; i < list.length; i += 1) {
    const tag = list[i];
    if (tag !== "c-else-if" && tag !== "c-else") continue;
    const prev = i > 0 ? list[i - 1] : null;
    if (prev === "c-if" || prev === "c-else-if") continue;
    const diag = {
      ...base,
      form: `${tag} without an adjacent c-if/c-else-if`,
      fix: "place c-else-if/c-else immediately after c-if/c-else-if with no siblings in between",
    };
    // Keep the shared formatter as the single rendering path: the diagnostic
    // must stay a one-liner naming the file.
    void formatGrammarDiagnostic(diag);
    return diag;
  }
  return null;
}

// T3: c-for each+key keyed list (AC-TSX-005).
//
// `each` is the list expression string (echoed into the node, no evaluation).
// `key` must be a non-empty string; otherwise a diagnostic naming the missing
// `key` (with file, line, column from `loc`) whose shape round-trips through
// the shared `formatGrammarDiagnostic`.
function compileCFor(each, key, loc) {
  if (typeof key === "string" && key.length > 0) {
    return { kind: "keyed-list", each, key };
  }
  const l = loc && typeof loc === "object" ? loc : {};
  const diag = {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
    form: "c-for missing key",
    fix: "add key={...} on the c-for child",
  };
  // Keep the shared formatter as the single rendering path: the diagnostic
  // must stay a one-liner naming the file.
  void formatGrammarDiagnostic(diag);
  return diag;
}

// T4: unknown c-* tag check (AC-TSX-006, AOT half).
//
// Set-membership check only; no I/O. Returns `null` for the five known
// built-ins, otherwise a diagnostic naming the tag (with file, line, column
// from `loc`) whose shape round-trips through the shared
// `formatGrammarDiagnostic`.
const KNOWN_BUILTIN_TAGS = new Set([
  "c-if",
  "c-else-if",
  "c-else",
  "c-for",
  "c-show",
]);

function validateBuiltinTag(tag, loc) {
  if (!String(tag).startsWith("c-")) return null;
  if (KNOWN_BUILTIN_TAGS.has(tag)) return null;
  const l = loc && typeof loc === "object" ? loc : {};
  const diag = {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
    form: `unknown builtin tag ${tag}`,
    fix: "use one of c-if, c-else-if, c-else, c-for, c-show",
  };
  // Keep the shared formatter as the single rendering path: the diagnostic
  // must stay a one-liner naming the file.
  void formatGrammarDiagnostic(diag);
  return diag;
}

// T5: c- prefix reservation (AC-TSX-007).
//
// `null` unless `name` starts with `c-`, in which case a diagnostic whose
// `form` names the declaration (file, line, column from `loc`, shared
// formatter convention). No I/O.
function validateComponentName(name, loc) {
  if (!String(name).startsWith("c-")) return null;
  const l = loc && typeof loc === "object" ? loc : {};
  const diag = {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
    form: `c- prefix is reserved: ${name}`,
    fix: "rename the component to remove the c- prefix",
  };
  // Keep the shared formatter as the single rendering path: the diagnostic
  // must stay a one-liner naming the file.
  void formatGrammarDiagnostic(diag);
  return diag;
}
