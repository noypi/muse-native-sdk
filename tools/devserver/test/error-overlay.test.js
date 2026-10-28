'use strict';
// T3 — FR-DX-002 (AC-DX-008): refresh error overlay with source-mapped frame + dismiss.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no secrets,
// no live services, no filesystem, no clock.
// ASSUMPTIONS (contract genuinely silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T3.md):
// - A-T3a: RefreshError is { message, frame: { file, line, column },
//   sourceMap?: { file, line, column } }; RenderedUI is a plain object, e.g.
//   { html: string }; OverlayHandle is { text, previousUI, dismissed, dismiss() }.
// - A-T3b: overlay text names the mapped `file:line`; dismiss() returns the
//   exact previous UI object with no reload flag and is idempotent.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { showErrorOverlay } = require('../lib/error-overlay.js');

function makeError() {
  return {
    message: 'boom',
    frame: { file: 'bundle.js', line: 1, column: 5 },
    sourceMap: { file: 'src/App.tsx', line: 42, column: 7 },
  };
}

function makeUI() {
  return { html: '<View>before</View>' };
}

describe('showErrorOverlay — mapped frame and dismiss restores UI', () => {
  it('shows the mapped file:line in the overlay text', () => {
    const handle = showErrorOverlay(makeError(), makeUI());
    assert.ok(handle.text.includes('src/App.tsx:42'));
  });

  it('keeps the previous UI mounted on the handle', () => {
    const ui = makeUI();
    const handle = showErrorOverlay(makeError(), ui);
    assert.equal(handle.previousUI, ui);
    assert.equal(handle.dismissed, false);
  });

  it('dismiss restores the exact previous UI with no reload', () => {
    const ui = makeUI();
    const handle = showErrorOverlay(makeError(), ui);
    const restored = handle.dismiss();
    assert.equal(restored, ui);
    assert.deepEqual(restored, { html: '<View>before</View>' });
    assert.notEqual(restored.fullReload, true);
    assert.equal(handle.dismissed, true);
  });

  it('dismiss is idempotent and returns the same UI', () => {
    const ui = makeUI();
    const handle = showErrorOverlay(makeError(), ui);
    const first = handle.dismiss();
    const second = handle.dismiss();
    assert.equal(second, first);
    assert.equal(second, ui);
  });

  it('falls back to the raw frame when no source map is given', () => {
    const err = makeError();
    delete err.sourceMap;
    const handle = showErrorOverlay(err, makeUI());
    assert.ok(handle.text.includes('bundle.js:1'));
  });

  it('includes the error message in the overlay text', () => {
    const handle = showErrorOverlay(makeError(), makeUI());
    assert.ok(handle.text.includes('boom'));
  });
});
