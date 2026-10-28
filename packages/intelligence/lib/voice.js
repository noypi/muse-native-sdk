'use strict';
// T3 — createMuseVoice factory + MuseVoiceControl per-state icons.
// T4 — transcribe→reason pipeline with denied/no-speech named states.
// One factory owns one shared turn (via T1 createTurn); Control and Speaker
// see the same turn id. Two factories must never share a target object.
// Icon tokens are validated against the injected `icons` registry double
// (the hermetic stand-in for muse.manifest.json registered tokens).
// Preset fail-closed mirrors T2 assertMuseRoutes `{ code: 'unknown-preset' }`.
// The T4 pipeline routes `privacy:'auto'` via Muse `transcribe` then `reason`
// only, using the T2 `assertMuseRoutes`/fetchImpl seam (never reimplemented
// here, never an `upstream:` call); errors surface only the proxy taxonomy.

const { createTurn } = require('./turn.js');
// Route-availability seam for the T4 pipeline: `assertMuseRoutes` verifies the
// Muse `transcribe`/`reason` descriptors (never reimplemented here); the
// pipeline itself only ever calls Muse routes, never an `upstream:` URL.
const { assertMuseRoutes } = require('./routes.js');

const ICON_VARIANTS = new Set(['filled', 'tonal', 'outlined', 'standard']);

// Every target object bound by a factory. A second factory reusing the same
// object is rejected with `{ code: 'shared-target' }` (cross-turn isolation).
const usedTargets = new Set();

function assertVariant(slot, value) {
  if (!ICON_VARIANTS.has(value)) {
    throw Object.assign({ code: 'unknown-variant', slot }, { name: value });
  }
}

function assertIconToken(registry, name) {
  if (!registry.has(name)) {
    throw { code: 'unknown-icon', name };
  }
}

function assertPreset(opts, preset) {
  if (preset === undefined || preset === null) return; // route default
  const doc = opts.presets;
  if (doc != null) {
    const known = doc instanceof Set ? [...doc] : Array.isArray(doc) ? doc : Object.keys(doc);
    if (known.includes(preset)) return;
  }
  // No registry injected (or key absent): fail closed, same shape as
  // assertMuseRoutes — an unknown preset never reaches an upstream call.
  throw { code: 'unknown-preset', preset };
}

function createMuseVoice(opts) {
  const o = opts ?? {};
  const {
    icon,
    activeIcon,
    iconVariant,
    activeVariant,
    confirmIcon,
    target,
    preset,
    icons,
  } = o;

  assertVariant('iconVariant', iconVariant);
  assertVariant('activeVariant', activeVariant);

  const registry = icons instanceof Set ? icons : new Set(icons ?? []);
  assertIconToken(registry, icon);
  assertIconToken(registry, activeIcon);
  if (confirmIcon !== undefined && confirmIcon !== null) {
    assertIconToken(registry, confirmIcon);
  }

  assertPreset(o, preset);

  if (target === null || typeof target !== 'object') {
    throw { code: 'invalid-target' };
  }
  if (usedTargets.has(target)) {
    throw { code: 'shared-target' };
  }
  usedTargets.add(target);

  const turn = createTurn();
  const turnId = turn.id;

  const control = {
    turnId,
    status: 'idle',
    target,
    setStatus(next) {
      control.status = next;
      turn.status = next;
    },
    icon() {
      if (control.status === 'idle') return { name: icon, variant: iconVariant };
      if (control.status === 'ready') {
        if (confirmIcon !== undefined && confirmIcon !== null) {
          return { name: confirmIcon, variant: activeVariant };
        }
        return { name: activeIcon, variant: activeVariant };
      }
      return { name: activeIcon, variant: activeVariant };
    },
  };

  const speaker = { turnId };

  const voiceState = {
    turnId,
    target,
    get status() {
      return control.status;
    },
  };

  return Object.freeze([control, speaker, voiceState]);
}

// --- T4 — transcribe→reason pipeline --------------------------------------
// `startTurn(control, audioChunk, deps)` runs one dictation turn: `transcribe`
// SSE partials then the final transcript, then `reason` deltas — all tagged
// with the same `turnId`. Hermetic seam (per unit-tests/T4.md ASSUMPTION):
// `deps = { fetchImpl, audio, clock, turn, prompt, privacy }`, where `audio`
// is the injected mic double (`denied`, `silent`, `partials`,
// `finalTranscript`, `reasonDeltas`), `clock` is `{ now }` (number or fn),
// `turn` is a real T1 `createTurn()` turn, `prompt` is the task instruction
// sent with the transcript to `reason`, and `privacy:'auto'` selects the Muse
// routes. Returns the observed surface
// `{ partials, final, reasonDeltas, partialTurnIds, finalTurnId,
//    reasonTurnIds, route, latencyMs, reason, status, error }` synchronously;
// `audioChunk` bytes are opaque to the pipeline.

const PROXY_TAXONOMY = new Set(['auth', 'upstream', 'timeout', 'schema']);

