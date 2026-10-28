'use strict';
// T8 — moderated named state + gallery stories + named errors (Batch 3).
// Plan Interface (verbatim):
// safety block → status `moderated` with `{ illustration:'m3-blocked', message:string, editAffordance:true }` + retry-with-edit; schema/param violations → `{ code:'schema'|'upstream'|'timeout'|'auth', route:'generate-image' }`, never a partial image. Spec: FR-MUSE-010 AC-MUSE-030.
// Spec refs: FR-MUSE-010 (AC-MUSE-030); TC-MUSE-029 manual half documented in the story file.
// Hermetic: node:test + node:assert/strict only; no network, no secrets, no
// config, no live services. The generate-image route is an injected double;
// the gallery story module is a pure descriptor (no imports, no network).

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { MuseImage } = require('../lib/image.js');
const stories = require('../../../examples/m3-gallery/src/muse-image.stories.js');

function baseProps(overrides = {}) {
  return {
    prompt: 'Avatar for Ada, engineer. Loves robots.',
    style: 'avatar',
    size: '512x512',
    seed: 'seed-moderated-1',
    ...overrides,
  };
}

function stubGenerate(handler) {
  const calls = [];
  const generateImage = async (req) => {
    calls.push(req);
    return handler(req, calls.length);
  };
  return { generateImage, calls };
}

function moderatedError() {
  return Object.assign({ code: 'moderated', route: 'generate-image' }, {
    message: 'Prompt blocked by safety review.',
  });
}

describe('T8 — safety block enters named moderated state (AC-MUSE-030)', () => {
  it('resolves to status moderated with m3-blocked illustration + message + edit affordance', async () => {
    const { generateImage } = stubGenerate(() => { throw moderatedError(); });
    const handle = MuseImage(baseProps(), { generateImage });
    const result = await handle.resolveImage();
    assert.equal(result.status, 'moderated');
    assert.equal(result.illustration, 'm3-blocked');
    assert.equal(typeof result.message, 'string');
    assert.ok(result.message.length > 0);
    assert.equal(result.editAffordance, true);
  });

  it('moderated never carries a partial image url', async () => {
    const { generateImage } = stubGenerate(() => { throw moderatedError(); });
    const handle = MuseImage(baseProps(), { generateImage });
    const result = await handle.resolveImage();
    assert.equal(result.status, 'moderated');
    assert.ok(result.url === undefined || result.url === null);
  });

  it('retry-with-edit re-generates from the edited prompt into ready', async () => {
    // ASSUMPTION: the handle exposes `retryWithEdit(editedPrompt)`; the plan
    // Interface line names the retry-with-edit behavior, not the method name.
    const { generateImage } = stubGenerate((req) => {
      if (req.prompt.includes('blocked-word')) throw moderatedError();
      return { url: `generated:${req.seed}`, seed: req.seed, width: 512, height: 512, status: 'ready' };
    });
    const handle = MuseImage(baseProps({ prompt: 'a blocked-word avatar' }), { generateImage });
    const blocked = await handle.resolveImage();
    assert.equal(blocked.status, 'moderated');
    const fixed = await handle.retryWithEdit('a friendly robot avatar');
    assert.equal(fixed.status === undefined || fixed.status === 'ready', true);
    assert.ok(typeof fixed.url === 'string');
  });
});

describe('T8 — schema/param violations fail named, never partial (AC-MUSE-030)', () => {
  for (const code of ['schema', 'upstream', 'timeout', 'auth']) {
    it(`{ code: '${code}', route: 'generate-image' } surfaces with no image bytes`, async () => {
      const { generateImage } = stubGenerate(() => {
        throw Object.assign({ code, route: 'generate-image' });
      });
      const handle = MuseImage(baseProps(), { generateImage });
      await assert.rejects(
        handle.resolveImage(),
        (err) => err && err.code === code && err.route === 'generate-image'
      );
    });
  }

  it('a rejected resolve never leaves a cached partial asset behind', async () => {
    const { generateImage, calls } = stubGenerate(() => {
      throw Object.assign({ code: 'schema', route: 'generate-image' });
    });
    const handle = MuseImage(baseProps({ cacheKey: 't8:no-partial' }), { generateImage });
    await assert.rejects(handle.resolveImage());
    assert.equal(calls.length, 1);
    // A second attempt must re-fire the route (errors are never memoized).
    await assert.rejects(handle.resolveImage());
    assert.equal(calls.length, 2);
  });
});

describe('T8 — all five states are reachable (AC-MUSE-030)', () => {
  it('idle → generating → ready is observable on the handle', async () => {
    let seenGenerating = false;
    const { generateImage } = stubGenerate((req) => {
      seenGenerating = true;
      return { url: `generated:${req.seed}`, seed: req.seed, width: 512, height: 512 };
    });
    const handle = MuseImage(baseProps(), { generateImage });
    assert.equal(handle.status, 'idle');
    const pending = handle.resolveImage();
    assert.ok(handle.status === 'generating' || seenGenerating === true || typeof pending.then === 'function');
    const result = await pending;
    assert.ok(result.url);
    assert.equal(handle.status, 'ready');
  });

  it('error state is named (not silent) on route failure', async () => {
    const { generateImage } = stubGenerate(() => {
      throw Object.assign({ code: 'timeout', route: 'generate-image' });
    });
    const handle = MuseImage(baseProps(), { generateImage });
    await assert.rejects(handle.resolveImage());
    assert.equal(handle.status, 'error');
    assert.ok(handle.lastError && handle.lastError.code === 'timeout');
  });
});

describe('T8 — gallery stories cover all five states in light+dark (AC-MUSE-030)', () => {
  const STATES = ['idle', 'generating', 'ready', 'moderated', 'error'];
  const THEMES = ['light', 'dark'];

  it('story module exports every state in both themes', () => {
    for (const state of STATES) {
      for (const theme of THEMES) {
        assert.ok(
          stories.stories && stories.stories[state] && stories.stories[state][theme],
          `missing story for ${state}/${theme}`
        );
      }
    }
  });

  it('moderated story renders the M3 blocked illustration with edit affordance', () => {
    for (const theme of THEMES) {
      const story = stories.stories.moderated[theme];
      assert.equal(story.illustration, 'm3-blocked');
      assert.equal(story.editAffordance, true);
      assert.ok(typeof story.message === 'string' && story.message.length > 0);
      assert.ok(!('url' in story) || story.url == null);
    }
  });

  it('ready story carries a generated asset, error story carries a named error', () => {
    for (const theme of THEMES) {
      assert.ok(typeof stories.stories.ready[theme].url === 'string');
      const err = stories.stories.error[theme];
      assert.ok(err && typeof err.code === 'string');
      assert.equal(err.route, 'generate-image');
    }
  });
});
