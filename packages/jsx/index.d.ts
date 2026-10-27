// @muse/jsx — types only (AC-TSX-021).
// No runtime entry: importing this package at runtime fails closed.
// Q-TSX-002 (c-show hidden-state semantics) is out of scope here;
// only the `when` prop-type declaration is provided.
declare namespace JSX {
  interface IntrinsicElements {
    'c-if': { test: boolean; children?: unknown };
    'c-else-if': { test: boolean; children?: unknown };
    'c-else': { children?: unknown };
    'c-for': { each: unknown[]; key: string; children?: unknown };
    'c-show': { when: boolean; children?: unknown };
  }
}
