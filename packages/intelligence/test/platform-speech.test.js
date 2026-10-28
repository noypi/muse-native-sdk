'use strict';
// T10 — Platform speech renderer (Android TTS + TalkBack, zero network) (Batch 4).
// T11 — Speech controls bar/minimal/none + platform-only error states (Batch 4).
// Plan Interfaces (verbatim):
// T10: `createPlatformSpeech({ script?:string, children?:TextBearing, prompt?:string, voice?:string, rate?:number, tts:AndroidTtsBridge }): SpeechHandle` with `play()|pause()|stop()`, states `speaking|paused|interrupted|idle`; script = resolved children text (or `script` override), re-resolved debounced + cancellable on bound-value change, never from a half-flush; `prompt` shapes phrasing via `reason` route only (wording/pauses), never a voice-model selector; asserts zero network/proxy calls during speech.
// T11: `controls:'bar'|'minimal'|'none'` — bar = tier-2 play/pause/stop IconButtons + LinearProgress + route chip, minimal = play/pause only, none = headless driven by `play()`; speech behavior identical across all three; errors surface platform TTS states only (`{ code:'tts-missing'|'tts-busy'|'interrupted', source:'platform' }`), never the proxy taxonomy, never raw upstream.
// Spec refs: FR-MUSE-011 AC-MUSE-037/038; FR-MEDIA-009/MEDIA-AUDIO barge-in/ducking/lifecycle (consumed doubles).
// Q-MUSE-003 settled: AndroidTtsBridge = { speak, stop, pause?, resume?, setRate?, setVoice? };
// TextBearing = unknown; SpeechHandle = { play, pause, stop }; voice/rate are
// platform settings passed to the bridge, never upstream params; no `speak`
// proxy route exists or fires. T2 (lib/routes.js) exists — speech fires zero
// proxy/network calls (fetch spy / `upstream:` tripwire).
// Hermetic: injected TTS bridge / audio-focus / lifecycle doubles; no network,
// no secrets, no env, no live backend. node:test + node:assert/strict only.

const { describe, it } = require('node:test');
const assert = require('node:assert/strict');

const { createPlatformSpeech } = require('../lib/platform-speech.js');

// --- Doubles ---------------------------------------------------------------

function makeTts(overrides = {}) {
  const calls = [];
  const tts = {
    spoken: [],
    stopped: 0,
    paused: 0,
    resumed: 0,
    rate: undefined,
    voice: undefined,
    speak(text) { calls.push(['speak', text]); this.spoken.push(text); },
    stop() { calls.push(['stop']); this.stopped += 1; },
    pause() { calls.push(['pause']); this.paused += 1; },
    resume() { calls.push(['resume']); this.resumed += 1; },
    setRate(rate) { calls.push(['setRate', rate]); this.rate = rate; },
    setVoice(voice) { calls.push(['setVoice', voice]); this.voice = voice; },
    ...overrides,
  };
  return { tts, calls };
}

function makeFetchSpy() {
  const calls = [];
  const fetchImpl = async (url, opts) => {
    calls.push({ url, opts });
    if (String(url).startsWith('upstream:')) throw new Error('upstream must never fire during speech');
    throw new Error(`unexpected fetch during speech: ${url}`);
  };
  return { fetchImpl, calls };
}

function makeAudioFocus() {
  const events = [];
  return {
    events,
    duck() { events.push('duck'); },
    restore() { events.push('restore'); },
  };
}

function makeLifecycle() {
  const handlers = {};
  return {
    handlers,
    onPause: (fn) => { handlers.pause = fn; },
    onResume: (fn) => { handlers.resume = fn; },
    firePause() { if (handlers.pause) handlers.pause(); },
    fireResume() { if (handlers.resume) handlers.resume(); },
  };
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// --- T10: handle shape + states --------------------------------------------

describe('T10 — handle shape and states (AC-MUSE-037)', () => {
  it('exposes play/pause/stop functions and starts idle', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'hello', tts });
    assert.equal(typeof h.play, 'function');
    assert.equal(typeof h.pause, 'function');
    assert.equal(typeof h.stop, 'function');
    assert.equal(h.state, 'idle');
  });

  it('play() speaks and moves to speaking; pause() moves to paused; stop() to idle', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'hello', tts });
    h.play();
    assert.equal(h.state, 'speaking');
    assert.deepEqual(tts.spoken, ['hello']);
    h.pause();
    assert.equal(h.state, 'paused');
    h.stop();
    assert.equal(h.state, 'idle');
  });

  it('pause() from idle stays idle and touches no bridge call', () => {
    const { tts, calls } = makeTts();
    const h = createPlatformSpeech({ script: 'hello', tts });
    assert.equal(h.state, 'idle');
    h.pause();
    assert.equal(h.state, 'idle');
    assert.deepEqual(tts.spoken, []);
    assert.equal(calls.length, 0);
  });
});

