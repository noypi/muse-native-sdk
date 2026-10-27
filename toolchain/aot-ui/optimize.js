'use strict';
// T8 — constant folding + tree shaking with removed-byte report
// (NFR-BUILD-013 / AC-BUILD-044). Hermetic: stdlib only. Pure function over
// T5's tree shape; never mutates its input, never touches fs/network/env.

function bytesOf(value) {
  return Buffer.byteLength(JSON.stringify(value), 'utf8');
}

function isPlainObject(value) {
  if (value === null || typeof value !== 'object') return false;
  const proto = Object.getPrototypeOf(value);
  return proto === Object.prototype || proto === null;
}

function isLiteralValue(value) {
  if (value === null) return true;
  const t = typeof value;
  if (t === 'number') return Number.isFinite(value);
  if (t === 'boolean') return true;
  if (t !== 'string') return false;
  const s = value.trim();
  if (s === '') return false;
  try {
    JSON.parse(s);
    return true;
  } catch {
    return false;
  }
}

// Optimize one node into 0..n replacement nodes (folding / removal).
function optimizeNodeInto(node, out) {
  if (!isPlainObject(node) && !Array.isArray(node)) {
    out.push(node);
    return;
  }
  if (Array.isArray(node)) {
    out.push(optimizeList(node));
    return;
  }
  const kind = node.kind;

  if (kind === 'if') {
    if (node.cond === false) return; // unreachable: drop subtree
    if (node.cond === true) {
      const kids = Array.isArray(node.children) ? optimizeList(node.children) : [];
      for (const k of kids) out.push(k);
      return;
    }
    // Non-constant condition: keep node, optimize children.
    const kept = { ...node };
    if (Array.isArray(node.children)) kept.children = optimizeList(node.children);
    out.push(kept);
    return;
  }

  if (kind === 'element' && node.used === false) return; // unused: drop subtree

  if (kind === 'expr' && 'value' in node && isLiteralValue(node.value)) {
    out.push({ kind: 'text', value: node.value });
    return;
  }

  const kept = { ...node };
  if (Array.isArray(node.children)) kept.children = optimizeList(node.children);
  out.push(kept);
}

function optimizeList(list) {
  const out = [];
  for (const node of list) optimizeNodeInto(node, out);
  return out;
}

function optimize(tree) {
  if (tree === null || tree === undefined) return { tree, removedBytes: 0 };
  const t = typeof tree;
  if (t !== 'object') return { tree, removedBytes: 0 };

  const before = bytesOf(tree);
  let result;
  if (Array.isArray(tree)) {
    result = optimizeList(tree);
  } else if (isPlainObject(tree)) {
    if (typeof tree.kind === 'string') {
      const out = [];
      optimizeNodeInto(tree, out);
      if (out.length === 0) result = null;
      else if (out.length === 1) result = out[0];
      else result = out;
    } else if (Array.isArray(tree.root)) {
      result = { ...tree, root: optimizeList(tree.root) };
    } else {
      result = { ...tree };
    }
  } else {
    return { tree, removedBytes: 0 };
  }
  const after = bytesOf(result);
  return { tree: result, removedBytes: Math.max(0, before - after) };
}

module.exports = { optimize };
