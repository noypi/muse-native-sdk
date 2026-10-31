'use strict';
// FR-M3-009 automated gallery device proof (AC-M3-020/TC-M3-021, AC-M3-021/TC-M3-022).
// Batch 3 (T4). Hermetic pure gates over the proof plan, viewport pixels, and
// golden diffs. No network, no secrets, no live services, no adb invocation:
// proofCommand only renders the command string the tester runs on device.
//
// Device serial (ASSUMPTION A1): every device step uses 127.0.0.1:6569.
// Tolerance default (ASSUMPTION A3): 0.02 until build/goldens/m3-tolerances.json
// pins per-component values; the committed file is authoritative.

const DEVICE_SERIAL = '127.0.0.1:6569';

const DEFAULT_TOLERANCE = 0.02;

const THEMES = ['light', 'dark'];

const STATES = ['hover', 'focus', 'pressed', 'disabled'];

function proofCommand(page) {
  const name = typeof page === 'string' && page.length > 0 ? page : 'gallery';
  return `adb -s ${DEVICE_SERIAL} exec-out screencap -p > ${name}.png`;
}

function variantsFor(entry) {
  const props = (entry && entry.props) || {};
  for (const key of Object.keys(props)) {
    const spec = props[key];
    if (spec && Array.isArray(spec.values) && spec.values.length > 0) {
      return spec.values.map((value) => String(value));
    }
  }
  return ['default'];
}

function buildProofPlan(manifest) {
  const components = (manifest && manifest.components) || [];
  const plan = [];
  for (const entry of components) {
    const tier = entry.tier;
    const variants = variantsFor(entry);
    for (const variant of variants) {
      for (const state of STATES) {
        for (const theme of THEMES) {
          plan.push({
            component: entry.name,
            tier,
            variant,
            state,
            theme,
            page: `tier${tier}/${entry.name}/${variant}/${state}/${theme}`,
          });
        }
      }
    }
  }
  return plan;
}

function assertCounts(plan, manifest) {
  const expected = buildProofPlan(manifest);
  const seen = new Set(
    (Array.isArray(plan) ? plan : []).map(
      (entry) => `${entry.component}|${entry.variant}|${entry.state}|${entry.theme}`,
    ),
  );
  for (const entry of expected) {
    const key = `${entry.component}|${entry.variant}|${entry.state}|${entry.theme}`;
    if (!seen.has(key)) {
      throw new Error(
        `gallery-proof count mismatch: missing page ${entry.page} for component ${entry.component}`,
      );
    }
  }
  if (Array.isArray(plan) && plan.length !== expected.length) {
    const first = expected[0];
    throw new Error(
      `gallery-proof count mismatch: expected ${expected.length} pages, saw ${plan.length} (page ${first.page} component ${first.component})`,
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

function diffAgainstGolden(actual, golden, tolerance, meta) {
  const page = meta && meta.page ? meta.page : 'unknown-page';
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
  THEMES,
  STATES,
  proofCommand,
  buildProofPlan,
  assertCounts,
  isBlankViewport,
  isDiagnosticOnly,
  diffAgainstGolden,
};
