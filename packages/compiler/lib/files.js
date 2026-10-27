"use strict";

const path = require("node:path");

function validateFileConvention(filePath, sourceText) {
  const basename = path.basename(filePath);
  const stem = path.basename(filePath, ".tsx");
  if (!filePath.endsWith(".tsx") || !/^[A-Z][A-Za-z0-9]*$/.test(stem)) {
    return `${filePath}: 1 component per .tsx in a PascalCase file, got ${basename}`;
  }
  if (!/export\s+default/.test(sourceText)) {
    return `${filePath}: 1 component per .tsx in a PascalCase file, got no default export`;
  }
  return null;
}

function validateSingleComponent(sourceText, filePath) {
  const text = String(sourceText ?? "");
  const found = [];
  const declRe =
    /^(?:export\s+(?:default\s+)?)?function\s+([A-Za-z_$][\w$]*)|^(?:export\s+)?const\s+([A-Za-z_$][\w$]*)\s*(?::[^\n=]+?)?\s*=/gm;
  let m;
  while ((m = declRe.exec(text)) !== null) {
    const name = m[1] ?? m[2];
    if (name && /^[A-Z]/.test(name)) found.push({ index: m.index, name });
  }
  const defRe =
    /export\s+default(?:\s+function\s*([A-Za-z_$][\w$]*)?)?(?:\s+([A-Za-z_$][\w$]*))?/g;
  while ((m = defRe.exec(text)) !== null) {
    if (found.some((e) => e.index === m.index)) continue;
    const name = m[1] || m[2] || "default";
    if (/^[A-Z]/.test(name)) {
      if (found.some((e) => e.name === name)) continue;
      found.push({ index: m.index, name });
    } else if (name === "default") {
      if (found.some((e) => e.name === "default")) continue;
      found.push({ index: m.index, name });
    }
  }
  found.sort((a, b) => a.index - b.index);
  if (found.length <= 1) return null;
  const second = found[1].name;
  return `${filePath}: 1 component per .tsx, move ${second} to ${second}.tsx`;
}

module.exports = { validateFileConvention, validateSingleComponent };
