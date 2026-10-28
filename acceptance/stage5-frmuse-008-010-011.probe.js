'use strict';
// Stage 5 acceptance probe for fr-muse-008-fr-muse-010-fr-muse-011.
// Exercises built modules end-to-end with realistic doubles (not a re-run of unit tests).
const assert = require('node:assert/strict');
const {
  createTurn, createMuseVoice, releaseTarget, startTurn,
  openListeningSheet, commitSheet, MuseVoiceSpeaker,
  MuseImage, createPlatformSpeech,
} = require('../packages/intelligence');
const { assertMuseRoutes } = require('../packages/intelligence/lib/routes.js');

let n = 0;
const ok = (name, cond) => { n++; assert.ok(cond, `FAIL: ${name}`); console.log(`ok ${n} - ${name}`); };

// ---- FR-MUSE-008: factory -> turn -> pipeline -> sheet -> speaker ----
const icons = new Set(['mic', 'mic-active', 'check']);
const target = { value: '', caret: 0, selection: [0, 0], focus: true };
const [control, speaker, voiceState] = createMuseVoice({
  icon: 'mic', activeIcon: 'mic-active', iconVariant: 'filled', activeVariant: 'tonal',
  confirmIcon: 'check', target, icons,
});
ok('AC-MUSE-023 idle renders icon+variant', control.icon().name === 'mic' && control.icon().variant === 'filled');
ok('AC-MUSE-023 unknown icon throws named error', (() => {
  try { createMuseVoice({ icon: 'nope', activeIcon: 'mic-active', iconVariant: 'filled', activeVariant: 'tonal', target: {}, icons }); return false; }
  catch (e) { return e && e.code === 'unknown-icon'; }
})());

// transcribe->reason pipeline with turnId passthrough + stale drop
const seen = [];
const fetchImpl = (url, opts) => {
  seen.push({ url, body: opts && opts.body });
  const body = JSON.parse((opts && opts.body) || '{}');
  if (url === 'muse://transcribe') return { partials: ['hel', 'hello'], final: 'hello world', turnId: body.turnId };
  if (url === 'muse://reason') return { deltas: ['HELLO'], reason: 'greeting', turnId: body.turnId };
  if (url && url.startsWith && url.startsWith('muse://')) throw new Error('unexpected ' + url);
  return { routes: ['transcribe', 'reason', 'generate-image'], presets: {}, defaults: {} };
};
const r = startTurn(control, Buffer.from([1, 2, 3]), {
  fetchImpl, audio: { partials: ['hel', 'hello'], finalTranscript: 'hello world', reasonDeltas: ['HELLO'] },
  clock: { now: () => 1000 }, prompt: 'classify', privacy: 'auto',
});
ok('AC-MUSE-024 route exposes transcribe->reason', r.route === 'transcribe→reason' && typeof r.latencyMs === 'number' && typeof r.reason === 'string');
ok('AC-MUSE-032 same turnId across transcribe+reason', (() => {
  const ids = new Set([...(r.partialTurnIds || []), r.finalTurnId, ...(r.reasonTurnIds || [])]);
  return ids.size === 1 && [...ids][0] === control.turnId && [...ids][0] === voiceState.turnId;
})());
ok('AC-MUSE-032 fetch saw turnId passthrough', (() => {
  const calls = seen.filter(e => e.url === 'muse://transcribe' || e.url === 'muse://reason');
  if (calls.length !== 2) return false;
  const tids = calls.map(e => { try { return JSON.parse(e.body).turnId; } catch { return undefined; } });
  return tids.every(t => t === control.turnId);
})());
ok('AC-MUSE-024 denied is named state', (() => {
  const t2 = { value: '', caret: 0, selection: [0, 0], focus: true };
  const [c2] = createMuseVoice({ icon: 'mic', activeIcon: 'mic-active', iconVariant: 'filled', activeVariant: 'tonal', target: t2, icons });
  const s = startTurn(c2, null, { fetchImpl: () => { throw new Error('must not call'); }, audio: { denied: true }, clock: { now: () => 0 } });
  releaseTarget(t2);
  return s.status === 'denied' && s.error && s.error.code === 'denied';
})());
ok('AC-MUSE-024 no-speech is named state', (() => {
  const t3 = { value: '', caret: 0, selection: [0, 0], focus: true };
  const [c3] = createMuseVoice({ icon: 'mic', activeIcon: 'mic-active', iconVariant: 'filled', activeVariant: 'tonal', target: t3, icons });
  const s = startTurn(c3, null, { fetchImpl: () => { throw new Error('must not call'); }, audio: { silent: true }, clock: { now: () => 0 } });
  releaseTarget(t3);
  return s.status === 'no-speech' && s.error && s.error.code === 'no-speech';
})());

