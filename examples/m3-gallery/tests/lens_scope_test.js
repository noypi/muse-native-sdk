'use strict';
// FR-SAMPLE-015 Batch 6 (T9 contract) — LIVE lens scope.
//
// Hermetic: node:test + node:assert/strict only. In-memory story lists,
// in-test fake proxy transport (records route calls, no network), committed
// source reads only (no secrets, no app config load, no device, no keys).
//
// Binds ONLY to the plan interface:
//   T9: `LensAnswer runLiveLens(PhotoBytes photo)` — proxy route `segment`
//       then `reason` per `proxy/proxy.config.json` (FR-MUSE-002 edge-proxy
//       contract, REFERENCED not reinvented); returns model-produced
//       `{ maskPolygon, reasonText }`; error shape: unknown lens id →
//       `{ code: 'unknown-story', storyId }` (never blank); no `stub://`
//       fixture or recorded response counts as an answer; no key material
//       in the gallery target, no separate lens URL/key/service shape.
// Navigation reuses the existing gallery deep-link + bridge path
// (`gallery://story/<id>` via galleryNav.js + RouterBridge navigate).
//
// ASSUMPTIONS:
//   A1 (live module path): the live lens entry lives at
//       `src/muse-lens.live.js` (CommonJS) exporting `runLiveLens(photo,
//       transport)` plus `LENS_STORY_ID`. The coder reconciles the exact
//       file layout; the behavior below is what must hold.
//   A2 (transport seam): the RouterBridge boundary is mocked at the seam —
//       tests inject an in-memory `transport` double recording
//       `{ route, params }` calls (seam doubles allowed). No implementation
//       double is asserted (no private fields, no call-order beyond the
//       spec'd segment-then-reason contract).
//   A3 (story registry): the lens story carries a stable `id` resolvable
//       through `createNavigator(stories).openStoryLink('gallery://story/<id>')`.

const { describe, it, beforeEach } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const NAV_PATH = '../src/galleryNav.js';
const STORIES_PATH = '../src/muse-lens.stories.js';
const LIVE_CANDIDATES = ['../src/muse-lens.live.js', '../src/muse-lens.js'];
const GALLERY_YAML_PATH = '../muse-native.yaml';

let navError = null;
let nav = null;
try {
  nav = require(NAV_PATH);
} catch (err) {
  navError = err;
}

function requireNav() {
  assert.equal(
    navError,
    null,
    `gallery navigation module missing at ${NAV_PATH}: ${navError && navError.message}`,
  );
  for (const fn of ['createNavigator', 'openStoryLink', 'requestShellAdvance']) {
    assert.equal(typeof nav[fn], 'function', `galleryNav must export ${fn}()`);
  }
}

function loadLiveLens() {
  let lastErr = null;
  for (const p of LIVE_CANDIDATES) {
    try {
      const mod = require(p);
      return { mod, path: p, err: null };
    } catch (err) {
      lastErr = err;
    }
  }
  return { mod: null, path: null, err: lastErr };
}

function requireLiveLens() {
  const loaded = loadLiveLens();
  assert.ok(
    loaded.mod,
    `live lens module missing (tried ${LIVE_CANDIDATES.join(', ')}): ${loaded.err && loaded.err.message}`,
  );
  assert.equal(
    typeof (loaded.mod && loaded.mod.runLiveLens),
    'function',
    `live lens module must export runLiveLens(photo, transport) (missing: ${loaded.err && loaded.err.message})`,
  );
  return loaded.mod;
}

// In-memory fake proxy transport (RouterBridge seam double): records every
// route call, serves canned model-produced answers, never touches network.
function createFakeProxy(answers) {
  const calls = [];
  const canned = Object.assign(
    {
      segment: { maskPolygon: [[0, 0], [64, 0], [64, 64], [0, 64]] },
      reason: { reasonText: 'a ceramic mug centered on the table' },
    },
    answers,
  );
  return {
    calls,
    async invoke(route, params) {
      calls.push({ route, params: params || {} });
      if (route === 'segment') return canned.segment;
      if (route === 'reason') return canned.reason;
      throw Object.assign(new Error(`unknown route: ${route}`), {
        code: 'unknown-route',
        route,
      });
    },
    routes() {
      return calls.map((c) => c.route);
    },
  };
}

function readSource(relPath) {
  const abs = path.join(__dirname, relPath);
  return fs.readFileSync(abs, 'utf8');
}

