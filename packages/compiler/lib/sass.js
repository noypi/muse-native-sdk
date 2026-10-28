"use strict";

// T2 (AC-LAYOUT-018): build-time-only SASS subset compiler.
//
// Exposes the T2 interface:
//   compileSass(source, options?) -> { css, map? }
// Handles variables, nesting, mixins, and lighten/darken functions.
// Throws SassCompileError { message, line, column } on bad input.
//
// Pure JS with no `sass` runtime dependency: nothing under the shipped
// runtime surface imports a SASS preprocessor (packaging half of T2).

class SassCompileError extends Error {
  constructor(message, line, column) {
    super(message);
    this.name = "SassCompileError";
    this.line = line;
    this.column = column;
  }
}

function toLineColumn(source, index) {
  let line = 1;
  let lineStart = 0;
  for (let i = 0; i < index && i < source.length; i++) {
    if (source[i] === "\n") {
      line += 1;
      lineStart = i + 1;
    }
  }
  return { line, column: index - lineStart + 1 };
}

function blankPreservingNewlines(text) {
  return text.replace(/[^\n]/g, " ");
}

function parseHexColor(hex) {
  let h = hex.slice(1);
  if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
  if (!/^[0-9a-fA-F]{6}$/.test(h)) return null;
  return {
    r: parseInt(h.slice(0, 2), 16),
    g: parseInt(h.slice(2, 4), 16),
    b: parseInt(h.slice(4, 6), 16),
  };
}

function toHex(r, g, b) {
  const c = (v) => Math.max(0, Math.min(255, Math.round(v))).toString(16).padStart(2, "0");
  return `#${c(r)}${c(g)}${c(b)}`;
}

function parsePercent(s) {
  const m = s.trim().match(/^([0-9]*\.?[0-9]+)\s*%?$/);
  if (!m) return null;
  return Math.max(0, Math.min(100, parseFloat(m[1]))) / 100;
}

