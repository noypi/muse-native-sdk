'use strict';
// @muse/cli — source loading + structural lowering to the syntax-neutral UI IR.
// T9 reuses this module (loader + lowering + binary encoding); build.js only
// orchestrates validation and artefact writes. Lowering here is structural
// only — compiler legality is enforced by later lanes.
const fs = require('node:fs');
const path = require('node:path');

const IR_VERSION = 2;

// Parse a static-only `routeTags` attribute value (AC-MUSE-055) into a
// `string[]`: comma/whitespace-separated, trimmed, empties dropped,
// de-duplicated in first-seen order. Absent or empty ≡ `[]` (unregistered →
// `ContentRegistry.Resolve` returns `nullptr` → repaint-only denial per
// RULE-RUNTIME-001). Static-only: only the quoted `routeTags="a,b"` form is
// read here; a dynamic `routeTags={...}` expression is never admitted as a
// slot (skipped in the dynamic-prop scan below).
function parseRouteTags(attrs) {
  const m = /\brouteTags\s*=\s*"([^"]*)"/.exec(attrs || '');
  if (!m) return [];
  const seen = new Set();
  for (const part of m[1].split(/[,\s]+/)) {
    const tag = part.trim();
    if (tag && !seen.has(tag)) seen.add(tag);
  }
  return [...seen];
}

// Collect the passthrough union of every descendant node's `routeTags`
// (first-seen order) for the owning `roots[]` Module entry (AC-MUSE-055).
function collectSubtreeTags(children) {
  const seen = new Set();
  const visit = (nodes) => {
    for (const node of nodes || []) {
      if (!node || typeof node !== 'object') continue;
      for (const tag of node.routeTags || []) {
        if (!seen.has(tag)) seen.add(tag);
      }
      if (Array.isArray(node.children)) visit(node.children);
    }
  };
  visit(children);
  return [...seen];
}

function collectTsxFiles(dir, out) {
  const entries = fs.readdirSync(dir, { withFileTypes: true });
  for (const entry of entries) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      // Skip build output and dependency trees so rebuilds stay stable.
      if (entry.name === 'dist' || entry.name === 'node_modules') continue;
      collectTsxFiles(full, out);
    } else if (entry.isFile() && entry.name.endsWith('.tsx')) {
      out.push(full);
    }
  }
  return out;
}

// Load every `.tsx` source under appDir. Returns [{ file, text }] with
// `file` relative to appDir (posix separators). Throws on I/O errors.
function loadSources(appDir) {
  const files = collectTsxFiles(appDir, []);
  files.sort();
  return files.map((full) => ({
    file: path.relative(appDir, full).split(path.sep).join('/'),
    text: fs.readFileSync(full, 'utf8'),
  }));
}

// Lower one source file to a static-schema node with dynamic value slots:
// static JSX structure becomes { type, props, children }; dynamic `{...}`
// expressions become entries in `slots`.
function lowerSource(source) {
  const { file, text } = source;
  const children = [];
  const tagRe = /<([A-Za-z][A-Za-z0-9.:-]*)([^<>]*)\/?>/g;
  let match;
  while ((match = tagRe.exec(text)) !== null) {
    const type = match[1];
    // Skip closing tags, fragments, and lowercase html-ish noise is kept
    // as-is: structural lowering accepts every well-formed tag.
    if (type.startsWith('/')) continue;
    const attrs = match[2] || '';
    const props = {};
    const slots = [];
    const routeTags = parseRouteTags(attrs);
    const staticPropRe = /([A-Za-z_][\w:.-]*)\s*=\s*"([^"]*)"/g;
    let propMatch;
    while ((propMatch = staticPropRe.exec(attrs || '')) !== null) {
      props[propMatch[1]] = propMatch[2];
    }
    const dynPropRe = /([A-Za-z_][\w:.-]*)\s*=\s*\{([^}]+)\}/g;
    while ((propMatch = dynPropRe.exec(attrs || '')) !== null) {
      // Static-only tag carriage (AC-MUSE-055): a dynamic
      // `routeTags={...}` expression is never admitted as a slot.
      if (propMatch[1] === 'routeTags') continue;
      slots.push({ kind: 'prop', name: propMatch[1], expression: propMatch[2].trim() });
    }
    // The raw `routeTags="..."` string stays out of `props`: the parsed
    // `routeTags: string[]` array beside it is the single carrier, so one
    // node never carries two shapes under one name.
    if (Object.prototype.hasOwnProperty.call(props, 'routeTags')) {
      delete props.routeTags;
    }
    children.push({ type, props, slots, routeTags, children: [] });
  }
  // Dynamic value slots from JSX expression children: `{expr}`.
  const exprRe = /\{([^}]+)\}/g;
  const valueSlots = [];
  while ((match = exprRe.exec(text)) !== null) {
    const expression = match[1].trim();
    if (expression) valueSlots.push({ kind: 'value', expression });
  }
  // Static text runs between tags.
  const textNodes = [];
  const stripped = text.replace(/<[^<>]*>/g, '\n');
  for (const line of stripped.split('\n')) {
    const value = line.trim();
    if (!value || value.startsWith('import ') || value.startsWith('export ')) continue;
    if (/^(const|let|var|return|function)\b/.test(value)) continue;
    if (/^[\w$.]+\s*\(.*\)\s*;?$/.test(value)) continue;
    textNodes.push({ type: '#text', value, routeTags: [] });
  }
  const roots = children.concat(textNodes);
  return {
    type: 'Module',
    file,
    props: {},
    slots: valueSlots,
    // Root passthrough copy (AC-MUSE-055): the union of every descendant
    // node's `routeTags` on the owning `roots[]` entry; absent ≡ `[]`.
    routeTags: collectSubtreeTags(roots),
    children: roots,
  };
}

// Lower loaded sources to the syntax-neutral IR:
// `{ version: 2, roots: [...] }`. Every well-formed input lowers
// structurally; legality checks belong to later lanes.
function lowerToIR(sources) {
  return {
    version: IR_VERSION,
    roots: sources.map(lowerSource),
  };
}

// Length-prefixed binary encoding of the same tree:
// offset 0: 4 bytes magic `MUI1` (ascii)
// offset 4: u32LE IR version (2)
// offset 8: u32LE payload length N (bytes)
// offset 12: N bytes utf8 JSON of the IR object
function encodeTree(ir) {
  const payload = Buffer.from(JSON.stringify(ir), 'utf8');
  const out = Buffer.alloc(12 + payload.length);
  out.write('MUI1', 0, 'ascii');
  out.writeUInt32LE(ir.version, 4);
  out.writeUInt32LE(payload.length, 8);
  payload.copy(out, 12);
  return out;
}

module.exports = { IR_VERSION, loadSources, lowerToIR, lowerSource, encodeTree };
