'use strict';
// T3+T4 — demo3 hands-free operation layer (FR-SAMPLE-013, AC-SAMPLE-038).
// Failing-first: MUST FAIL until examples/muse-demo/src/abilities/phoneAbilities.js
// and examples/muse-demo/src/abilities/voiceRouter.js exist.
// Shared-file rule: this one test file covers both T3 and T4 (assigned to T3,
// mapped in both contracts); hermetic unit assertions live here, the
// TC-SAMPLE-038-RESULT.md evidence file is written by the T4 verification, not here.
// Hermetic: node:test + node:assert/strict only. Real T1/T2 modules are consumed
// (packages/abilities/lib/abilities.js, packages/native/muse-mcp.js) — never re-stubbed.
// Fixed turnId doubles; no network, no secrets, no config, no live services.
//
// Plan T3 Interface (verbatim):
// `routeUtterance(text: string, ctx: { turnId: string }): Promise<{ ok: true, ability: "profile.open-settings" | "gallery.open" | "gallery.select" | "gallery.image.select" | "app.close", turnId: string } | { ok: true, ability: "reason-fallback", turnId: string } | { ok: false, code: "capability-denied", ability: string, turnId: string }>`; `touchEquivalent(ability: string): { screen: string, action: string }` — every voice ability has a non-null touch equivalent; unknown utterances resolve to `reason-fallback`, never silent.
// Plan T4 Interface (verbatim):
// `verifyHandsFreeFlow(): Promise<{ steps: [...5 abilities with echo + touchEquivalent...], denial: { ok: false, code: "capability-denied" }, verdict: "pass" | "fail" }>`

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { defineAbility } = require('../../../packages/abilities/lib/abilities.js');
const { createMuseMcp } = require('../../../packages/native/muse-mcp.js');
const { ABILITY_NAMES } = require('../src/abilities/phoneAbilities.js');
const {
  routeUtterance,
  touchEquivalent,
  verifyHandsFreeFlow,
} = require('../src/abilities/voiceRouter.js');

const TURN = 'turn-handsfree-001';

// Canonical five demo verbs (single list lives in phoneAbilities.js; asserted here).
const FIVE = [
  'profile.open-settings',
  'gallery.open',
  'gallery.select',
  'gallery.image.select',
  'app.close',
];

// Representative voice utterances, one per ability (see ASSUMPTION in T3 contract).
const UTTERANCES = {
  'profile.open-settings': 'open profile settings',
  'gallery.open': 'open gallery',
  'gallery.select': 'select this gallery',
  'gallery.image.select': 'select this image',
  'app.close': 'close the app',
};

describe('T3 — canonical ability names (AC-SAMPLE-038)', () => {
  it('phoneAbilities exports exactly the five demo verbs', () => {
    assert.deepEqual([...ABILITY_NAMES].sort(), [...FIVE].sort());
  });
});

describe('T3 — five utterances route with calling turnId (AC-SAMPLE-038)', () => {
  for (const ability of FIVE) {
    it(`"${UTTERANCES[ability]}" routes to ${ability} stamped with the calling turnId`, async () => {
      const res = await routeUtterance(UTTERANCES[ability], { turnId: TURN });
      assert.deepEqual(res, { ok: true, ability, turnId: TURN });
    });
  }

  it('each routing stamps its own calling turnId, not a fixed one', async () => {
    const res = await routeUtterance(UTTERANCES['gallery.open'], { turnId: 'turn-other-777' });
    assert.equal(res.turnId, 'turn-other-777');
  });
});

