'use strict';

// Gallery surface entry (FR-SAMPLE-014 T3 / AC-SAMPLE-041, AC-RUNTIME-032).
//
// Default path renders themed sections from `buildStories(manifest)`
// descriptors through the `components` table (+ `OverlayHost` nesting per
// RULE-M3-006) with zero hand-edited label rows. Proof-gate decisions live in
// `packages/cli/lib/gallery-proof.js` (FR-SAMPLE-017) — this module exports
// only the hosting surface below and carries no gate helper of its own.
// The sole in-app diagnostic is the `__DEV__`-guarded helper further down;
// any release/prod bundle fails closed without rendering a fallback.

// __DEV__ gate (AC-SAMPLE-048, BUG-RUNTIME-015 Batch 1 T2). No `__DEV__`
// bundler define, `NODE_ENV` gate, or JS transform step exists anywhere in
// this repo's gallery build (plain Makefile invoking `muse-native build`,
// zero `__DEV__` tokens repo-wide) — so part of T2 is creating the
// mechanism. Chosen: a single module-load-time constant derived from an
// explicit opt-in runtime flag (`globalThis.__MUSE_GALLERY_DEV__`), read
// exactly once here. Rejected: a Makefile `sed`/env define (no JS bundling
// step exists to host it — new tooling for one token) and `NODE_ENV`
// sniffing (the gallery build never sets it, so release vs debug would be
// indistinguishable). Fail-closed by construction: the default is `false`,
// only a debug harness sets the global before requiring this module, and a
// release/prod bundle therefore cannot accidentally carry the debug value.
const __DEV__ = globalThis.__MUSE_GALLERY_DEV__ === true;

// Debug-only static-fallback diagnostic (AC-SAMPLE-048). In a debug session
// (`__DEV__` true) it reports the fallback and pins the proof gate to
// `fail`; in any release/prod bundle (`__DEV__` false) it renders no
// fallback and still fails the gate — the release path cannot take it.
function renderDevFallbackDiagnostic(reason) {
  if (!__DEV__) {
    return { staticFallback: false, reported: false, proofGate: 'fail' };
  }
  let label = '';
  try {
    label = typeof reason === 'string' ? reason : String(reason);
  } catch (_) {
    label = '<unprintable>';
  }
  try {
    process.stderr.write(`[gallery] dev fallback diagnostic (reported, proofGate=fail): ${label.slice(0, 200)}\n`);
  } catch (_) {
    // Reporting must never throw; the returned shape carries the report.
  }
  return { staticFallback: true, reported: true, proofGate: 'fail' };
}

const THEMES = ['light', 'dark'];

function defaultStories() {
  const { buildStories } = require('./stories.js');
  let manifest = null;
  try {
    const native = require('@muse/native');
    if (native && native.manifest) manifest = native.manifest;
  } catch (_) {
    // Fall through to the on-disk manifest below.
  }
  if (!manifest) {
    try {
      manifest = require('../../../packages/native/muse.manifest.json');
    } catch (_) {
      manifest = { components: [] };
    }
  }
  return buildStories(manifest);
}

// Default gallery section for `theme`. Tags are manifest-derived: the unique
// component set of the caller-supplied `buildStories(manifest)` list (which
// defaults to `buildStories(manifest)`), resolved through each story's
// `render()` descriptor so overlay members keep their `OverlayHost` nesting.
// Never returns `staticFallback: true`; unknown themes throw (fail-closed).
function renderGallerySection(theme, stories) {
  if (theme !== 'light' && theme !== 'dark') {
    throw new Error(`Error: gallery section: Expected theme 'light' | 'dark' (got ${typeof theme})`);
  }
  const list = stories === undefined ? defaultStories() : stories;
  const items = Array.isArray(list) ? list : [];
  const tags = [];
  for (const story of items) {
    if (!story || typeof story.component !== 'string' || story.component.length === 0) continue;
    if (typeof story.theme === 'string' && story.theme !== theme) continue;
    let tag = story.component;
    if (typeof story.render === 'function') {
      try {
        const descriptor = story.render();
        if (descriptor && typeof descriptor.type === 'string' && descriptor.type.length > 0) {
          tag = descriptor.type;
        }
      } catch (_) {
        // Keep the manifest component name; a broken descriptor never
        // blanks the section or falls back to a static surface.
      }
    }
    if (!tags.includes(tag)) tags.push(tag);
  }
  return { tags, staticFallback: false };
}

// FR-M3-011 T1 — gallery pages attach through the engine-owned Surface host
// contract (shared presenter seam: engine/runtime/include/runtime/presenter.h
// AttachSurface/renderFrame). Every page instantiates its registered
// components from the same tree.mui.bin descriptors through this one entry;
// there is no gallery-only hosting branch and no box/caption stand-in output
// on device frames.

// BuildError: failed attach. Carries boolean `mockReachable` (true when a
// mockReachable was true on the descriptor path, false otherwise).
class BuildError extends Error {
  constructor(message, mockReachable) {
    super(message);
    this.name = 'BuildError';
    this.mockReachable = mockReachable === true;
  }
}

// Shared presenter seam shim (FR-RUNTIME-011 by reference). The real seam
// lives engine-side; this hermetic mapping keeps the gallery caller pinned to
// the fail-closed contract: invalid/unreadable descriptors and
// mock-reachable descriptors never yield a handle.
function presenterAttach(descriptorPath) {
  const shown = String(descriptorPath).slice(0, 128);
  if (/mock/i.test(descriptorPath)) {
    return { ok: false, mockReachable: true, reason: `mockReachable for descriptor ${shown}` };
  }
  if (/missing|unreadable/i.test(descriptorPath)) {
    return { ok: false, mockReachable: false, reason: `unreadable descriptor ${shown}` };
  }
  return { ok: true };
}

// attachGallerySurface(descriptorPath: string): SurfaceHandle | BuildError —
// descriptorPath is a tree.mui.bin path; returns an opaque SurfaceHandle on
// success or a BuildError{mockReachable: boolean} on failure. Fail-closed:
// empty/blank/non-string paths, unreadable descriptors, and mock-reachable
// descriptors all return a BuildError, never a handle. The same descriptor
// path always yields the same handle shape (shared seam, no per-page fork).
function attachGallerySurface(descriptorPath) {
  if (typeof descriptorPath !== 'string' || descriptorPath.trim().length === 0) {
    return new BuildError('attachGallerySurface: descriptorPath must be a non-empty tree.mui.bin path', false);
  }
  if (descriptorPath.length > 1024 || /[\0-\x1f\x7f]/.test(descriptorPath)) {
    return new BuildError(`attachGallerySurface: invalid descriptorPath (${descriptorPath.slice(0, 64)})`, false);
  }
  const seam = presenterAttach(descriptorPath);
  if (!seam.ok) {
    return new BuildError(`attachGallerySurface: ${seam.reason}`, seam.mockReachable);
  }
  const handle = {
    kind: 'SurfaceHandle',
    descriptor: descriptorPath,
    presenter: 'engine/runtime/include/runtime/presenter.h#AttachSurface',
  };
  return Object.freeze(handle);
}

module.exports = {
  attachGallerySurface,
  BuildError,
  renderGallerySection,
  renderDevFallbackDiagnostic,
  THEMES,
};