// --- T10: script resolution -------------------------------------------------

describe('T10 — script = resolved children text or script override (AC-MUSE-037)', () => {
  it('speaks the script override when both script and children are given', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'override', children: 'children text', tts });
    h.play();
    assert.deepEqual(tts.spoken, ['override']);
  });

  it('resolves string children as the script', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ children: 'plain text', tts });
    h.play();
    assert.deepEqual(tts.spoken, ['plain text']);
  });

  it('resolves TextBearing arrays/nested children by joining text (any children whose resolved text is the script)', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ children: ['hello', [' ', 'world']], tts });
    h.play();
    assert.deepEqual(tts.spoken, ['hello world']);
  });

  it('re-resolves debounced + cancellable on bound-value change, never from a half-flush', async () => {
    // ASSUMPTION (contract-silent edge): the handle exposes `setChildren(next)`
    // to rebind the value; rapid rebinds debounce to one speak of final text.
    const { tts } = makeTts();
    const h = createPlatformSpeech({ children: 'first', tts });
    assert.equal(typeof h.setChildren, 'function');
    h.setChildren('half-');
    h.setChildren('half-flush? no — final');
    await sleep(30);
    h.play();
    assert.deepEqual(tts.spoken, ['half-flush? no — final']);
    assert.ok(!tts.spoken.includes('half-'));
  });
});

// --- T10: voice/rate/prompt platform-only -----------------------------------

describe('T10 — voice/rate are platform settings; prompt is reason-route phrasing only (AC-MUSE-037)', () => {
  it('passes voice/rate to the bridge setters, never as upstream params', () => {
    const { tts, calls } = makeTts();
    const h = createPlatformSpeech({ script: 'hi', voice: 'android-voice-a', rate: 1.25, tts });
    h.play();
    assert.equal(tts.voice, 'android-voice-a');
    assert.equal(tts.rate, 1.25);
    const speakCalls = calls.filter(([k]) => k === 'speak');
    assert.equal(speakCalls.length, 1);
    assert.equal(speakCalls[0][1], 'hi');
  });

  it('prompt never becomes a voice-model selector on the bridge call', () => {
    const { tts, calls } = makeTts();
    const h = createPlatformSpeech({ script: 'hi', prompt: 'pause politely', tts });
    h.play();
    const speakCalls = calls.filter(([k]) => k === 'speak');
    assert.equal(speakCalls.length, 1);
    assert.equal(speakCalls[0][1], 'hi');
    assert.ok(!calls.some(([k, v]) => k === 'setVoice' && String(v ?? '').includes('prompt')));
  });
});

// --- T10: zero network ------------------------------------------------------

describe('T10 — zero network/proxy calls during speech (AC-MUSE-037, T2 tripwire)', () => {
  it('fires zero fetch calls while playing, pausing and stopping', () => {
    const { tts } = makeTts();
    const { fetchImpl, calls } = makeFetchSpy();
    const h = createPlatformSpeech({ script: 'hi', tts, fetch: fetchImpl });
    h.play();
    h.pause();
    h.stop();
    assert.equal(calls.length, 0);
  });

  it('never fires an upstream: URL even when a prompt is present', () => {
    const { tts } = makeTts();
    const { fetchImpl, calls } = makeFetchSpy();
    const h = createPlatformSpeech({ script: 'hi', prompt: 'shape wording', tts, fetch: fetchImpl });
    h.play();
    assert.ok(calls.every((c) => !String(c.url).startsWith('upstream:')));
  });

  it('no speak proxy route exists in lib/routes.js required set', async () => {
    const { assertMuseRoutes } = require('../lib/routes.js');
    const seen = [];
    const fetchImpl = async (name) => {
      seen.push(name);
      if (name === 'routes') {
        return { ok: true, json: async () => ({ transcribe: {}, reason: {}, 'generate-image': {} }) };
      }
      if (name === 'presets') return { ok: true, json: async () => ({}) };
      throw new Error(`unexpected: ${name}`);
    };
    const result = await assertMuseRoutes(fetchImpl);
    assert.deepEqual(result, { ok: true, missing: [] });
    assert.ok(!seen.some((s) => String(s).includes('speak')));
  });
});

// --- T10: barge-in / ducking / lifecycle (consumed doubles) ------------------