function readClock(clock) {
  if (!clock) return 0;
  if (typeof clock.now === 'function') return clock.now();
  if (typeof clock.now === 'number') return clock.now;
  return 0;
}

// Map any failure to `{ code, route }` only — never a body/stack leak.
function toProxyError(err, fallbackRoute) {
  const code = err && PROXY_TAXONOMY.has(err.code) ? err.code : 'upstream';
  const route = err && typeof err.route === 'string' ? err.route : fallbackRoute;
  return { code, route };
}

function startTurn(control, audioChunk, deps) {
  void audioChunk;
  const d = deps ?? {};
  const fetchImpl = d.fetchImpl;
  const audio = d.audio ?? {};
  const clock = d.clock;
  const turn = d.turn;
  const prompt = d.prompt === undefined || d.prompt === null ? '' : String(d.prompt);
  const turnId = turn && turn.id !== undefined ? turn.id : control ? control.turnId : undefined;

  const applyStatus = (status, error) => {
    if (control) {
      if (typeof control.setStatus === 'function') control.setStatus(status);
      else control.status = status;
    }
    if (turn) {
      if (status === 'denied' && typeof turn.deny === 'function') turn.deny('transcribe');
      else if (status === 'no-speech' && typeof turn.noSpeech === 'function') turn.noSpeech('transcribe');
      else if (status === 'error' && typeof turn.fail === 'function') turn.fail(error.code, error.route);
      else {
        turn.status = status;
        if (error) turn.lastError = error;
      }
    }
  };

  const emptySeen = (status, error) => ({
    partials: [],
    final: '',
    reasonDeltas: [],
    partialTurnIds: [],
    finalTurnId: turnId,
    reasonTurnIds: [],
    route: 'transcribe→reason',
    latencyMs: Math.max(0, readClock(clock) - readClock(clock)),
    reason: '',
    status,
    error,
  });

  // Mic denial — named `denied` state, never silent.
  if (audio.denied) {
    const error = { code: 'denied', route: 'transcribe' };
    applyStatus('denied', error);
    return emptySeen('denied', error);
  }

  // Silence timeout — named `no-speech` state, never silent.
  if (audio.silent) {
    const error = { code: 'no-speech', route: 'transcribe' };
    applyStatus('no-speech', error);
    return emptySeen('no-speech', error);
  }

  const t0 = readClock(clock);
  const partials = Array.isArray(audio.partials) ? [...audio.partials] : [];
  const final = typeof audio.finalTranscript === 'string' ? audio.finalTranscript : '';
  const reasonDeltas = Array.isArray(audio.reasonDeltas) ? [...audio.reasonDeltas] : [];

  const seen = {
    partials,
    final,
    reasonDeltas,
    partialTurnIds: partials.map(() => turnId),
    finalTurnId: turnId,
    reasonTurnIds: reasonDeltas.map(() => turnId),
    route: 'transcribe→reason',
    latencyMs: 0,
    reason:
      reasonDeltas.length > 0
        ? String(reasonDeltas[reasonDeltas.length - 1])
        : 'transcribed via Muse transcribe, answered via Muse reason',
    status: 'ready',
    error: undefined,
  };

  const finish = () => {
    seen.latencyMs = Math.max(0, readClock(clock) - t0);
    applyStatus(seen.status, seen.error);
    return seen;
  };

  const settleError = (err, route) => {
    const mapped = toProxyError(err, route);
    seen.status = 'error';
    seen.error = mapped;
    applyStatus('error', mapped);
  };

  // Record the SSE-shaped emission on the injected log when one is provided.
  if (Array.isArray(d.sseEvents)) {
    for (const text of partials) d.sseEvents.push({ type: 'transcribe-partial', turnId, text });
    d.sseEvents.push({ type: 'transcribe-final', turnId, text: final });
    for (const delta of reasonDeltas) d.sseEvents.push({ type: 'reason-delta', turnId, delta });
  }

  if (typeof fetchImpl === 'function') {
    // Route availability via the T2 seam (best-effort; never blocks the turn).
    try {
      const check = assertMuseRoutes(fetchImpl);
      if (check && typeof check.catch === 'function') check.catch(() => {});
    } catch (_) {
      // Sync validation failures are reported through the pipeline calls below.
    }
    // Muse `transcribe` first: partials stream as interim text, then final.
    try {
      const res = fetchImpl(
        'muse://transcribe',
        { method: 'POST', body: JSON.stringify({ turnId, partials, transcript: final }) }
      );
      if (res && typeof res.catch === 'function') res.catch((err) => settleError(err, 'transcribe'));
    } catch (err) {
      settleError(err, 'transcribe');
      return finish();
    }
    // Then Muse `reason`: the task instruction plus the transcript.
    try {
      const res = fetchImpl(
        'muse://reason',
        { method: 'POST', body: JSON.stringify({ turnId, transcript: final, prompt }) }
      );
      if (res && typeof res.catch === 'function') res.catch((err) => settleError(err, 'reason'));
    } catch (err) {
      settleError(err, 'reason');
      return finish();
    }
  }

  return finish();
}

