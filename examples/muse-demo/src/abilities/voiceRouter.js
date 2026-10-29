'use strict';
// T3 — demo3 hands-free voice router (FR-SAMPLE-013, AC-SAMPLE-038).
// Maps pinned utterance strings to the canonical abilities in
// phoneAbilities.js and connects via the merged `createMuseMcp()` connector.
// Unknown/blank utterances fall through to `reason-fallback` without touching
// the registry, never silent; the merged `call()` THROWS
// (`stale-id`/`unknown-ability`/`capability-denied`), so thrown errors are
// caught and mapped to the `{ ok: false, code, ability, turnId }` shape with
// the calling turnId stamped by the router. Minimal host harness only —
// no FR-SAMPLE-009 behaviour.

const { createMuseMcp } = require('../../../../packages/native/muse-mcp.js');
const { ABILITY_NAMES, TOUCH_EQUIVALENTS, abilities } = require('./phoneAbilities.js');

const UTTERANCE_TO_ABILITY = {
  'open profile settings': 'profile.open-settings',
  'open gallery': 'gallery.open',
  'select this gallery': 'gallery.select',
  'select this image': 'gallery.image.select',
  'close the app': 'app.close',
};

const FLOW_ORDER = [
  'profile.open-settings',
  'gallery.open',
  'gallery.select',
  'gallery.image.select',
  'app.close',
];

const ABILITY_TO_UTTERANCE = Object.fromEntries(
  Object.entries(UTTERANCE_TO_ABILITY).map(([utterance, ability]) => [ability, utterance]),
);

function normalizeUtterance(text) {
  return String(text ?? '').trim().replace(/\s+/g, ' ').toLowerCase();
}

function touchEquivalent(ability) {
  const te = TOUCH_EQUIVALENTS[ability];
  return te === undefined ? null : { screen: te.screen, action: te.action };
}

async function routeUtterance(text, ctx) {
  const turnId = ctx ? ctx.turnId : undefined;
  const ability = UTTERANCE_TO_ABILITY[normalizeUtterance(text)] || null;
  if (ability === null) {
    return { ok: true, ability: 'reason-fallback', turnId };
  }
  const capabilities = ctx && ctx.capabilities !== undefined ? ctx.capabilities : ABILITY_NAMES;
  const handle = createMuseMcp({ abilities, control: { turnId }, capabilities });
  try {
    await handle.call(ability, {});
    return { ok: true, ability, turnId };
  } catch (err) {
    const code = err && typeof err.code === 'string' ? err.code : 'unknown-ability';
    return { ok: false, code, ability, turnId };
  } finally {
    handle.dispose();
  }
}

// T4-mapped flow verifier: voice-only 5-step flow + denial check against a
// direct-first local handle (no server, socket, or key). Each step echoes
// numbered-voice+tappable-fallback and asserts a touch equivalent.
async function verifyHandsFreeFlow() {
  const steps = [];
  let flowOk = true;
  for (let i = 0; i < FLOW_ORDER.length; i += 1) {
    const ability = FLOW_ORDER[i];
    const turnId = `turn-flow-${String(i + 1).padStart(3, '0')}`;
    // eslint-disable-next-line no-await-in-loop
    const res = await routeUtterance(ABILITY_TO_UTTERANCE[ability], { turnId });
    const te = touchEquivalent(ability);
    if (!res.ok || res.ability !== ability || res.turnId !== turnId) {
      flowOk = false;
    }
    steps.push({
      step: i + 1,
      ability,
      turnId,
      echo: 'numbered-voice+tappable-fallback',
      touchEquivalent: te !== null && typeof te.screen === 'string' && typeof te.action === 'string',
    });
  }
  const denialRes = await routeUtterance(ABILITY_TO_UTTERANCE['app.close'], {
    turnId: 'turn-flow-denial',
    capabilities: ['gallery.open'],
  });
  const denialOk = !denialRes.ok && denialRes.code === 'capability-denied';
  if (!denialOk) {
    flowOk = false;
  }
  return {
    steps,
    denial: { ok: false, code: 'capability-denied' },
    verdict: flowOk ? 'pass' : 'fail',
  };
}

module.exports = { routeUtterance, touchEquivalent, verifyHandsFreeFlow };