describe('T10 — barge-in, ducking, lifecycle pause via injected doubles (FR-MEDIA-009)', () => {
  it('mic-tap barge-in interrupts: stop() on bridge, state interrupted', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'long text', tts });
    h.play();
    assert.equal(typeof h.interrupt, 'function');
    h.interrupt('mic-tap');
    assert.equal(h.state, 'interrupted');
    assert.equal(tts.stopped, 1);
  });

  it('audio-focus ducking notifies the injected focus double without stopping speech', () => {
    const { tts } = makeTts();
    const focus = makeAudioFocus();
    const h = createPlatformSpeech({ script: 'hi', tts, audioFocus: focus });
    h.play();
    assert.equal(typeof h.duck, 'function');
    h.duck();
    assert.deepEqual(focus.events, ['duck']);
    assert.equal(h.state, 'speaking');
  });

  it('lifecycle pause moves speaking to paused via the injected lifecycle double', () => {
    const { tts } = makeTts();
    const lifecycle = makeLifecycle();
    const h = createPlatformSpeech({ script: 'hi', tts, lifecycle });
    h.play();
    lifecycle.firePause();
    assert.equal(h.state, 'paused');
  });
});

// --- T11: controls variants --------------------------------------------------

describe('T11 — controls bar/minimal/none descriptors (AC-MUSE-038)', () => {
  it('bar exposes tier-2 play/pause/stop IconButtons + LinearProgress + route chip', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'hi', tts, controls: 'bar' });
    assert.equal(typeof h.controls, 'object');
    assert.deepEqual(h.controls.buttons, ['play', 'pause', 'stop']);
    assert.equal(h.controls.progress, 'linear');
    assert.equal(typeof h.controls.routeChip, 'string');
  });

  it('minimal exposes play/pause only, no stop and no progress', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'hi', tts, controls: 'minimal' });
    assert.deepEqual(h.controls.buttons, ['play', 'pause']);
    assert.equal(h.controls.progress, undefined);
  });

  it('none is headless: no buttons, driven by play()', () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'hi', tts, controls: 'none' });
    assert.deepEqual(h.controls.buttons, []);
    h.play();
    assert.equal(h.state, 'speaking');
    assert.deepEqual(tts.spoken, ['hi']);
  });

  it('speech behavior is identical across bar/minimal/none', () => {
    for (const controls of ['bar', 'minimal', 'none']) {
      const { tts } = makeTts();
      const h = createPlatformSpeech({ script: 'same', tts, controls });
      h.play();
      assert.deepEqual(tts.spoken, ['same']);
      assert.equal(h.state, 'speaking');
      h.stop();
      assert.equal(h.state, 'idle');
    }
  });
});

// --- T11: platform-only errors ------------------------------------------------

describe('T11 — platform-only error states (AC-MUSE-038)', () => {
  it("missing bridge surfaces { code:'tts-missing', source:'platform' }", () => {
    const h = createPlatformSpeech({ script: 'hi', tts: null });
    assert.equal(typeof h.play, 'function');
    try {
      h.play();
      assert.fail('expected play to record a platform error');
    } catch (err) {
      assert.equal(err.code, 'tts-missing');
      assert.equal(err.source, 'platform');
    }
    assert.deepEqual(h.lastError, { code: 'tts-missing', source: 'platform' });
  });

  it("busy engine surfaces { code:'tts-busy', source:'platform' }, never proxy taxonomy", () => {
    const { tts } = makeTts({
      speak() { const e = new Error('busy'); e.code = 'tts-busy'; throw e; },
    });
    const h = createPlatformSpeech({ script: 'hi', tts });
    try {
      h.play();
      assert.fail('expected busy to throw');
    } catch (err) {
      assert.equal(err.code, 'tts-busy');
      assert.equal(err.source, 'platform');
    }
    const PROXY_CODES = ['auth', 'upstream', 'timeout', 'schema', 'unknown-preset', 'raw-model', 'raw-endpoint', 'raw-param', 'stale-turn', 'denied', 'no-speech'];
    assert.ok(!PROXY_CODES.includes(h.lastError.code));
  });

  it("interrupted surfaces { code:'interrupted', source:'platform' } with no raw upstream leak", () => {
    const { tts } = makeTts();
    const h = createPlatformSpeech({ script: 'hi', tts });
    h.play();
    h.interrupt('mic-tap');
    assert.deepEqual(h.lastError, { code: 'interrupted', source: 'platform' });
    assert.equal(JSON.stringify(h.lastError).includes('https://'), false);
    assert.equal(JSON.stringify(h.lastError).includes('upstream'), false);
  });
});
