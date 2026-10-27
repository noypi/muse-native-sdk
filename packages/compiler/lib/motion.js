"use strict";

// T6 (AC-TSX-029): transition token maps — token-string values accepted,
// literal curve/duration spellings rejected. No I/O, never throws.
function toLoc(loc) {
  const l = loc && typeof loc === "object" ? loc : {};
  return {
    file: typeof l.file === "string" ? l.file : "unknown",
    line: typeof l.line === "number" ? l.line : 0,
    column: typeof l.column === "number" ? l.column : 0,
    nodePath: typeof l.nodePath === "string" ? l.nodePath : "",
  };
}

function isLiteralCurveString(s) {
  return s.indexOf("(") !== -1;
}

function checkTransitionProp(prop, value, loc) {
  try {
    if (typeof value === "string") {
      if (isLiteralCurveString(value)) {
        return {
          code: "TSX_TRANSITION_LITERAL",
          property: prop,
          ...toLoc(loc),
          severity: "error",
        };
      }
      return null;
    }
    if (typeof value === "number") {
      return {
        code: "TSX_TRANSITION_LITERAL",
        property: prop,
        ...toLoc(loc),
        severity: "error",
      };
    }
    if (value !== null && typeof value === "object") {
      return {
        code: "TSX_TRANSITION_LITERAL",
        property: prop,
        ...toLoc(loc),
        severity: "error",
      };
    }
    // Unknown input shapes (undefined, null, boolean, ...): fail closed
    // with a diagnostic, never throw.
    if (value === undefined || value === null) {
      return {
        code: "TSX_TRANSITION_LITERAL",
        property: prop,
        ...toLoc(loc),
        severity: "error",
      };
    }
    return {
      code: "TSX_TRANSITION_LITERAL",
      property: prop,
      ...toLoc(loc),
      severity: "error",
    };
  } catch {
    try {
      return {
        code: "TSX_TRANSITION_LITERAL",
        property: prop,
        file: "unknown",
        line: 0,
        column: 0,
        nodePath: "",
        severity: "error",
      };
    } catch {
      return null;
    }
  }
}

 // T8 (AC-TSX-031): sharedId pair type-check — matched id on two routes yields
// no diagnostic; lone id yields one warning per lone node. No I/O, never throws.
function checkSharedIds(nodes) {
  try {
    if (!Array.isArray(nodes)) return [];
    const groups = new Map();
    for (const n of nodes) {
      if (!n || typeof n !== "object") continue;
      const id = n.sharedId;
      if (typeof id !== "string" || id.length === 0) continue;
      let g = groups.get(id);
      if (!g) {
        g = { routes: new Set(), nodes: [] };
        groups.set(id, g);
      }
      g.routes.add(n.route);
      g.nodes.push(n);
    }
    const out = [];
    for (const [id, g] of groups) {
      if (g.routes.size >= 2) continue;
      for (const n of g.nodes) {
        const l = toLoc(n.loc);
        out.push({
          code: "TSX_SHARED_ID_LONE",
          sharedId: id,
          route: n.route,
          file: l.file,
          line: l.line,
          column: l.column,
          nodePath: l.nodePath,
          severity: "warning",
        });
      }
    }
    return out;
  } catch {
    return [];
  }
}

 // T9 (AC-TSX-032): route pattern validation — exact `===` membership in
// caller-supplied `knownPatterns`. Known name returns `null`; unknown
// returns an error diagnostic naming route + pattern. No I/O, never throws.
function checkRoutePattern(route, pattern, loc, knownPatterns) {
  try {
    const list = Array.isArray(knownPatterns) ? knownPatterns : [];
    for (const k of list) {
      if (k === pattern) return null;
    }
    const l = toLoc(loc);
    return {
      code: "TSX_UNKNOWN_PATTERN",
      route,
      pattern,
      file: l.file,
      line: l.line,
      column: l.column,
      nodePath: l.nodePath,
      severity: "error",
    };
  } catch {
    try {
      return {
        code: "TSX_UNKNOWN_PATTERN",
        route,
        pattern,
        file: "unknown",
        line: 0,
        column: 0,
        nodePath: "",
        severity: "error",
      };
    } catch {
      return null;
    }
  }
}

module.exports = { checkTransitionProp, checkSharedIds, checkRoutePattern };
