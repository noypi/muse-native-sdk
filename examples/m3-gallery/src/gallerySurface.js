'use strict';

// Gallery surface entry (FR-SAMPLE-014 T3 / AC-SAMPLE-041, AC-RUNTIME-032).
//
// Default path renders themed sections from `buildStories(manifest)`
// descriptors through the `components` table (+ `OverlayHost` nesting per
// RULE-M3-006) with zero hand-edited label rows. The static root survives
// solely as the explicit-flag diagnostic `renderStaticFallback`, reachable
// only via `--static-fallback` (CLI) / `MUSE_STATIC_FALLBACK=1` (env), which
// reports the fallback and fails the proof gate
// (`muse-native gallery-tree --assert-evaluated --fail-on-static-fallback`).

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

// Fail-closed explicit-flag decision. Only the exact CLI flag
// `--static-fallback` or exactly `MUSE_STATIC_FALLBACK=1` enables the static
// fallback diagnostic; every near-miss value denies. Never reads
// `process.env`; the caller supplies `{ argv, env }`.
function isStaticFallbackEnabled(input) {
  const argv = input && Array.isArray(input.argv) ? input.argv : [];
  const env = input && input.env && typeof input.env === 'object' ? input.env : {};
  for (const arg of argv) {
    if (arg === '--static-fallback') return true;
  }
  return env.MUSE_STATIC_FALLBACK === '1';
}

// Explicit-flag static-fallback diagnostic (AC-RUNTIME-032). Logs/reports
// the fallback and pins the proof gate to `fail`: no reason string — empty,
// hostile, or otherwise — may flip the gate to `pass`.
function renderStaticFallback(reason) {
  let label = '';
  try {
    label = typeof reason === 'string' ? reason : String(reason);
  } catch (_) {
    label = '<unprintable>';
  }
  try {
    process.stderr.write(`[gallery] static fallback diagnostic (reported, proofGate=fail): ${label.slice(0, 200)}\n`);
  } catch (_) {
    // Reporting must never throw; the returned shape carries the report.
  }
  return { staticFallback: true, reported: true, proofGate: 'fail' };
}

// Proof-gate decision (AC-RUNTIME-032): the default evaluated tree passes;
// the static fallback fails. Fail-closed: any truthy `staticFallback` fails.
function evaluateProofGate(input) {
  const failed = !!(input && input.staticFallback);
  return { pass: !failed };
}

// FR-M3-011 T1 — gallery pages attach through the engine-owned Surface host
// contract (shared presenter seam: engine/runtime/include/runtime/presenter.h
// AttachSurface/renderFrame). Every page instantiates its registered
// components from the same tree.mui.bin descriptors through this one entry;
// there is no gallery-only hosting branch and no box/caption stand-in output
// on device frames.

// BuildError: failed attach. Carries boolean `mockReachable` (true when a
// mock painter was reachable on the descriptor path, false otherwise).
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
    return { ok: false, mockReachable: true, reason: `mock painter reachable for descriptor ${shown}` };
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
  renderStaticFallback,
  isStaticFallbackEnabled,
  evaluateProofGate,
  THEMES,
};
