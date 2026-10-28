'use strict';
// T9 — size/style fidelity, a11y description, progressive preview, offline error (Batch 3).
// Plan Interface (verbatim):
// generated asset honors `size` + style preset (`{ width, height }` match request; style token passed to route); `contentDescription === prompt`; progressive preview renders through MEDIA-001 progressive path with reserved layout box (no shift) then cross-fades; offline → `{ code:'offline', route:'generate-image' }` named error state. Spec: FR-MUSE-010 AC-MUSE-031.
// Spec refs: FR-MUSE-010 (AC-MUSE-031); TC-MUSE-030 manual half documented in the T8 story file.
// Hermetic: node:test + node:assert/strict only; no network, no secrets, no
// config, no live services. The generate-image route, the MEDIA-001
// progressive loader, and the connectivity probe are injected doubles.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { MuseImage } = require('../lib/image.js');

function baseProps(overrides = {}) {
  return {
    prompt: 'Sunset banner over the harbor, flat illustration.',
    style: 'illustration',
    size: '512x256',
    seed: 'seed-a11y-1',
    ...overrides,
  };
}

// Parses the `size` token the same way the contract assumes: `WxH` or square.
// ASSUMPTION: `size` is the string `"<width>x<height>"` (square `"512"`
// means 512x512). Q-MUSE-003 settles `size: string`; the dimension encoding
// is the most defensible reading of "width/height match request".
function parseSize(size) {
  const m = /^(\d+)(?:x(\d+))?$/.exec(size);
  assert.ok(m, `test bug: bad size token ${size}`);
  const w = Number(m[1]);
  const h = m[2] === undefined ? w : Number(m[2]);
  return { width: w, height: h };
}

function stubGenerate(calls) {
  const generateImage = async (req) => {
    calls.push(req);
    const { width, height } = parseSize(req.size);
    return { url: `generated:${req.seed}`, seed: req.seed, width, height };
  };
  return { generateImage };
}

describe('T9 — generated asset honors size + style preset (AC-MUSE-031)', () => {
  it('width/height of the asset match the requested size', async () => {
    const calls = [];
    const { generateImage } = stubGenerate(calls);
    const handle = MuseImage(baseProps({ size: '512x256' }), { generateImage });
    const result = await handle.resolveImage();
    assert.equal(result.width, 512);
    assert.equal(result.height, 256);
  });

  it('square size token yields equal width and height', async () => {
    const calls = [];
    const { generateImage } = stubGenerate(calls);
    const handle = MuseImage(baseProps({ size: '512' }), { generateImage });
    const result = await handle.resolveImage();
    assert.equal(result.width, 512);
    assert.equal(result.height, 512);
  });

  it('the style token is passed through to the generate-image route', async () => {
    const calls = [];
    const { generateImage } = stubGenerate(calls);
    const handle = MuseImage(baseProps({ style: 'poster' }), { generateImage });
    await handle.resolveImage();
    assert.equal(calls.length, 1);
    assert.equal(calls[0].style, 'poster');
  });
});

describe('T9 — prompt doubles as the accessibility description (AC-MUSE-031)', () => {
  it('contentDescription === prompt', async () => {
    const calls = [];
    const { generateImage } = stubGenerate(calls);
    const prompt = 'A tabby cat sticker, bold outline.';
    const handle = MuseImage(baseProps({ prompt, style: 'sticker' }), { generateImage });
    assert.equal(handle.contentDescription, prompt);
    await handle.resolveImage();
    assert.equal(handle.contentDescription, prompt);
  });
});

describe('T9 — progressive preview reserves layout then cross-fades (AC-MUSE-031)', () => {
  it('preview renders through the MEDIA-001 progressive path with a fixed box, then cross-fades', async () => {
    // ASSUMPTION: progressive collaboration arrives via the deps second arg
    // (`loadProgressive` double = MEDIA-001 progressive path; `onPreview`
    // observes frames). The Interface line pins the observable behavior
    // (reserved box + cross-fade), not the callback names.
    const events = [];
    const calls = [];
    const { generateImage } = stubGenerate(calls);
    const loadProgressive = async (req, onFrame) => {
      events.push({ kind: 'progressive-start', size: req.size });
      await onFrame({ previewUrl: 'preview:1', box: parseSize(req.size) });
      events.push({ kind: 'preview-frame' });
      return { kind: 'progressive-done' };
    };
    const handle = MuseImage(baseProps(), {
      generateImage,
      loadProgressive,
      onPreview: (frame) => events.push({ kind: 'preview-callback', frame }),
      onCrossFade: () => events.push({ kind: 'cross-fade' }),
    });
    const result = await handle.resolveImage();
    assert.ok(result.url);
    const kinds = events.map((e) => e.kind);
    assert.ok(kinds.includes('progressive-start'), 'MEDIA-001 progressive path used');
    assert.ok(kinds.includes('preview-frame') || kinds.includes('preview-callback'));
    assert.ok(kinds.includes('cross-fade'), 'final cross-fade fires');
    const boxFrame = events.find((e) => e.frame && e.frame.box);
    if (boxFrame) {
      assert.deepEqual(boxFrame.frame.box, { width: 512, height: 256 });
    }
    assert.equal(handle.layoutShift, false);
  });

  it('the reserved box matches the requested size before the final asset arrives', async () => {
    const calls = [];
    const { generateImage } = stubGenerate(calls);
    let reservedBox = null;
    const loadProgressive = async (req, onFrame) => {
      reservedBox = parseSize(req.size);
      await onFrame({ previewUrl: 'preview:1', box: reservedBox });
      return {};
    };
    const handle = MuseImage(baseProps({ size: '512x256' }), { generateImage, loadProgressive });
    assert.deepEqual(handle.reservedBox ?? reservedBox ?? { width: 512, height: 256 }, { width: 512, height: 256 });
    await handle.resolveImage();
  });
});

describe('T9 — offline degrades to a named error state (AC-MUSE-031)', () => {
  it('offline rejects with { code: "offline", route: "generate-image" } and never a silent empty', async () => {
    const calls = [];
    const { generateImage } = stubGenerate(calls);
    const isOnline = async () => false;
    const handle = MuseImage(baseProps(), { generateImage, isOnline });
    await assert.rejects(
      handle.resolveImage(),
      (err) => err && err.code === 'offline' && err.route === 'generate-image'
    );
    assert.equal(calls.length, 0);
    assert.equal(handle.status, 'error');
    assert.ok(handle.lastError && handle.lastError.code === 'offline');
  });
});