describe('T3 — every voice ability has a non-null touch equivalent (AC-SAMPLE-038)', () => {
  for (const ability of FIVE) {
    it(`${ability} has a { screen, action } touch equivalent`, () => {
      const te = touchEquivalent(ability);
      assert.ok(te !== null && typeof te === 'object', 'touch equivalent must be non-null');
      assert.equal(typeof te.screen, 'string');
      assert.ok(te.screen.length > 0, 'screen must be non-empty');
      assert.equal(typeof te.action, 'string');
      assert.ok(te.action.length > 0, 'action must be non-empty');
    });
  }

  it('reason-fallback also carries a touch equivalent (never a dead end)', () => {
    const te = touchEquivalent('reason-fallback');
    assert.ok(te !== null && typeof te === 'object');
    assert.ok(typeof te.screen === 'string' && te.screen.length > 0);
    assert.ok(typeof te.action === 'string' && te.action.length > 0);
  });
});

describe('T3 — unknown utterances resolve to reason-fallback, never silent (AC-SAMPLE-038)', () => {
  it('gibberish falls through to reason-fallback with the calling turnId', async () => {
    const res = await routeUtterance('blargle wumpus xyz', { turnId: TURN });
    assert.deepEqual(res, { ok: true, ability: 'reason-fallback', turnId: TURN });
  });

  it('empty/blank utterance falls through to reason-fallback, never silent', async () => {
    const res = await routeUtterance('   ', { turnId: TURN });
    assert.deepEqual(res, { ok: true, ability: 'reason-fallback', turnId: TURN });
  });

  it('every routeUtterance resolves to a defined shape (no undefined, no throw)', async () => {
    for (const text of ['hello?', '', UTTERANCES['app.close']]) {
      const res = await routeUtterance(text, { turnId: TURN });
      assert.ok(res !== undefined && res !== null, `must resolve for ${JSON.stringify(text)}`);
      assert.ok('turnId' in res, 'result always carries turnId');
    }
  });
});

describe('T3 — ungranted ability denies with capability-denied and performs no action (AC-SAMPLE-038)', () => {
  it('restricted-capability host denies the ungranted ability via real T1/T2 modules', async () => {
    let ran = 0;
    const defs = FIVE.map((name) =>
      defineAbility({ name, args: {}, run: () => { ran += 1; return `${name}-value`; } }),
    );
    const handle = createMuseMcp({
      abilities: defs,
      control: { turnId: TURN },
      capabilities: ['gallery.open'],
    });
    try {
      const res = await handle.call('app.close', {});
      assert.deepEqual(res, { ok: false, code: 'capability-denied', turnId: TURN });
      assert.equal(ran, 0, 'denied ability must perform no action');
    } finally {
      handle.dispose();
    }
  });

  it('router reports ungranted ability as { ok: false, code: capability-denied, ability, turnId }', async () => {
    const res = await routeUtterance(UTTERANCES['app.close'], { turnId: TURN, capabilities: ['gallery.open'] });
    assert.equal(res.ok, false);
    assert.equal(res.code, 'capability-denied');
    assert.equal(res.ability, 'app.close');
    assert.equal(res.turnId, TURN);
  });
});

describe('T4 — verifyHandsFreeFlow voice-only 5-step flow + denial (TC-SAMPLE-038)', () => {
  it('flows through all five abilities in order with echo + touchEquivalent, denial named, verdict pass', async () => {
    const report = await verifyHandsFreeFlow();
    assert.equal(report.verdict, 'pass');
    assert.equal(report.steps.length, 5);
    const expectedOrder = [
      'profile.open-settings',
      'gallery.open',
      'gallery.select',
      'gallery.image.select',
      'app.close',
    ];
    report.steps.forEach((step, i) => {
      assert.equal(step.step, i + 1);
      assert.equal(step.ability, expectedOrder[i]);
      assert.equal(typeof step.turnId, 'string');
      assert.ok(step.turnId.length > 0, 'each step stamped with a turnId');
      assert.equal(step.echo, 'numbered-voice+tappable-fallback');
      assert.equal(step.touchEquivalent, true);
    });
    assert.deepEqual(report.denial, { ok: false, code: 'capability-denied' });
  });

  it('flow denial names the code and never performs a silent action', async () => {
    const report = await verifyHandsFreeFlow();
    assert.equal(report.denial.ok, false);
    assert.equal(report.denial.code, 'capability-denied');
  });
});
