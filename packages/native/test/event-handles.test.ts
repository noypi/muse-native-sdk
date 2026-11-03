/**
 * TC-M3-034 (AC-M3-033) + TC-M3-035 (AC-M3-034) — fail-closed event handles
 * with per-component conformance verts (FR-M3-013).
 *
 * Hermetic: node:test + node:assert/strict only. Imports the Button handle
 * factory through the package public surface (`../button.js`, the canonical
 * delivery-lane copy picked in Batch 2 per plan T7), reads
 * muse.manifest.json + index.d.ts from disk. No network, no secrets, no
 * services, no device.
 *
 * Batch-1 RED state: `../button.js` does not exist yet (canonical impl copy
 * is picked by the delivery lane in Batch 2, plan T7), so this file fails at
 * import today. Every test below is written against the plan's `Interface:`
 * line verbatim:
 *   `createButton({enabled,onPress}).press(lease)->{fired,frames:1,reinstantiated:0}`
 *   `foldButtonFrames(rawFrames)->0|1`
 *   `onButtonPress(...)`
 *
 * ASSUMPTION M-1 (lease mint is per-button): the plan names the lease ticket
 * (`{tapId}` single-use) and its behaviors but no mint function. FR-M3-013
 * Notes require the pending-registry to be scoped per-button, so minting is
 * a method on the handle: `handle.mint()` returns a single-use lease, or
 * `null` when disabled (disabled-never-mint, AC-M3-036). If the coder names
 * the mint differently, update these call sites — not the expected behavior.
 * ASSUMPTION M-2 (IconButton reuse): IconButton reuses the Button
 * lease/handle contract (same `press(lease)` shape); if the coder ships a
 * separate `createIconButton`, update the call site — not the behavior.
 * ASSUMPTION M-3 (binding N/A for Button): AC-M3-033 "binding write-through
 * applies where two-way". Button's minimal surface is `{enabled,onPress}`
 * (no two-way prop), so there is no Button binding to write through; the
 * write-through pattern itself is proven by selection.js Checkbox/Radio.
 * These tests pin that a Button press touches no binding-like surface.
 */

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createRequire } from 'node:module';

const require = createRequire(import.meta.url);
// RED today: the canonical delivery-lane copy does not exist yet (plan T7).
const { createButton, foldButtonFrames, onButtonPress } = require('../button.js');

const MANIFEST_URL = new URL('../muse.manifest.json', import.meta.url);
const DTS_URL = new URL('../index.d.ts', import.meta.url);

function manifestProps(name) {
  const manifest = JSON.parse(readFileSync(MANIFEST_URL, 'utf8'));
  const entry = manifest.components.find((c) => c.name === name);
  assert.ok(entry, `manifest must contain a ${name} entry`);
  return entry.props ?? {};
}

// ---------------------------------------------------------------------------
// TC-M3-034 — fail-closed handle semantics (AC-M3-033)
// ---------------------------------------------------------------------------

test('TC-M3-034: an enabled tap fires exactly once within one frame, no re-instantiation', () => {
  let calls = 0;
  const button = createButton({ enabled: true, onPress: () => { calls += 1; } });
  const lease = button.mint();
  assert.ok(lease, 'an enabled button must mint a lease');
  const result = button.press(lease);
  assert.deepEqual(
    { fired: result.fired, frames: result.frames, reinstantiated: result.reinstantiated },
    { fired: 1, frames: 1, reinstantiated: 0 },
  );
  assert.equal(calls, 1, 'onPress must fire exactly once per tap');
});

test('TC-M3-034: replaying a consumed lease fires nothing (fire-once per tap)', () => {
  let calls = 0;
  const button = createButton({ enabled: true, onPress: () => { calls += 1; } });
  const lease = button.mint();
  assert.equal(button.press(lease).fired, 1);
  const replay = button.press(lease);
  assert.equal(replay.fired, 0, 'a consumed lease must never fire again');
  assert.equal(calls, 1, 'exactly one listener invocation across tap + replay');
});

test('TC-M3-034: a lease minted for another handle is silent here (tap outside is silent)', () => {
  const a = createButton({ enabled: true, onPress: () => {} });
  let bCalls = 0;
  const b = createButton({ enabled: true, onPress: () => { bCalls += 1; } });
  const foreign = a.mint();
  const result = b.press(foreign);
  assert.equal(result.fired, 0, "another handle's lease must not fire this button");
  assert.equal(bCalls, 0);
});