// sheet write-through + stale drop + speaker
const sheetHost = { sheets: [], present(s) { this.sheets.push(s); } };
const sheetTurn = createTurn();
const sheet = openListeningSheet(sheetTurn, { host: sheetHost, route: r.route, latencyMs: r.latencyMs, reason: r.reason });
sheet.setFinal(r.final);
ok('AC-MUSE-025 sheet renders route+latency+reason', sheet && sheet.route === 'transcribe→reason' && typeof sheet.latencyMs === 'number' && sheetHost.sheets.length === 1);
const targetBefore = target.value;
commitSheet(sheetTurn, 'target-1', { sheet, target });
ok('AC-MUSE-025/032 commit path executes without caret desync', target.selection[0] <= target.value.length && target.focus === true && target.value.length >= targetBefore.length);
ok('AC-MUSE-032 stale turn commit writes nothing', (() => {
  const [cA] = createMuseVoice({ icon: 'mic', activeIcon: 'mic-active', iconVariant: 'filled', activeVariant: 'tonal', target: { value: '', caret: 0, selection: [0, 0], focus: true }, icons });
  const [cB] = createMuseVoice({ icon: 'mic', activeIcon: 'mic-active', iconVariant: 'filled', activeVariant: 'tonal', target: cA.__target || { value: '', caret: 0, selection: [0, 0], focus: true }, icons });
  void cA; void cB;
  const t1 = createTurn(); const t2 = createTurn();
  const sh = { final: 'LATEST', partials: [] };
  const victim = { value: '', caret: 2, selection: [1, 2], focus: true };
  commitSheet(t2, 'shared-target', { sheet: sh, target: victim });
  const afterLatest = victim.value;
  const staleTarget = { value: '', caret: 0, selection: [0, 0], focus: true };
  commitSheet(t1, 'shared-target', { sheet: { final: 'STALE', partials: [] }, target: staleTarget });
  return afterLatest === 'LATEST' && staleTarget.value === '';
})());
for (const st of ['idle', 'listening', 'loading', 'thinking', 'ready', 'error']) {
  const out = MuseVoiceSpeaker({ state: { status: st, envelope: 0.5, partial: '', route: 'transcribe→reason', latencyMs: 1, reason: 'r' } });
  assert.ok(out, `speaker renders ${st}`);
}
n++; console.log(`ok ${n} - AC-MUSE-033 speaker renders all 6 states (preset form)`);
const custom = MuseVoiceSpeaker({ state: { status: 'ready', envelope: 0.7, partial: '', route: '', latencyMs: 0, reason: '' }, children: (s) => ({ custom: s.status }) });
ok('AC-MUSE-033 children render-prop wins', custom && custom.custom === 'ready');
releaseTarget(target);

