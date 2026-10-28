"use strict";

// T1 (AC-LAYOUT-017): supported CSS3 subset with build-time errors.
//
// `engine/layout/css-subset.json` is the single source of truth; this module
// mirrors it (with an embedded fallback so the compiler never depends on
// disk layout) and exposes the T1 interface:
//   isSupported(property) -> boolean
//   validateStylesheet(source) -> [{ property, line, column }]
// `validateStylesheet` returns one entry per unsupported property with a
// 1-based source location, or [] when fully supported. Never throws on
// valid input; non-string input yields [].

let listed = null;
try {
  // eslint-disable-next-line global-require
  listed = require("../../../engine/layout/css-subset.json").properties;
} catch {
  listed = null;
}

const FALLBACK = [
  "align-content",
  "align-items",
  "align-self",
  "background",
  "background-color",
  "border",
  "border-bottom",
  "border-bottom-left-radius",
  "border-bottom-right-radius",
  "border-color",
  "border-left",
  "border-radius",
  "border-right",
  "border-style",
  "border-top",
  "border-top-left-radius",
  "border-top-right-radius",
  "border-width",
  "box-shadow",
  "box-sizing",
  "color",
  "column-gap",
  "cursor",
  "display",
  "flex",
  "flex-basis",
  "flex-direction",
  "flex-flow",
  "flex-grow",
  "flex-shrink",
  "flex-wrap",
  "font",
  "font-family",
  "font-size",
  "font-style",
  "font-weight",
  "gap",
  "grid",
  "grid-area",
  "grid-column",
  "grid-gap",
  "grid-row",
  "grid-template",
  "grid-template-areas",
  "grid-template-columns",
  "grid-template-rows",
  "height",
  "justify-content",
  "letter-spacing",
  "line-height",
  "margin",
  "margin-bottom",
  "margin-left",
  "margin-right",
  "margin-top",
  "max-height",
  "max-width",
  "min-height",
  "min-width",
  "opacity",
  "order",
  "outline",
  "outline-color",
  "outline-width",
  "overflow",
  "overflow-x",
  "overflow-y",
  "padding",
  "padding-bottom",
  "padding-left",
  "padding-right",
  "padding-top",
  "row-gap",
  "text-align",
  "text-decoration",
  "text-overflow",
  "text-shadow",
  "text-transform",
  "transform",
  "transform-origin",
  "transition",
  "transition-delay",
  "transition-duration",
  "transition-property",
  "transition-timing-function",
  "visibility",
  "white-space",
  "width",
];

const SUPPORTED = new Set(
  Array.isArray(listed) && listed.length > 0 ? listed : FALLBACK,
);

function isSupported(property) {
  if (typeof property !== "string") return false;
  return SUPPORTED.has(property);
}

function toLineColumn(source, index) {
  let line = 1;
  let lineStart = 0;
  for (let i = 0; i < index; i++) {
    if (source[i] === "\n") {
      line += 1;
      lineStart = i + 1;
    }
  }
  return { line, column: index - lineStart + 1 };
}

function validateStylesheet(source) {
  if (typeof source !== "string" || source.length === 0) return [];
  const errors = [];
  // Declaration property names: `name` followed by `:`. Covers custom
  // properties (`--x`) and vendor prefixes (`-webkit-*`) so they are
  // reported as unsupported rather than silently ignored.
  const re = /(-{1,2}[A-Za-z][A-Za-z0-9-]*|--[A-Za-z0-9-_]+|[A-Za-z][A-Za-z0-9-]*)\s*:/g;
  let match;
  while ((match = re.exec(source)) !== null) {
    const property = match[1];
    if (!SUPPORTED.has(property)) {
      const { line, column } = toLineColumn(source, match.index);
      errors.push({ property, line, column });
    }
  }
  return errors;
}

module.exports = { isSupported, validateStylesheet };
