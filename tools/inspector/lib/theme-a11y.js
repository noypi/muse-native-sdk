'use strict';
// Theme editor preview/diff + a11y semantic-tree mirror (FR-DX-003 / AC-DX-013 / TC-DX-013).
// Pure functions: previewTokens deep-merges a patch over a clone of the base
// tokens and exports a committable { path, from, to } diff; mirrorSemantics
// returns a deep-frozen read-only copy of the semantic tree.

const HOSTILE_KEYS = new Set(['__proto__', 'prototype', 'constructor']);

function badPatchError(detail) {
  const err = new Error(`malformed token patch in inspector view (DX-003 / AC-DX-013): ${detail}`);
  err.code = 'E_INSPECTOR_BAD_PATCH';
  err.cause = 'malformed token patch in inspector view (DX-003 / AC-DX-013)';
  err.action = 'Preview a plain-object token patch with safe keys (DX-003).';
  return err;
}

function badTreeError(detail) {
  const err = new Error(`malformed semantics tree in inspector view (DX-003 / AC-DX-013): ${detail}`);
  err.code = 'E_INSPECTOR_BAD_TREE';
  err.cause = 'malformed semantics tree in inspector view (DX-003 / AC-DX-013)';
  err.action = 'Mirror a semantic tree with a string role on every node (DX-003).';
  return err;
}

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function assertSafeKeys(obj) {
  for (const key of Object.keys(obj)) {
    if (HOSTILE_KEYS.has(key)) throw badPatchError(`rejected unsafe key "${key}"`);
  }
}

function cloneTokens(value) {
  if (Array.isArray(value)) return value.map(cloneTokens);
  if (isPlainObject(value)) {
    assertSafeKeys(value);
    const out = {};
    for (const key of Object.keys(value)) out[key] = cloneTokens(value[key]);
    return out;
  }
  return value;
}

function deepEqual(a, b) {
  if (Object.is(a, b)) return true;
  if (Array.isArray(a) && Array.isArray(b)) {
    if (a.length !== b.length) return false;
    return a.every((v, i) => deepEqual(v, b[i]));
  }
  if (isPlainObject(a) && isPlainObject(b)) {
    const ka = Object.keys(a);
    const kb = Object.keys(b);
    if (ka.length !== kb.length) return false;
    return ka.every((k) => Object.prototype.hasOwnProperty.call(b, k) && deepEqual(a[k], b[k]));
  }
  return false;
}

function previewTokens(baseTokens, patch) {
  if (!isPlainObject(baseTokens)) throw badPatchError('base tokens must be an object');
  if (!isPlainObject(patch)) throw badPatchError('patch must be an object');
  assertSafeKeys(patch);
  const patched = cloneTokens(baseTokens);
  const diff = [];
  applyPatch(patched, patch, '', diff);
  return { patched, diff };
}

function applyPatch(target, patch, prefix, diff) {
  assertSafeKeys(patch);
  for (const key of Object.keys(patch)) {
    const path = prefix ? `${prefix}.${key}` : key;
    const next = patch[key];
    if (isPlainObject(next)) {
      assertSafeKeys(next);
      const current = target[key];
      if (isPlainObject(current)) {
        applyPatch(current, next, path, diff);
      } else if (current === undefined) {
        const fresh = {};
        applyPatch(fresh, next, path, diff);
        target[key] = fresh;
      } else {
        const from = cloneTokens(current);
        const to = {};
        applyPatch(to, next, path, diff);
        // If the object-form patch replaced a primitive, record each leaf via
        // recursion above against an empty base so paths stay leaf-granular.
        void from;
        target[key] = to;
      }
    } else if (Array.isArray(next)) {
      const from = Object.prototype.hasOwnProperty.call(target, key)
        ? cloneTokens(target[key])
        : undefined;
      const to = cloneTokens(next);
      if (!deepEqual(from, to)) diff.push({ path, from, to });
      target[key] = to;
    } else {
      const hasCurrent = Object.prototype.hasOwnProperty.call(target, key);
      const from = hasCurrent ? target[key] : undefined;
      if (!hasCurrent || !Object.is(from, next)) {
        diff.push({ path, from, to: next });
      }
      target[key] = next;
    }
  }
}

function assertValidNode(node) {
  if (!isPlainObject(node)) throw badTreeError('node must be an object');
  if (typeof node.role !== 'string' || node.role.length === 0) {
    throw badTreeError('node role must be a non-empty string');
  }
  if (node.label !== undefined && typeof node.label !== 'string') {
    throw badTreeError('node label must be a string');
  }
  if (node.children !== undefined) {
    if (!Array.isArray(node.children)) throw badTreeError('node children must be an array');
    for (const child of node.children) assertValidNode(child);
  }
}

function cloneTree(node) {
  const out = {};
  for (const key of Object.keys(node)) {
    if (HOSTILE_KEYS.has(key)) throw badTreeError(`rejected unsafe key "${key}"`);
    const value = node[key];
    if (key === 'children' && Array.isArray(value)) {
      out[key] = value.map(cloneTree);
    } else if (Array.isArray(value)) {
      out[key] = value.map((v) => (isPlainObject(v) ? cloneTree(v) : v));
    } else if (isPlainObject(value)) {
      out[key] = cloneTree(value);
    } else {
      out[key] = value;
    }
  }
  return out;
}

function deepFreeze(value) {
  if (Array.isArray(value)) {
    for (const v of value) deepFreeze(v);
    Object.freeze(value);
    return value;
  }
  if (isPlainObject(value)) {
    for (const key of Object.keys(value)) deepFreeze(value[key]);
    Object.freeze(value);
    return value;
  }
  return value;
}

function mirrorSemantics(tree) {
  assertValidNode(tree);
  const copy = cloneTree(tree);
  deepFreeze(copy);
  return { tree: copy };
}

module.exports = { previewTokens, mirrorSemantics };
