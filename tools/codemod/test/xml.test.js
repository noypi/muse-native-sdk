'use strict';
// T8 — FR-DX-006 (AC-DX-026): native XML/View converter — LinearLayout /
// ConstraintLayout / TextView-style layouts map to engine primitives;
// SurfaceView / MapView-style constructs go to `platformViews` via the
// interop path, never converted inline.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no
// secrets, no application config, no live services, no filesystem, no clock.
// ASSUMPTIONS (plan/specs silent on structural shape; most defensible
// behavior tested — marked ASSUMPTION in T8.md):
// - A-T8a: "output parses as TSX" uses the same in-file heuristic as T6/T7
//   (balanced {}, (), [] + a JSX element tag); a real parser check is
//   tester-layer work.
// - A-T8b: engine-primitive tag names are the coder's choice; tests pin only
//   that Android-layout spellings are gone (no LinearLayout, no TextView)
//   and that platform-view tags are NOT converted inline.
// - A-T8c: empty-string input yields `{ output: '', platformViews: [] }`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { convertXml } = require('../lib/xml.js');

// Minimal representative layout: LinearLayout + TextView, no platform views.
const CLEAN_LAYOUT = `
<LinearLayout
    xmlns:android="http://schemas.android.com/apk/res/android"
    android:orientation="vertical"
    android:layout_width="match_parent"
    android:layout_height="match_parent">
    <TextView
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:text="Hello" />
</LinearLayout>
`;

// Layout mixing a mapped container with platform-view constructs.
const PLATFORM_LAYOUT = `
<LinearLayout
    xmlns:android="http://schemas.android.com/apk/res/android"
    android:orientation="vertical"
    android:layout_width="match_parent"
    android:layout_height="match_parent">
    <TextView
        android:layout_width="wrap_content"
        android:layout_height="wrap_content"
        android:text="Preview" />
    <SurfaceView
        android:layout_width="match_parent"
        android:layout_height="200dp" />
    <com.google.android.gms.maps.MapView
        android:layout_width="match_parent"
        android:layout_height="200dp" />
</LinearLayout>
`;

function assertParsesAsTsx(output) {
  assert.equal(typeof output, 'string');
  assert.match(output, /<\w+[^>]*>/, 'output should contain a JSX element');
  const pairs = [['{', '}'], ['(', ')'], ['[', ']']];
  for (const [open, close] of pairs) {
    const opened = output.split(open).length - 1;
    const closed = output.split(close).length - 1;
    assert.equal(closed, opened, `unbalanced ${open}${close}`);
  }
}

describe('convertXml — sample layout conversion', () => {
  it('converts a sample layout to non-empty output', () => {
    const { output } = convertXml(CLEAN_LAYOUT);
    assert.equal(typeof output, 'string');
    assert.ok(output.length > 0);
  });

  it('maps away Android-layout spellings (LinearLayout, TextView)', () => {
    const { output } = convertXml(CLEAN_LAYOUT);
    assert.ok(!output.includes('LinearLayout'), 'no LinearLayout remnant');
    assert.ok(!output.includes('TextView'), 'no TextView remnant');
  });

  it('preserves text content from the layout', () => {
    const { output } = convertXml(CLEAN_LAYOUT);
    assert.ok(output.includes('Hello'), 'android:text content preserved');
  });

  it('produces output that parses as TSX', () => {
    const { output } = convertXml(CLEAN_LAYOUT);
    assertParsesAsTsx(output);
  });

  it('reports an empty platformViews list for the clean layout', () => {
    const { platformViews } = convertXml(CLEAN_LAYOUT);
    assert.deepEqual(platformViews, []);
  });
});

describe('convertXml — platform-view flagging', () => {
  it('flags SurfaceView-style constructs in platformViews', () => {
    const { platformViews } = convertXml(PLATFORM_LAYOUT);
    assert.ok(
      platformViews.some((entry) => entry.includes('SurfaceView')),
      `expected SurfaceView in report, got: ${JSON.stringify(platformViews)}`,
    );
  });

  it('flags MapView-style constructs in platformViews', () => {
    const { platformViews } = convertXml(PLATFORM_LAYOUT);
    assert.ok(
      platformViews.some((entry) => entry.includes('MapView')),
      `expected MapView in report, got: ${JSON.stringify(platformViews)}`,
    );
  });

  it('does not convert platform-view constructs inline', () => {
    const { output } = convertXml(PLATFORM_LAYOUT);
    assert.ok(!output.includes('SurfaceView'), 'no inline SurfaceView');
    assert.ok(!output.includes('MapView'), 'no inline MapView');
  });

  it('returns empty output parts with an empty report for empty input', () => {
    assert.deepEqual(convertXml(''), { output: '', platformViews: [] });
  });
});
