'use strict';
// T5 — MuseField route+reason+latency badge with text/selection/focus preserved (AC-MUSE-012).
// Hermetic: pure rebind, no dispatch, no network, no keys. Uses node:test + node:assert.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');

function freshField() {
  delete require.cache[path.resolve(__dirname, '../lib/field.js')];
  return require('../lib/field.js');
}

beforeEach(() => {
  delete require.cache[path.resolve(__dirname, '../lib/field.js')];
});

const VERDICT_A = { route: 'reason', reason: 'medical_symptoms→sensitive', latencyMs: 42 };
const VERDICT_B = { route: 'reason', reason: 'general→reason', latencyMs: 87 };

describe('bindField — pure rebind preserves text/selection/focus, attaches badge (AC-MUSE-012)', () => {
  it('returns identical text/selection/focus with the new badge attached', () => {
    const { bindField } = freshField();
    const out = bindField({
      value: 'fever for three days',
      selection: { start: 6, end: 11 },
      focused: true,
      verdict: VERDICT_A,
    });
    assert.equal(out.value, 'fever for three days');
    assert.deepEqual(out.selection, { start: 6, end: 11 });
    assert.equal(out.focused, true);
    assert.deepEqual(out.badge, { route: 'reason', reason: 'medical_symptoms→sensitive', latencyMs: 42 });
  });

  it('route change mid-session re-renders the badge only — text/selection/focus untouched', () => {
    const { bindField } = freshField();
    const before = bindField({
      value: 'notes stay here',
      selection: { start: 0, end: 5 },
      focused: false,
      verdict: VERDICT_A,
    });
    const after = bindField({
      value: before.value,
      selection: before.selection,
      focused: before.focused,
      verdict: VERDICT_B,
    });
    assert.equal(after.value, before.value);
    assert.deepEqual(after.selection, before.selection);
    assert.equal(after.focused, before.focused);
    assert.deepEqual(after.badge, { route: 'reason', reason: 'general→reason', latencyMs: 87 });
    assert.notDeepEqual(after.badge, before.badge);
  });

  it('preserves collapsed caret (start === end)', () => {
    const { bindField } = freshField();
    const out = bindField({
      value: 'caret here|',
      selection: { start: 10, end: 10 },
      focused: true,
      verdict: VERDICT_B,
    });
    assert.deepEqual(out.selection, { start: 10, end: 10 });
    assert.equal(out.value, 'caret here|');
  });

  it('preserves empty text and zero selection without dropping the badge', () => {
    const { bindField } = freshField();
    const out = bindField({
      value: '',
      selection: { start: 0, end: 0 },
      focused: false,
      verdict: VERDICT_A,
    });
    assert.equal(out.value, '');
    assert.deepEqual(out.selection, { start: 0, end: 0 });
    assert.deepEqual(out.badge, { route: 'reason', reason: 'medical_symptoms→sensitive', latencyMs: 42 });
  });

  it('never mutates the input object (write-through, no aliasing)', () => {
    const { bindField } = freshField();
    const input = {
      value: 'do not mutate',
      selection: { start: 1, end: 3 },
      focused: true,
      verdict: VERDICT_A,
    };
    const snapshot = JSON.parse(JSON.stringify(input));
    const out = bindField(input);
    assert.deepEqual(input, snapshot);
    assert.notEqual(out, input);
    assert.notEqual(out.selection, input.selection);
    assert.notEqual(out.badge, input.verdict);
  });

  it('preserves multiline / unicode text exactly', () => {
    const { bindField } = freshField();
    const value = 'línea 1\n🤒 fiebre 39°C\nline 3';
    const out = bindField({
      value,
      selection: { start: 2, end: 12 },
      focused: true,
      verdict: VERDICT_B,
    });
    assert.equal(out.value, value);
    assert.deepEqual(out.selection, { start: 2, end: 12 });
  });
});
