'use strict';
// T5 — FR-DX-002 (AC-DX-009): leaf-edit + token-edit dev-channel acceptance.
// Hermetic: node:test + node:assert/strict + stdlib only. No network, no secrets,
// no live services, no filesystem, no clock. No new production: this file drives
// the staged batch-1 units (`../lib/refresh.js` = T1, `../lib/token-swap.js` = T2)
// read-only through a test-local dev-channel fixture (form with typed input +
// open nav stack) and asserts both halves in one scenario.
// ASSUMPTIONS (plan genuinely silent on fixture/helper shape; most defensible
// behavior tested — marked ASSUMPTION in T5.md):
// - A-T5a: `runDevChannelScenario` is a test-local helper (no new production
//   surface) taking `{ tree, leafEdit, tokenDelta }` and returning
//   `{ formPreserved, navPreserved, rethemedWithoutReload }`.
// - A-T5b: form input lives at `tree.forms.<name>.draft`, nav at `tree.navStack`,
//   resolved color at `component.resolved.color`, token name `colorPrimary`.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { applyComponentRefresh } = require('../lib/refresh.js');
const { applyTokenSwap } = require('../lib/token-swap.js');

function makeDevChannelFixture() {
  return {
    tree: {
      components: [
        {
          id: 'leaf',
          output: '<Text>v1</Text>',
          instanceId: 'inst-leaf-1',
          localState: {},
          signals: { theme: 'light' },
          form: { draft: '' },
          resolved: { color: '#ffffff' },
        },
        {
          id: 'form',
          output: '<Input />',
          instanceId: 'inst-form-1',
          localState: {},
          signals: {},
          form: { draft: 'typed input here' },
          resolved: { color: '#ffffff' },
        },
      ],
      navStack: ['Home', 'Detail', 'Settings'],
      signals: { theme: 'light' },
      forms: { form: { draft: 'typed input here' } },
    },
    leafEdit: { componentId: 'leaf', nextOutput: '<Text>v2</Text>' },
    tokenDelta: { tokens: { colorPrimary: '#000000' } },
  };
}

function runDevChannelScenario(fixture) {
  const { tree, leafEdit, tokenDelta } = fixture;
  const formBefore = tree.forms.form.draft;
  const navBefore = [...tree.navStack];

  applyComponentRefresh(tree, leafEdit);
  const formPreserved = tree.forms.form.draft === formBefore;
  const navPreserved =
    tree.navStack.length === navBefore.length &&
    tree.navStack.every((entry, i) => entry === navBefore[i]);

  const swapOut = applyTokenSwap(tree, tokenDelta);
  const rethemed = swapOut.tree.components.every(
    (c) => c.resolved && c.resolved.color === tokenDelta.tokens.colorPrimary,
  );
  const rethemedWithoutReload =
    rethemed &&
    swapOut.reinstantiated.length === 0 &&
    swapOut.tree.fullReload !== true;

  return { formPreserved, navPreserved, rethemedWithoutReload };
}

describe('runDevChannelScenario — leaf edit + token edit in one channel', () => {
  it('passes the full scenario: form kept, nav kept, re-themed without reload', () => {
    const result = runDevChannelScenario(makeDevChannelFixture());
    assert.deepEqual(result, {
      formPreserved: true,
      navPreserved: true,
      rethemedWithoutReload: true,
    });
  });

  it('swaps the edited leaf output while driving the scenario', () => {
    const fixture = makeDevChannelFixture();
    runDevChannelScenario(fixture);
    assert.equal(
      fixture.tree.components.find((c) => c.id === 'leaf').output,
      '<Text>v2</Text>',
    );
  });

  it('preserves the typed form input across the leaf edit', () => {
    const fixture = makeDevChannelFixture();
    const result = runDevChannelScenario(fixture);
    assert.equal(result.formPreserved, true);
    assert.equal(fixture.tree.forms.form.draft, 'typed input here');
  });

  it('preserves the open nav stack depth and entries across the leaf edit', () => {
    const fixture = makeDevChannelFixture();
    const result = runDevChannelScenario(fixture);
    assert.equal(result.navPreserved, true);
    assert.deepEqual(fixture.tree.navStack, ['Home', 'Detail', 'Settings']);
  });

  it('re-themes every component with zero re-instantiations and no reload flag', () => {
    const fixture = makeDevChannelFixture();
    const beforeIds = fixture.tree.components.map((c) => c.instanceId);
    const result = runDevChannelScenario(fixture);
    assert.equal(result.rethemedWithoutReload, true);
    for (const c of fixture.tree.components) {
      assert.equal(c.resolved.color, '#000000');
    }
    assert.deepEqual(
      fixture.tree.components.map((c) => c.instanceId),
      beforeIds,
    );
    assert.notEqual(fixture.tree.fullReload, true);
  });

  it('returns exactly the three-boolean scenario shape', () => {
    const result = runDevChannelScenario(makeDevChannelFixture());
    assert.deepEqual(Object.keys(result).sort(), [
      'formPreserved',
      'navPreserved',
      'rethemedWithoutReload',
    ]);
  });

  it('rejects a leaf edit targeting an unknown component id', () => {
    const fixture = makeDevChannelFixture();
    fixture.leafEdit = { componentId: 'missing', nextOutput: '<Text>x</Text>' };
    assert.throws(() => runDevChannelScenario(fixture), /Unknown component id/);
  });
});
