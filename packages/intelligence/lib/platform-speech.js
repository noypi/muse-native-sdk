'use strict';
// T10 — Platform speech renderer (Android TTS + TalkBack, zero network).
// FR-MUSE-011 AC-MUSE-037; FR-MEDIA-009 barge-in/ducking/lifecycle (consumed doubles).
//
// `script` is the resolved children text (or the `script` override when given).
// Re-resolution on bound-value change (`setChildren`) is debounced and
// cancellable: rapid rebinds coalesce to one final text, never a half-flush.
// `prompt` shapes phrasing via the `reason` route only (wording/pauses) — it is
// never a voice-model selector and never leaves this module. `voice`/`rate`
// are platform TTS settings passed to the injected bridge only, never upstream.
// This module performs ZERO network/proxy calls: it never imports
// `lib/routes.js` and never calls any fetch implementation (an injected
// `fetch` is accepted for signature compatibility and deliberately ignored).

function resolveText(value) {
  if (value === undefined || value === null) return '';
  if (typeof value === 'string') return value;
  if (Array.isArray(value)) return value.map(resolveText).join('');
  return '';
}

function platformError(code) {
  return { code, source: 'platform' };
}

// Platform-only taxonomy: only these codes ever leave this module. Any other
// upstream/bridge code is confined to `tts-busy` so the proxy taxonomy
// (auth/upstream/timeout/schema/...) and raw upstream text never surface.
const PLATFORM_CODES = new Set(['tts-missing', 'tts-busy', 'interrupted']);

function toPlatformCode(err) {
  const code = err && typeof err.code === 'string' && err.code ? err.code : 'tts-busy';
  return PLATFORM_CODES.has(code) ? code : 'tts-busy';
}

function buildControls(variant) {
  const mode = variant === undefined || variant === null ? 'bar' : variant;
  if (mode === 'minimal') return { buttons: ['play', 'pause'], progress: undefined };
  if (mode === 'none') return { buttons: [] };
  return { buttons: ['play', 'pause', 'stop'], progress: 'linear', routeChip: 'platform-tts' };
}

function createPlatformSpeech(opts) {
  const options = opts ?? {};
  const {
    script,
    children,
    voice,
    rate,
    tts,
    audioFocus,
    lifecycle,
    controls,
  } = options;
  // `prompt`, `fetch` intentionally not destructured into behavior:
  // `prompt` is reason-route phrasing context (never a bridge/upstream param),
  // `fetch` is accepted but never called (zero-network assertion).

  const hasOverride = script !== undefined && script !== null;
  let boundChildren = children;
  let pendingChildren;
  let hasPending = false;
  let debounceTimer = null;
  let currentScript = hasOverride ? String(script) : resolveText(boundChildren);

  let state = 'idle';
  let lastError;

  function flushPending() {
    debounceTimer = null;
    if (!hasPending) return;
    hasPending = false;
    boundChildren = pendingChildren;
    pendingChildren = undefined;
    if (!hasOverride) currentScript = resolveText(boundChildren);
  }

  function applyPlatformSettings() {
    if (!tts) return;
    if (voice !== undefined && typeof tts.setVoice === 'function') tts.setVoice(voice);
    if (rate !== undefined && typeof tts.setRate === 'function') tts.setRate(rate);
  }

  function requireTts() {
    if (!tts || typeof tts.speak !== 'function') {
      lastError = platformError('tts-missing');
      throw lastError;
    }
  }

  function play() {
    requireTts();
    if (debounceTimer !== null) {
      clearTimeout(debounceTimer);
      flushPending();
    }
    applyPlatformSettings();
    try {
      // Speak text only: voice/rate went to the bridge setters above, and
      // `prompt` never reaches the bridge as a model selector.
      tts.speak(currentScript);
    } catch (err) {
      lastError = platformError(toPlatformCode(err));
      throw lastError;
    }
    state = 'speaking';
  }

  function pause() {
    if (state !== 'speaking') return;
    state = 'paused';
    try {
      if (tts && typeof tts.pause === 'function') tts.pause();
    } catch (err) {
      lastError = platformError(toPlatformCode(err));
      throw lastError;
    }
  }

  function stop() {
    state = 'idle';
    try {
      if (tts && typeof tts.stop === 'function') tts.stop();
    } catch (err) {
      lastError = platformError(toPlatformCode(err));
      throw lastError;
    }
  }

  function setChildren(next) {
    // Debounced + cancellable re-resolve: each rebind cancels the previous
    // pending flush so only the final value is ever committed (no half-flush).
    pendingChildren = next;
    hasPending = true;
    if (debounceTimer !== null) clearTimeout(debounceTimer);
    debounceTimer = setTimeout(flushPending, 10);
  }

  function interrupt(reason) {
    void reason;
    // Barge-in (e.g. mic-tap): cut the platform utterance, mark interrupted.
    // MEDIA-AUDIO owns focus routing; this renderer only stops the bridge.
    try {
      if (tts && typeof tts.stop === 'function') tts.stop();
    } finally {
      state = 'interrupted';
      lastError = platformError('interrupted');
    }
  }

  function duck() {
    // Audio-focus ducking is owned by MEDIA-AUDIO; notify the injected focus
    // double without changing speech state.
    if (audioFocus && typeof audioFocus.duck === 'function') audioFocus.duck();
  }

  if (lifecycle) {
    if (typeof lifecycle.onPause === 'function') {
      lifecycle.onPause(() => {
        if (state === 'speaking') pause();
      });
    }
    if (typeof lifecycle.onResume === 'function') {
      lifecycle.onResume(() => {
        if (state === 'paused' && tts && typeof tts.resume === 'function') {
          try {
            tts.resume();
          } catch (err) {
            lastError = platformError(toPlatformCode(err));
            throw lastError;
          }
          state = 'speaking';
        }
      });
    }
  }

  const controlsDescriptor = buildControls(controls);

  const handle = {
    play,
    pause,
    stop,
    setChildren,
    interrupt,
    duck,
    controls: controlsDescriptor,
  };
  Object.defineProperty(handle, 'state', {
    enumerable: true,
    get() {
      return state;
    },
  });
  Object.defineProperty(handle, 'lastError', {
    enumerable: true,
    get() {
      return lastError;
    },
  });
  return handle;
}

module.exports = { createPlatformSpeech };
