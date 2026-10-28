'use strict';

// Typed JS surface check (FR-PKG-003, AC-PKG-007).
// Static `src/index.d.ts` text check: every manifest-listed export must be
// declared with matching arity/arg kinds. Full `tsc --noEmit` proof is
// tester-layer work; this gate fails closed on the declaration text alone.

const { PkgError } = require('./errors.js');

// Manifest arg/return kinds to the TypeScript types the entry d.ts uses.
// Opaque native handles cross the boundary as numbers (e.g. WorldId).
const KIND_TS = {
  handle: 'number',
  number: 'number',
  string: 'string',
  boolean: 'boolean',
  void: 'void',
};

function typesPath(manifest) {
  const entry = manifest && manifest.entry;
  if (entry && typeof entry.types === 'string' && entry.types.length > 0) {
    return entry.types;
  }
  return 'src/index.d.ts';
}

function symbolPath(manifest, name) {
  return `${typesPath(manifest)}#${name}`;
}

function normalizeType(text) {
  return String(text).replace(/\s+/g, '').toLowerCase();
}

function expectedTs(kind) {
  if (typeof kind !== 'string') return '';
  const mapped = KIND_TS[kind];
  return normalizeType(mapped !== undefined ? mapped : kind);
}

// Split a parameter list on top-level commas so generic/function types
// containing commas (e.g. `Record<string, number>`) stay intact.
function splitParams(text) {
  const parts = [];
  let depth = 0;
  let current = '';
  for (const ch of text) {
    if (ch === '(' || ch === '<' || ch === '{' || ch === '[') depth += 1;
    if (ch === ')' || ch === '>' || ch === '}' || ch === ']') depth -= 1;
    if (ch === ',' && depth === 0) {
      parts.push(current);
      current = '';
    } else {
      current += ch;
    }
  }
  if (current.trim() !== '') parts.push(current);
  return parts;
}

// A parameter declares its type after the first top-level `:`;
// a bare type with no name (e.g. `(number)`) is the type itself.
function paramTypeOf(param) {
  let depth = 0;
  for (let i = 0; i < param.length; i += 1) {
    const ch = param[i];
    if (ch === '(' || ch === '<' || ch === '{' || ch === '[') depth += 1;
    if (ch === ')' || ch === '>' || ch === '}' || ch === ']') depth -= 1;
    if (ch === ':' && depth === 0) {
      return param.slice(i + 1).split('=')[0].trim();
    }
  }
  return param.split('=')[0].trim();
}

// Parse `export declare function name(params): ret;` declarations into a
// Map of name -> { arity, argTypes, returnType }. Balanced-paren scan so
// function-typed parameters do not cut the match short.
function parseDeclarations(dts) {
  const decls = new Map();
  const header = /export\s+declare\s+function\s+([A-Za-z_$][\w$]*)\s*\(/g;
  let match;
  while ((match = header.exec(dts)) !== null) {
    const name = match[1];
    if (decls.has(name)) continue;
    let depth = 1;
    let i = header.lastIndex;
    while (i < dts.length && depth > 0) {
      if (dts[i] === '(') depth += 1;
      else if (dts[i] === ')') depth -= 1;
      i += 1;
    }
    const paramsText = dts.slice(header.lastIndex, i - 1);
    const rest = dts.slice(i).match(/^\s*:\s*([^;{]+);/);
    if (!rest) continue;
    const params = splitParams(paramsText).filter((p) => p.trim() !== '');
    decls.set(name, {
      arity: params.length,
      argTypes: params.map(paramTypeOf),
      returnType: rest[1].trim(),
    });
    header.lastIndex = i + rest[0].length;
  }
  return decls;
}

function checkEntryTypes(manifest, dts) {
  const list = manifest && Array.isArray(manifest.exports) ? manifest.exports : [];
  if (list.length === 0) return;
  const text = typeof dts === 'string' ? dts : '';
  if (/<script[\s>]/i.test(text)) {
    throw new PkgError(
      'E_TYPES',
      typesPath(manifest),
      'entry d.ts must not contain executable markup',
    );
  }
  const decls = parseDeclarations(text);
  for (const entry of list) {
    const name = entry && entry.name;
    const path = symbolPath(manifest, name);
    const decl = decls.get(name);
    if (!decl) {
      throw new PkgError(
        'E_TYPES',
        path,
        `entry d.ts must declare function "${name}"`,
      );
    }
    if (decl.arity !== entry.arity) {
      throw new PkgError(
        'E_TYPES',
        path,
        `d.ts declaration of "${name}" must have arity ${entry.arity}`,
      );
    }
    const expectedKinds = Array.isArray(entry.argKinds) ? entry.argKinds : [];
    const kindsMatch =
      decl.argTypes.length === expectedKinds.length &&
      expectedKinds.every((kind, i) => normalizeType(decl.argTypes[i]) === expectedTs(kind));
    if (!kindsMatch) {
      throw new PkgError(
        'E_TYPES',
        path,
        `d.ts declaration of "${name}" must have arg kinds [${expectedKinds.join(', ')}]`,
      );
    }
    if (
      typeof entry.returnKind === 'string' &&
      normalizeType(decl.returnType) !== expectedTs(entry.returnKind)
    ) {
      throw new PkgError(
        'E_TYPES',
        path,
        `d.ts declaration of "${name}" must return "${entry.returnKind}"`,
      );
    }
  }
}

module.exports = { checkEntryTypes };