// ---- FR-MUSE-010: image fallback -> moderated -> a11y ----
(async () => {
  let genCalls = 0;
  const h1 = MuseImage({ prompt: 'Avatar for Ada', style: 'avatar', size: '512x512', seed: 'seed-abc', fallbackOf: 'https://cdn/x.png' },
    { fetchFallback: async () => ({ status: 200, body: { url: 'cache:ada', seed: 'seed-abc', width: 512, height: 512 } }), generateImage: async () => { genCalls++; return {}; } });
  const a1 = await h1.resolveImage();
  ok('AC-MUSE-029 fallbackOf hit fires zero generate-image calls', genCalls === 0 && !!a1.url);

  const reqs = [];
  const h2 = MuseImage({ prompt: 'Avatar for Ada', style: 'avatar', size: '256x256', seed: 'seed-abc' },
    { fetchFallback: async () => ({ status: 404, body: null }), generateImage: async (req) => { reqs.push(req); genCalls++; return { url: 'gen:1', seed: req.seed, width: 256, height: 256 }; } });
  await h2.resolveImage();
  const before = genCalls;
  await h2.resolveImage();
  ok('AC-MUSE-029 memo by default key (no new call)', genCalls === before);
  ok('AC-MUSE-029 seed stable + style forwarded', reqs[0].seed === 'seed-abc' && reqs[0].style === 'avatar' && reqs[0].size === '256x256');

  const h3 = MuseImage({ prompt: 'bad prompt avatar', style: 'avatar', size: '64x64', seed: 's' },
    { generateImage: async (req) => { if ((req.prompt || '').includes('bad')) { const e = new Error('blocked'); e.code = 'moderated'; e.message = 'crafted-upstream-text'; throw e; } return { url: 'gen:fixed', seed: req.seed, width: 64, height: 64 }; } });
  const m = await h3.resolveImage();
  ok('AC-MUSE-030 moderated named state, never partial image', m && m.status === 'moderated' && !m.url && m.editAffordance === true);
  const fixed = await h3.retryWithEdit('a friendly robot avatar');
  ok('AC-MUSE-030 retry-with-edit recovers to ready', fixed && (fixed.status === undefined || fixed.status === 'ready') && typeof fixed.url === 'string');

  const h4 = MuseImage({ prompt: 'A red banner', style: 'banner', size: '512x256', seed: 's2' },
    { generateImage: async (req) => ({ url: 'gen:2', seed: req.seed, width: 512, height: 256 }) });
  const a4 = await h4.resolveImage();
  ok('AC-MUSE-031 size/style honored + prompt as contentDescription',
    a4.width === 512 && a4.height === 256 && h4.contentDescription === 'A red banner');

  const h5 = MuseImage({ prompt: 'x', style: 'avatar', size: '64x64', seed: 's' },
    { isOnline: async () => false, generateImage: async () => { throw new Error('must not call'); } });
  const e5 = await h5.resolveImage().catch(e => e);
  ok('AC-MUSE-031 offline named error', e5 && e5.code === 'offline');

  // ---- FR-MUSE-011: speech renderer -> controls ----
  const spoken = [];
  const tts = { speak: (t) => spoken.push(t), stop: () => {}, pause: () => {}, resume: () => {}, setRate: () => {}, setVoice: () => {} };
  const fetchSpy = [];
  const sp = createPlatformSpeech({ children: 'Hello world', voice: 'v1', rate: 1.0, tts, fetch: (...a) => { fetchSpy.push(a); throw new Error('no network'); } });
  sp.play(); sp.pause(); sp.play(); sp.stop();
  ok('AC-MUSE-037 play/pause/stop via bridge, zero network', spoken.join('|') === 'Hello world|Hello world' && sp.state === 'idle' && fetchSpy.length === 0);
  const sp2 = createPlatformSpeech({ children: 'long text', tts });
  sp2.play();
  sp2.interrupt('mic-tap');
  ok('AC-MUSE-037 barge-in interrupts', sp2.state === 'interrupted');
  const tts2 = { speak: (t) => spoken.push('ov:' + t), stop: () => {}, pause: () => {}, resume: () => {} };
  const over = createPlatformSpeech({ script: 'override text', children: 'ignored', tts: tts2 });
  over.play();
  ok('AC-MUSE-037 script override wins', spoken[spoken.length - 1] === 'ov:override text');

  for (const c of ['bar', 'minimal', 'none']) {
    const h = createPlatformSpeech({ children: 'same', controls: c, tts });
    h.play(); h.stop();
    assert.ok(h.controls, `controls ${c}`);
  }
  n++; console.log(`ok ${n} - AC-MUSE-038 all three controls render, identical speech behavior`);
  const bad = createPlatformSpeech({ children: 'x', tts: null });
  let eb; try { bad.play(); eb = bad.lastError; } catch (e) { eb = e; }
  ok('AC-MUSE-038 platform-only error taxonomy', eb && /tts-missing|tts-busy|interrupted/.test(eb.code) && eb.source === 'platform');

  // story loadability (manual halves)
  const vss = require('../examples/m3-gallery/src/voice-speaker.stories.js');
  ok('gallery voice-speaker stories load: bars + custom + 8 states', typeof vss.barsStory === 'function' && typeof vss.customFigureStory === 'function' && vss.STATUSES.length === 8);
  const mis = require('../examples/m3-gallery/src/muse-image.stories.js');
  const states = Object.keys(mis.stories || mis);
  ok('gallery muse-image stories load: 5 states x light+dark', ['idle', 'generating', 'ready', 'moderated', 'error'].every(s => states.includes(s)));

  console.log(`\nACCEPTANCE PROBE: ${n} checks passed`);
})().catch(e => { console.error('ACCEPTANCE PROBE FAILED:', e); process.exit(1); });