test('TC-M3-034: a disabled button never fires and touches neither listener nor binding surface', () => {
  let calls = 0;
  const button = createButton({ enabled: false, onPress: () => { calls += 1; } });
  assert.equal(button.mint(), null, 'disabled-never-mint: a disabled button mints no lease');
  assert.equal(calls, 0, 'a disabled button must never notify its listener');
  assert.equal(button.pressed, undefined, 'no pressed shadow state may appear on a disabled handle');
});

test('TC-M3-034: a throwing listener never breaks the tap path (recorded, no escape)', () => {
  const button = createButton({
    enabled: true,
    onPress: () => { throw new Error('boom-not-real'); },
  });
  const lease = button.mint();
  let result;
  assert.doesNotThrow(() => { result = button.press(lease); }, 'a throwing listener must not escape press()');
  assert.equal(result.fired, 1, 'the tap still counts as fired exactly once');
  assert.equal(result.frames, 1);
  assert.equal(result.reinstantiated, 0);
});

test('TC-M3-034: a drag-revoked lease never fires tap (drag-no-fire)', () => {
  let calls = 0;
  const button = createButton({ enabled: true, onPress: () => { calls += 1; } });
  const lease = button.mint();
  lease.revoked = 'drag';
  const result = button.press(lease);
  assert.equal(result.fired, 0, 'a drag/slop/CANCEL-revoked lease must fire nothing');
  assert.equal(calls, 0);
});

test('TC-M3-034: foldButtonFrames maps raw frames to 0|1 (pure)', () => {
  assert.equal(foldButtonFrames([]), 0, 'no frames fold to 0');
  const folded = foldButtonFrames([{ tapId: 't-not-real-1' }]);
  assert.ok(folded === 0 || folded === 1, 'fold must return exactly 0 or 1');
});

test('TC-M3-034: onButtonPress agrees with press on the same tap (pure preview)', () => {
  let calls = 0;
  const button = createButton({ enabled: true, onPress: () => { calls += 1; } });
  const lease = button.mint();
  const expected = onButtonPress({ enabled: true, tapId: lease.tapId });
  const result = button.press(lease);
  assert.equal(result.fired, expected, 'onButtonPress must agree with press() on the same tap');
  assert.equal(calls, expected);
});

// ---------------------------------------------------------------------------
// TC-M3-035 — per-component conformance (AC-M3-034)
// ---------------------------------------------------------------------------

test('TC-M3-035: check-props parity — manifest == index.d.ts unions == factory echo', () => {
  const props = manifestProps('Button');
  assert.deepEqual(
    props.variant.values,
    ['filled', 'tonal', 'outlined', 'text', 'elevated'],
    'manifest Button.variant union is the single source',
  );
  const dts = readFileSync(DTS_URL, 'utf8');
  const body = dts.slice(dts.indexOf('ButtonProps'));
  const closed = body.slice(0, body.indexOf('};'));
  for (const value of props.variant.values) {
    assert.ok(closed.includes(`'${value}'`), `ButtonProps must include manifest value "${value}"`);
  }
  assert.ok(/\bonPress\??:/.test(closed), 'ButtonProps must declare the onPress event prop');
  const handler = () => {};
  const { components } = require('../index.js');
  const desc = components.Button({ variant: 'filled', label: 'x', onPress: handler });
  assert.equal(desc.props.variant, 'filled', 'factory echoes the manifest variant value');
  assert.equal(desc.props.onPress, handler, 'factory echoes the declared onPress prop');
});

test('TC-M3-035: unknown event key is absent from the schema and never becomes live (negative)', () => {
  const props = manifestProps('Button');
  assert.ok(props.onTap === undefined, 'Button schema must not contain an undeclared onTap key');
  let tapped = 0;
  const button = createButton({ enabled: true, onTap: () => { tapped += 1; } });
  const lease = button.mint();
  const result = button.press(lease);
  assert.equal(tapped, 0, 'an undeclared event key must never become a live listener');
  assert.equal(result.fired, 0, 'with no declared onPress there is nothing to fire');
});

test('TC-M3-035: IconButton tap verts — fire-once, disabled-no-fire, throwing-listener-safe', () => {
  let calls = 0;
  const button = createButton({ enabled: true, onPress: () => { calls += 1; } });
  const lease = button.mint();
  assert.equal(button.press(lease).fired, 1, 'IconButton-class handle fires once per tap');
  assert.equal(button.press(lease).fired, 0, 'replay stays silent');
  assert.equal(calls, 1);
  const disabled = createButton({ enabled: false, onPress: () => { calls += 1; } });
  assert.equal(disabled.mint(), null, 'disabled IconButton-class handle mints nothing');
  assert.equal(calls, 1, 'disabled handle never notifies');
});