describe('T9 — LIVE lens path (FR-SAMPLE-015)', () => {
  beforeEach(() => {
    delete globalThis.__museTestBroadcasts;
  });

  it('live lens module exports runLiveLens(photo, transport)', () => {
    const lens = requireLiveLens();
    assert.equal(typeof lens.runLiveLens, 'function');
  });

  it('runLiveLens calls proxy route segment then reason (FR-MUSE-002 order)', async () => {
    const lens = requireLiveLens();
    const transport = createFakeProxy();
    const answer = await lens.runLiveLens({ bytes: [1, 2, 3], id: 'photo-1' }, transport);
    assert.deepEqual(transport.routes(), ['segment', 'reason']);
    assert.ok(answer && typeof answer === 'object');
  });

  it('runLiveLens returns model-produced { maskPolygon, reasonText }', async () => {
    const lens = requireLiveLens();
    const transport = createFakeProxy({
      segment: { maskPolygon: [[10, 10], [20, 10], [20, 20]] },
      reason: { reasonText: 'the red bicycle against the wall' },
    });
    const answer = await lens.runLiveLens({ bytes: [9, 9, 9], id: 'photo-2' }, transport);
    assert.deepEqual(answer.maskPolygon, [[10, 10], [20, 10], [20, 20]]);
    assert.equal(answer.reasonText, 'the red bicycle against the wall');
  });

  it('lens story resolves via gallery://story/<id> through the navigate path', () => {
    requireNav();
    const lens = requireLiveLens();
    assert.ok(
      typeof lens.LENS_STORY_ID === 'string' && lens.LENS_STORY_ID.length > 0,
      'live lens module must export a stable LENS_STORY_ID',
    );
    const stories = [{ id: 'buttons' }, { id: lens.LENS_STORY_ID }, { id: 'cards' }];
    const n = nav.createNavigator(stories);
    const out = n.openStoryLink(`gallery://story/${lens.LENS_STORY_ID}`);
    assert.equal(out.storyId, lens.LENS_STORY_ID);
    assert.equal(out.index, 1);
    // Same id through the shell-chrome deep-link bridge emits a
    // RouterBridge navigate record (mock transport seam at the boundary).
    globalThis.__museTestBroadcasts = [];
    const issued = nav.requestShellAdvance(
      1,
      'deep-link',
      `gallery://story/${lens.LENS_STORY_ID}`,
      {},
    );
    assert.equal(issued, true);
    assert.equal(globalThis.__museTestBroadcasts.length, 1);
    assert.equal(
      globalThis.__museTestBroadcasts[0].action,
      'muse.native.NAVIGATE',
    );
  });

  it('unknown lens id fails closed per unknown-story (never blank)', () => {
    requireNav();
    const lens = requireLiveLens();
    const stories = [{ id: 'buttons' }, { id: lens.LENS_STORY_ID }];
    const n = nav.createNavigator(stories);
    const out = n.openStoryLink('gallery://story/lens-no-such-photo');
    assert.deepEqual(out, { code: 'unknown-story', storyId: 'lens-no-such-photo' });
  });

  it('lens story declares ONLY the edge-proxy contract (no separate URL/key/service)', () => {
    const lens = requireLiveLens();
    const story = lens.lensStory();
    assert.ok(story && typeof story === 'object', 'live lens module must export lensStory()');
    const backend = story.backend || {};
    assert.ok(
      backend.proxy === true || backend.contract === 'edge-proxy',
      'lens story backend must declare the FR-MUSE-002 edge-proxy contract',
    );
    const raw = JSON.stringify(story);
    for (const forbidden of ['http://', 'https://', 'apiKey', 'api_key', 'API_KEY', 'Bearer ', 'sk-']) {
      assert.ok(
        !raw.includes(forbidden),
        `lens story must not carry a separate lens URL/key/service shape (found ${JSON.stringify(forbidden)})`,
      );
    }
    // Committed story descriptors carry no key material either.
    const storiesSrc = readSource(STORIES_PATH);
    for (const forbidden of ['apiKey', 'api_key', 'API_KEY', 'Bearer ', 'sk-live', 'sk-test']) {
      assert.ok(
        !storiesSrc.includes(forbidden),
        `muse-lens.stories.js must contain no key material (found ${JSON.stringify(forbidden)})`,
      );
    }
  });

  it('gallery target carries no key material and no separate lens endpoint', () => {
    requireNav();
    const yamlSrc = readSource(GALLERY_YAML_PATH);
    for (const forbidden of ['apiKey', 'api_key', 'API_KEY', 'Bearer ', 'sk-live', 'sk-test']) {
      assert.ok(
        !yamlSrc.includes(forbidden),
        `muse-native.yaml (gallery) must contain no key material (found ${JSON.stringify(forbidden)})`,
      );
    }
    assert.ok(
      !/https?:\/\//.test(yamlSrc),
      'muse-native.yaml (gallery) must define no separate lens URL (edge-proxy contract only)',
    );
    // The live story itself must not be stub-backed.
    const lens = requireLiveLens();
    const story = lens.lensStory();
    assert.ok(
      !JSON.stringify(story).includes('stub://'),
      'live lens story must not reference a stub:// fixture',
    );
  });

  it('a stub:// answer NEVER counts as a pass (explicit anti-stub)', async () => {
    const lens = requireLiveLens();
    assert.equal(
      typeof lens.countsAsPass,
      'function',
      'live lens module must export countsAsPass(answer)',
    );
    assert.equal(
      lens.countsAsPass({ maskPolygon: 'stub://kitchen', reasonText: 'stubbed' }),
      false,
      'stub:// mask must never count as a pass',
    );
    assert.equal(
      lens.countsAsPass({ maskPolygon: [[0, 0], [1, 1]], reasonText: 'stubbed error replay' }),
      false,
      'recorded/stubbed error replay must never count as a pass',
    );
    const transport = createFakeProxy();
    const answer = await lens.runLiveLens({ bytes: [7, 7, 7], id: 'photo-3' }, transport);
    assert.equal(lens.countsAsPass(answer), true);
  });

  it('unknown proxy route is rejected before any upstream call (fail-closed)', async () => {
    const lens = requireLiveLens();
    const transport = createFakeProxy();
    await assert.rejects(
      () => lens.runLiveLens({ bytes: [5], id: 'photo-4', route: 'no-such-route' }, transport),
      (err) => err && (err.code === 'unknown-route' || err.code === 'unknown-story'),
    );
  });
});