function evalColorFunctions(value) {
  return value.replace(
    /\b(lighten|darken)\s*\(\s*(#[0-9a-fA-F]{3,6})\s*,\s*([0-9]*\.?[0-9]+\s*%?)\s*\)/g,
    (whole, fn, hex, pctRaw) => {
      const rgb = parseHexColor(hex);
      const p = parsePercent(pctRaw);
      if (!rgb || p === null) return whole;
      if (fn === "lighten") {
        return toHex(
          rgb.r + (255 - rgb.r) * p,
          rgb.g + (255 - rgb.g) * p,
          rgb.b + (255 - rgb.b) * p,
        );
      }
      return toHex(rgb.r * (1 - p), rgb.g * (1 - p), rgb.b * (1 - p));
    },
  );
}

function combineSelector(parent, child) {
  const c = child.trim();
  if (!parent) return c;
  if (c.includes("&")) return c.split("&").join(parent);
  return `${parent} ${c}`;
}

// Recursive descent over a rule body. `rules` collects flattened
// { selector, declarations } entries in source order.
function parseRules(text, start, end, parent, rules, original) {
  let i = start;
  const decls = [];
  const skipWs = () => {
    while (i < end && /\s/.test(text[i])) i++;
  };
  while (i < end) {
    skipWs();
    if (i >= end) break;
    if (text[i] === "}") {
      i++;
      break;
    }
    // Look ahead: nested rule if the next `{` comes before the next `;`.
    const nextBrace = text.indexOf("{", i);
    const nextSemi = text.indexOf(";", i);
    const nextClose = text.indexOf("}", i);
    if (nextBrace !== -1 && nextBrace < end && (nextSemi === -1 || nextBrace < nextSemi)) {
      const selector = text.slice(i, nextBrace).trim();
      if (!selector) {
        const { line, column } = toLineColumn(original, i);
        throw new SassCompileError("Expected selector before '{'.", line, column);
      }
      // Find matching close brace.
      let depth = 1;
      let j = nextBrace + 1;
      while (j < end && depth > 0) {
        if (text[j] === "{") depth++;
        else if (text[j] === "}") depth--;
        j++;
      }
      if (depth !== 0) {
        const { line, column } = toLineColumn(original, nextBrace);
        throw new SassCompileError("Unclosed block.", line, column);
      }
      const full = combineSelector(parent, selector);
      const childDecls = [];
      parseRules(text, nextBrace + 1, j - 1, full, rules, original);
      void childDecls;
      i = j;
      continue;
    }
    // Declaration: `prop : value ;`
    const colon = text.indexOf(":", i);
    if (colon === -1 || colon >= end || (nextSemi !== -1 && colon > nextSemi)) {
      const { line, column } = toLineColumn(original, i);
      throw new SassCompileError("Expected ':' in declaration.", line, column);
    }
    if (nextSemi === -1 || nextSemi >= end || (nextClose !== -1 && nextClose < nextSemi && nextClose < (nextBrace === -1 ? Infinity : nextBrace))) {
      const { line, column } = toLineColumn(original, i);
      throw new SassCompileError("Expected ';' after declaration.", line, column);
    }
    const prop = text.slice(i, colon).trim();
    const value = text.slice(colon + 1, nextSemi).trim();
    if (!prop || !value) {
      const { line, column } = toLineColumn(original, i);
      throw new SassCompileError(
        `Invalid declaration${prop ? ` for property "${prop}"` : ""}: missing value.`,
        line,
        column,
      );
    }
    decls.push({ prop, value: evalColorFunctions(value) });
    i = nextSemi + 1;
  }
  if (parent && decls.length > 0) {
    rules.push({ selector: parent, declarations: decls });
  } else if (!parent && decls.length > 0) {
    const { line, column } = toLineColumn(original, start);
    throw new SassCompileError("Declarations must be inside a rule.", line, column);
  }
  return end;
}

function compileSass(source, options) {
  if (typeof source !== "string") {
    throw new SassCompileError("SASS source must be a string.", 1, 1);
  }
  const includePaths = options && options.includePaths;
  if (includePaths !== undefined && !Array.isArray(includePaths)) {
    throw new SassCompileError("`includePaths` must be an array of strings.", 1, 1);
  }
  // Work on a copy that preserves newlines (and thus line numbers) when
  // stripping variable declarations and mixin definitions.
  let text = source;

  const variables = new Map();
  text = text.replace(/^\s*\$([\w-]+)\s*:\s*([^;{}]+);/gm, (whole, name, value, offset) => {
    const v = value.trim();
    if (!v) {
      const { line, column } = toLineColumn(source, offset);
      throw new SassCompileError(`Invalid value for variable "$${name}".`, line, column);
    }
    variables.set(name, v);
    return blankPreservingNewlines(whole);
  });

  const mixins = new Map();
  // Non-greedy single-level mixin bodies (subset: no nested defs).
  const mixinRe = /@mixin\s+([\w-]+)\s*(?:\(([^)]*)\))?\s*\{([\s\S]*?)\}/g;
  text = text.replace(mixinRe, (whole, name, params, body, offset) => {
    void params;
    void offset;
    mixins.set(name, body);
    return blankPreservingNewlines(whole);
  });

  // Expand `@include name;` / `@include name(args);`.
  text = text.replace(/@include\s+([\w-]+)\s*(?:\(([^)]*)\))?\s*;/g, (whole, name, _args, offset) => {
    const body = mixins.get(name);
    if (body === undefined) {
      const { line, column } = toLineColumn(source, offset);
      throw new SassCompileError(`Unknown mixin "${name}".`, line, column);
    }
    return ` ${body} `;
  });
  const strayInclude = text.match(/@include\b/);
  if (strayInclude && strayInclude.index !== undefined) {
    const { line, column } = toLineColumn(source, strayInclude.index);
    throw new SassCompileError("Malformed @include.", line, column);
  }

  // Substitute variables.
  text = text.replace(/\$([\w-]+)/g, (whole, name, offset) => {
    if (!variables.has(name)) {
      const { line, column } = toLineColumn(source, offset);
      throw new SassCompileError(`Unknown variable "$${name}".`, line, column);
    }
    return variables.get(name);
  });

  // Top-level: sequence of `selector { ... }` blocks.
  const rules = [];
  let i = 0;
  const n = text.length;
  const skipTopWs = () => {
    while (i < n && /\s/.test(text[i])) i++;
  };
  skipTopWs();
  while (i < n) {
    const brace = text.indexOf("{", i);
    if (brace === -1) {
      const rest = text.slice(i).trim();
      if (rest) {
        const { line, column } = toLineColumn(source, i);
        throw new SassCompileError(`Unexpected content: "${rest.slice(0, 32)}".`, line, column);
      }
      break;
    }
    const selector = text.slice(i, brace).trim();
    if (!selector) {
      const { line, column } = toLineColumn(source, i);
      throw new SassCompileError("Expected selector before '{'.", line, column);
    }
    let depth = 1;
    let j = brace + 1;
    while (j < n && depth > 0) {
      if (text[j] === "{") depth++;
      else if (text[j] === "}") depth--;
      j++;
    }
    if (depth !== 0) {
      const { line, column } = toLineColumn(source, brace);
      throw new SassCompileError("Unclosed block.", line, column);
    }
    const full = combineSelector("", selector);
    parseRules(text, brace + 1, j - 1, full, rules, source);
    i = j;
    skipTopWs();
  }

  const css = rules
    .map((r) => `${r.selector} { ${r.declarations.map((d) => `${d.prop}: ${d.value};`).join(" ")} }`)
    .join("\n");
  return { css: css ? `${css}\n` : "" };
}

module.exports = { compileSass, SassCompileError };