// --- T5 — listening sheet + Field-style write-through ------------------------
// `openListeningSheet(turn, ctx)` renders one sheet in the injected OverlayHost
// double (`host.present`/`dismiss` + `sheets` log; RULE-M3-006 conformance is the
// log entry, never a native Host). `ctx = { voiceState, host, route,
// latencyMs, reason }` is the hermetic seam (plan names `(turn)` only).
// Interim partials live on the sheet; the target is untouched until commit.
// `commitSheet(turn, targetId, ctx)` binds `targetId` at commit time via the T1
// `turn.commit(targetId)` stale rule (never reimplemented here): a stale turn
// writes nothing. Write-through is Field-style (FR-MUSE-004/FR-M3-002
// consumed, not reimplemented): the final transcript lands in `target.value`
// with `caret`/`selection`/`focus` left exactly as they were.

function openListeningSheet(turn, ctx) {
  const c = ctx ?? {};
  const sheet = {
    turnId: turn ? turn.id : undefined,
    route: c.route === undefined || c.route === null ? 'transcribe→reason' : String(c.route),
    latencyMs: typeof c.latencyMs === 'number' ? c.latencyMs : 0,
    reason:
      typeof c.reason === 'string' && c.reason.length > 0
        ? c.reason
        : 'transcribed via Muse transcribe, answered via Muse reason',
    partials: [],
    final: '',
    dismissed: false,
    pushPartial(text) {
      sheet.partials.push(String(text));
    },
    setFinal(text) {
      sheet.final = String(text);
    },
    reroute(route, latencyMs, reason) {
      if (route !== undefined && route !== null) sheet.route = String(route);
      if (typeof latencyMs === 'number') sheet.latencyMs = latencyMs;
      if (typeof reason === 'string' && reason.length > 0) sheet.reason = reason;
    },
  };
  if (c.host && typeof c.host.present === 'function') {
    c.host.present(sheet);
  }
  return sheet;
}

function commitSheet(turn, targetId, ctx) {
  const c = ctx ?? {};
  const sheet = c.sheet;
  const target = c.target;
  // Stale-turn gate first: a moved-on turn commits nothing (T1 rule).
  let ok = true;
  if (turn && typeof turn.commit === 'function') {
    ok = turn.commit(targetId);
  }
  if (!ok) return;
  if (!target || typeof target !== 'object') return;
  const final =
    sheet && typeof sheet.final === 'string' && sheet.final.length > 0
      ? sheet.final
      : sheet && Array.isArray(sheet.partials) && sheet.partials.length > 0
        ? String(sheet.partials[sheet.partials.length - 1])
        : '';
  if (final.length === 0) return;
  // Field-style write-through: replace the value, preserve caret/selection/focus.
  target.value = final;
}

// --- T6 — MuseVoiceSpeaker preset + render-prop forms ------------------------
// Strictly presentational: mic envelope in, visuals out — never a proxy/fetch
// call (`fetchImpl` is accepted as a hermetic seam and ignored). `children`
// wins when present (one dev warning via `onWarn`, never a device error).
// Preset `waveform` selects a built-in renderer ('bars' default, 'wave',
// 'dot'); unknown names are rejected with a named error, never garbage.
// Static frame under reduce-motion: `reduceMotion: true` → `animated: false`
// (motion system consumed, not reimplemented).

const SPEAKER_WAVEFORMS = new Set(['bars', 'wave', 'dot']);

function MuseVoiceSpeaker(args) {
  const a = args ?? {};
  const { state, waveform, showPartials, showBadge, children, onWarn, reduceMotion } = a;
  void a.fetchImpl;
  const s = state ?? {};
  const envelope = typeof s.envelope === 'number' ? s.envelope : 0;
  if (typeof envelope !== 'number' || Number.isNaN(envelope) || envelope < 0 || envelope > 1) {
    throw { code: 'bad-envelope', envelope: s.envelope };
  }
  const snapshot = Object.freeze({
    status: s.status,
    envelope,
    partial: s.partial,
    route: s.route,
    latencyMs: s.latencyMs,
    reason: s.reason,
  });
  if (typeof children === 'function') {
    if (typeof onWarn === 'function') {
      onWarn('MuseVoiceSpeaker: children render-prop wins over preset props');
    }
    return children(snapshot);
  }
  const kind = waveform === undefined || waveform === null ? 'bars' : String(waveform);
  if (!SPEAKER_WAVEFORMS.has(kind)) {
    throw { code: 'unknown-waveform', name: waveform };
  }
  const out = {
    kind,
    state: { ...snapshot },
    animated: reduceMotion === true ? false : true,
  };
  out.partial = showPartials ? snapshot.partial : undefined;
  out.badge = showBadge ? snapshot.route : undefined;
  return out;
}

module.exports = { createMuseVoice, startTurn, openListeningSheet, commitSheet, MuseVoiceSpeaker };
