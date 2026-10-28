'use strict';
// T5 — Listening sheet in OverlayHost + Field-style write-through (Batch 2).
// Plan Interface (verbatim):
// `openListeningSheet(turn): SheetHandle` — renders in OverlayHost (RULE-M3-006)
// showing route + latencyMs + classification reason; `commitSheet(turn,
// targetId): void` writes final transcript into `target` preserving
// caret/selection/focus; mid-session route change preserves text, selection and
// focus.
// Spec refs: FR-MUSE-008 (AC-MUSE-025, AC-MUSE-032), RULE-M3-006 + FR-MUSE-004 +
// FR-M3-002 (required dependencies, consumed not reimplemented).
// Hermetic: node:test + node:assert/strict only. OverlayHost, clock, and the
// WritableState target are injected doubles with { value, caret, selection,
// focus } — no real @muse/core import, no network, no secrets, no config.
// Interim partials render in the sheet only; targetId binds at commit (T1 stale
// rule: a stale turn commits nothing).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createTurn } = require('../lib/turn.js');
const { createMuseVoice, openListeningSheet, commitSheet } = require('../lib/voice.js');

function stubTarget(value = 'note', caret = 4, selection = [4, 4]) {
  return { value, caret, selection: [...selection], focus: true };
}

function makeFactory(target) {
  const tuple = createMuseVoice({
    icon: 'mic',
    activeIcon: 'graphic_eq',
    iconVariant: 'outlined',
    activeVariant: 'filled',
    target,
    icons: new Set(['mic', 'graphic_eq', 'check']),
  });
  return tuple[2];
}

function stubHost() {
  const sheets = [];
  return {
    sheets,
    present(sheet) { sheets.push(sheet); return sheet; },
    dismiss(sheet) { sheet.dismissed = true; },
  };
}

describe('T5 — sheet renders route + latencyMs + reason (AC-MUSE-025)', () => {
  it('openListeningSheet renders in the OverlayHost with route/latencyMs/reason', () => {
    const target = stubTarget();
    const voiceState = makeFactory(target);
    const turn = createTurn();
    const host = stubHost();
    const sheet = openListeningSheet(turn, { voiceState, host, route: 'transcribe→reason', latencyMs: 42, reason: 'dictation' });
    assert.equal(host.sheets.length, 1);
    assert.equal(sheet.route, 'transcribe→reason');
    assert.equal(sheet.latencyMs, 42);
    assert.equal(typeof sheet.reason, 'string');
    assert.ok(sheet.reason.length > 0);
  });

  it('interim partials render in the sheet only; the target is untouched until commit', () => {
    const target = stubTarget('note', 4, [4, 4]);
    const voiceState = makeFactory(target);
    const turn = createTurn();
    const host = stubHost();
    const sheet = openListeningSheet(turn, { voiceState, host, route: 'transcribe→reason', latencyMs: 1, reason: 'r' });
    sheet.pushPartial('hel');
    sheet.pushPartial('hello');
    assert.deepEqual(sheet.partials, ['hel', 'hello']);
    assert.equal(target.value, 'note');
  });
});

describe('T5 — commit writes final transcript preserving caret/selection/focus', () => {
  it('commitSheet writes the final transcript into target, preserving caret/selection/focus', () => {
    const target = stubTarget('note', 2, [1, 3]);
    const voiceState = makeFactory(target);
    const turn = createTurn();
    const host = stubHost();
    const sheet = openListeningSheet(turn, { voiceState, host, route: 'transcribe→reason', latencyMs: 5, reason: 'r' });
    sheet.pushPartial('hello');
    sheet.setFinal('hello world');
    commitSheet(turn, 'target-a', { voiceState, target, sheet });
    assert.equal(target.value, 'hello world');
    assert.deepEqual(target.selection, [1, 3]);
    assert.equal(target.caret, 2);
    assert.equal(target.focus, true);
  });

  it('mid-session route change preserves text, selection and focus', () => {
    const target = stubTarget('draft', 3, [0, 5]);
    const voiceState = makeFactory(target);
    const turn = createTurn();
    const host = stubHost();
    const sheet = openListeningSheet(turn, { voiceState, host, route: 'transcribe', latencyMs: 5, reason: 'r' });
    sheet.pushPartial('hel');
    sheet.reroute('transcribe→reason', 9, 'dictation');
    assert.deepEqual(sheet.partials, ['hel']);
    sheet.setFinal('hello world');
    commitSheet(turn, 'target-a', { voiceState, target, sheet });
    assert.equal(target.value, 'hello world');
    assert.deepEqual(target.selection, [0, 5]);
    assert.equal(target.caret, 3);
    assert.equal(target.focus, true);
  });

  it('targetId binds at commit: committing to a moved-on target writes nothing (T1 stale rule)', () => {
    const target = stubTarget('note', 4, [4, 4]);
    const voiceState = makeFactory(target);
    const first = createTurn();
    const second = createTurn();
    const host = stubHost();
    const sheet1 = openListeningSheet(first, { voiceState, host, route: 'transcribe→reason', latencyMs: 5, reason: 'r' });
    sheet1.setFinal('first transcript');
    const sheet2 = openListeningSheet(second, { voiceState, host, route: 'transcribe→reason', latencyMs: 6, reason: 'r' });
    sheet2.setFinal('second transcript');
    commitSheet(second, 'target-a', { voiceState, target, sheet: sheet2 });
    assert.equal(target.value, 'second transcript');
    commitSheet(first, 'target-a', { voiceState, target, sheet: sheet1 });
    assert.equal(target.value, 'second transcript');
  });
});
