'use strict';
// FR-M3-009 automated gallery device proof (AC-M3-020/TC-M3-021, AC-M3-021/TC-M3-022).
// Batch 3 (T4). Hermetic pure gates over the proof plan, viewport pixels, and
// golden diffs. No network, no secrets, no live services, no adb invocation:
// proofCommand only renders the command string the tester runs on device.
//
// Device serial (ASSUMPTION A1): every device step uses 127.0.0.1:6569.
// Tolerance default (ASSUMPTION A3): 0.02 until goldens/m3-tolerances.json
// pins per-component values; the relocated file at
// examples/m3-gallery/goldens/m3-tolerances.json is authoritative per D3.
// The committed build/goldens/m3-tolerances.json is superseded (untouched).

const { join } = require('node:path');

const DEVICE_SERIAL = '127.0.0.1:6569';

const DEFAULT_TOLERANCE = 0.02;

// D3 relocation: authoritative goldens live outside build/.
const GOLDENS_DIR = join(__dirname, '..', 'goldens');

const TOLERANCES_PATH = join(GOLDENS_DIR, 'm3-tolerances.json');

const { buildStories } = require('./stories.js');

// STATES is SUPERSEDED (BUG-SAMPLE-003 T5): hover/focus/pressed/disabled are
// driveState concerns exercised via driveState, never gallery proof pages.
// Only TextFieldEditCore wires a boolean state prop (`enabled`); Surface and
// Ripple declare none. Kept exported so existing importers keep loading, but
// buildProofPlan no longer consumes it — the plan is re-emitted from
// buildStories(manifest), the single source of truth (SPEC-SAMPLE-PROJECT
// v2.37 AC-SAMPLE-041; AC-M3-015/016), so the two can never diverge again.
const STATES = ['hover', 'focus', 'pressed', 'disabled'];

// Interaction states owned by driveState — never gallery proof pages. Named
// in count-mismatch diagnostics because the dominant historical failure mode
// is a stale plan carrying these states.
const NON_PROOF_STATES = ['hover', 'focus', 'pressed', 'disabled'];

function proofCommand(page) {
  const name = typeof page === 'string' && page.length > 0 ? page : 'gallery';
  return `adb -s ${DEVICE_SERIAL} exec-out screencap -p > ${name}.png`;
}

// Re-baselined emission (BUG-SAMPLE-003 T5, probed 2026-10-31 against the
// cleaned tree via `node -e` requiring src/stories.js +
// packages/native/muse.manifest.json): 232 unique
// component|variant|state|theme pages; Surface 2 (states ['default']),
// Ripple 2 (states ['default']), TextFieldEditCore 4 (states
// ['default','enabled']). Re-probe — never hand-edit — if manifest props
// change. Zero hover/focus/pressed/disabled pages by construction.
function buildProofPlan(manifest) {
  return buildStories(manifest).map((story) => ({
    component: story.component,
    tier: story.tier,
    variant: story.variant,
    state: story.state,
    theme: story.theme,
    page: `tier${story.tier}/${story.component}/${story.variant}/${story.state}/${story.theme}`,
  }));
}

function proofKey(entry) {
  return `${entry.component}|${entry.variant}|${entry.state}|${entry.theme}`;
}

function assertCounts(plan, manifest) {
  const expected = buildProofPlan(manifest);
  const list = Array.isArray(plan) ? plan : [];
  const seen = new Set(list.map(proofKey));
  const wanted = new Set(expected.map(proofKey));
  for (const entry of expected) {
    if (!seen.has(proofKey(entry))) {
      throw new Error(
        `gallery-proof count mismatch: missing page ${entry.page} for component ${entry.component} ` +
          `(state "${entry.state}"; driveState-only states ${NON_PROOF_STATES.join('/')} are never gallery proof pages)`,
      );
    }
  }
  for (const entry of list) {
    if (!wanted.has(proofKey(entry))) {
      throw new Error(
        `gallery-proof count mismatch: extra page ${entry.page || proofKey(entry)} for component ${entry.component} ` +
          `(state "${entry.state}"; driveState-only states ${NON_PROOF_STATES.join('/')} are never gallery proof pages)`,
      );
    }
  }
  if (list.length !== expected.length) {
    const first = expected[0];
    throw new Error(
      `gallery-proof count mismatch: expected ${expected.length} pages, saw ${list.length} (page ${first.page} component ${first.component})`,
    );
  }
}

function isBlankViewport(pixels, width, height) {
  if (!Array.isArray(pixels) && !(pixels && typeof pixels.length === 'number')) {
    return true;
  }
  const length = pixels.length;
  if (length === 0) {
    return true;
  }
  if (width === 0 || height === 0) {
    return true;
  }
  const first = pixels[0];
  for (let i = 1; i < length; i += 1) {
    if (pixels[i] !== first) {
      return false;
    }
  }
  return true;
}

function isDiagnosticOnly(output) {
  const text = typeof output === 'string' ? output : String(output);
  if (/FATAL EXCEPTION/i.test(text)) {
    return true;
  }
  if (/^\s*(FATAL|E\/|W\/|I\/|D\/|V\/)/.test(text)) {
    return true;
  }
  if (/ANR in|crash|tombstone/i.test(text) && !/content/i.test(text)) {
    return true;
  }
  return false;
}

function toleranceFor(component, tolerances) {
  if (tolerances && typeof tolerances[component] === 'number') {
    return tolerances[component];
  }
  if (tolerances && typeof tolerances.default === 'number') {
    return tolerances.default;
  }
  return DEFAULT_TOLERANCE;
}

function diffAgainstGolden(actual, golden, tolerance, meta) {  const page = meta && meta.page ? meta.page : 'unknown-page';
  const component = meta && meta.component ? meta.component : undefined;
  if (golden === null || golden === undefined) {
    return { pass: false, page, component };
  }
  const limit = typeof tolerance === 'number' ? tolerance : DEFAULT_TOLERANCE;
  const a = Array.isArray(actual) ? actual : Array.from(actual || []);
  const g = Array.isArray(golden) ? golden : Array.from(golden || []);
  if (a.length === 0 || g.length === 0 || a.length !== g.length) {
    return { pass: false, page, component };
  }
  let changed = 0;
  for (let i = 0; i < a.length; i += 1) {
    if (a[i] !== g[i]) {
      changed += 1;
    }
  }
  const ratio = changed / a.length;
  if (ratio <= limit) {
    return { pass: true };
  }
  return { pass: false, page, component, ratio };
}

module.exports = {
  DEVICE_SERIAL,
  DEFAULT_TOLERANCE,
  GOLDENS_DIR,
  TOLERANCES_PATH,
  STATES,
  proofCommand,
  buildProofPlan,
  assertCounts,
  isBlankViewport,
  isDiagnosticOnly,
  toleranceFor,
  diffAgainstGolden,
};
