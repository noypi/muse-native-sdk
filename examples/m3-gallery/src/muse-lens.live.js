'use strict';

// T9 LIVE lens path (FR-SAMPLE-015, AC-SAMPLE-042/043).
//
// Gallery-side live entry only. The backend contract is REFERENCED from
// FR-MUSE-002 (edge-proxy `segment` then `reason` routes per
// `proxy/proxy.config.json`) — no URL/key/service shape is defined here and
// no key material lives in the gallery target. Production wiring injects the
// real RouterBridge transport (`invoke(route, params)`); tests inject the
// in-memory fake at the same seam.

const LENS_STORY_ID = 'lens-live';

const LIVE_ROUTES = ['segment', 'reason'];

// Fail closed on an unknown requested route before any upstream call
// (FR-MUSE-002 / AC-MUSE-006). `photo.route` is an optional override seam;
// anything naming a route outside the edge-proxy table is rejected.
function assertKnownRoute(photo) {
  const requested = photo !== null && typeof photo === 'object' ? photo.route : undefined;
  if (requested === undefined || requested === null) {
    return;
  }
  if (typeof requested !== 'string' || !LIVE_ROUTES.includes(requested)) {
    throw Object.assign(new Error(`unknown route: ${String(requested)}`), {
      code: 'unknown-route',
      route: requested,
    });
  }
}

async function runLiveLens(photo, transport) {
  assertKnownRoute(photo);
  if (transport === null || typeof transport !== 'object' || typeof transport.invoke !== 'function') {
    throw Object.assign(new Error('unknown route: missing proxy transport'), {
      code: 'unknown-route',
    });
  }
  const seg = await transport.invoke('segment', { photo });
  const maskPolygon = seg !== null && typeof seg === 'object' ? seg.maskPolygon : undefined;
  const rea = await transport.invoke('reason', { photo, maskPolygon });
  const reasonText = rea !== null && typeof rea === 'object' ? rea.reasonText : undefined;
  return { maskPolygon, reasonText };
}

// Stable lens story descriptor: backend declares ONLY the edge-proxy
// contract. No URL, key, or service shape by construction.
function lensStory() {
  return {
    id: LENS_STORY_ID,
    name: 'MuseLens / live',
    state: 'live',
    backend: { proxy: true, contract: 'edge-proxy', routes: [...LIVE_ROUTES] },
    props: {},
  };
}

// A recorded/stub answer NEVER counts as a pass (AC-SAMPLE-044):
// string masks (`stub://…`), stub-flavoured reason text, or anything that is
// not a model-produced { polygon, text } pair all fail the gate.
function countsAsPass(answer) {
  if (answer === null || typeof answer !== 'object') {
    return false;
  }
  const { maskPolygon, reasonText } = answer;
  if (typeof maskPolygon === 'string') {
    return false;
  }
  if (typeof reasonText === 'string' && /(stub|replay|recorded|fixture)/i.test(reasonText)) {
    return false;
  }
  if (!Array.isArray(maskPolygon) || maskPolygon.length === 0) {
    return false;
  }
  for (const pt of maskPolygon) {
    if (!Array.isArray(pt) || pt.length < 2) {
      return false;
    }
    for (const n of pt) {
      if (typeof n !== 'number' || !Number.isFinite(n)) {
        return false;
      }
    }
  }
  if (typeof reasonText !== 'string' || reasonText.length === 0) {
    return false;
  }
  return true;
}

module.exports = { runLiveLens, lensStory, countsAsPass, LENS_STORY_ID, LIVE_ROUTES };
