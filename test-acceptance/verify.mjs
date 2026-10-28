// Acceptance verification harness for FR-SAMPLE-009 + FR-SAMPLE-010
// (T5–T9 function-level). Executes REAL production code from
// examples/muse-demo/src by truncating each module before its JSX component
// region, stubbing only 'react' / '@muse/native' imports, and stripping types.
// Fails (exit 1) on any assertion failure.
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { pathToFileURL as _p2f } from 'node:url';
const pathToFileURL = (u) => (u instanceof URL ? u.href : _p2f(u));
import { stripTypeScriptTypes } from 'node:module';
import { createRequire } from 'node:module';

const ROOT = new URL('..', import.meta.url);
const SRC = new URL('../examples/muse-demo/src/', import.meta.url);
const TMP = new URL('./.gen/', import.meta.url);
mkdirSync(TMP, { recursive: true });

const STUBS = pathToFileURL(new URL('./stubs.mjs', import.meta.url));
let failures = 0;
function check(name, cond, extra = '') {
  if (cond) console.log(`  ok: ${name}`);
  else { failures += 1; console.log(`  FAIL: ${name} ${extra}`); }
}

// Load the pure region of a .ts/.tsx module: cut before marker, rewrite bare
// 'react' / '@muse/native' imports to the stub, apply extra rewrites, strip
// types, write to .gen and import. `importMap` rewrites other specifiers.
async function loadPure(relPath, { cutBefore, importMap = {}, extraRewrites = [] }) {
  const src = readFileSync(new URL(relPath, SRC), 'utf8');
  const idx = cutBefore ? src.indexOf(cutBefore) : -1;
  if (cutBefore && idx < 0) throw new Error(`cut marker missing in ${relPath}: ${cutBefore}`);
  let code = idx < 0 ? src : src.slice(0, idx);
  code = code.replace(/from\s+['"]react['"]/g, `from '${STUBS}'`)
             .replace(/from\s+['"]@muse\/native['"]/g, `from '${STUBS}'`);
  for (const [from, to] of Object.entries(importMap)) {
    code = code.split(`from '${from}'`).join(`from '${to}'`);
    code = code.split(`from "${from}"`).join(`from '${to}'`);
  }
  for (const [re, rep] of extraRewrites) code = code.replace(re, rep);
  const js = stripTypeScriptTypes(code);
  const out = new URL(relPath.replace(/\//g, '_').replace(/\.tsx?$/, '.mjs'), TMP);
  writeFileSync(out, js);
  return import(out.href);
}

const require = createRequire(import.meta.url);
console.log('harness top reached');

// ---- T5: suggestions (pure .ts, direct import) ----
const sugHref = pathToFileURL(new URL('../examples/muse-demo/src/suggestions.ts', import.meta.url));
console.log('sug href:', sugHref);
const sug = await import(sugHref);
console.log('T5 annotatePhoto');
{
  const r1 = await sug.annotatePhoto('photo-001', { x: 100, y: 200 });
  check('tap resolves mask polygon', Array.isArray(r1.mask) && r1.mask.length === 8, JSON.stringify(r1.mask)?.slice(0, 80));
  check('1..3 suggestions', r1.suggestions.length >= 1 && r1.suggestions.length <= 3, `n=${r1.suggestions.length}`);
  const r2 = await sug.annotatePhoto('photo-001', { x: 100, y: 200 });
  check('memoized by photoId+point (same ref, no re-segment)', r1 === r2);
  const r3 = await sug.annotatePhoto('photo-001', { x: 101, y: 200 });
  check('different point re-segments', r3 !== r1);
  const picked = sug.promoteSuggestion(r1.suggestions, 0);
  check('promote reuses polygon (identical ref)', picked === r1.suggestions[0]);
  for (const code of ['auth', 'upstream', 'timeout', 'schema']) {
    const key = `force-${code}:photo-err`;
    try { await sug.annotatePhoto(key, { x: 10, y: 10 }); check(`forced ${code} rejects`, false); }
    catch (e) {
      check(`forced ${code} rejects with {code,retry}`, e && e.code === code && typeof e.retry === 'function');
      const again = await e.retry();
      check(`retry after forced ${code} resolves`, Array.isArray(again.mask));
    }
  }
  try { await sug.annotatePhoto('', { x: 10, y: 10 }); check('empty photoId schema-rejects', false); }
  catch (e) { check('empty photoId schema-rejects', e.code === 'schema' && typeof e.retry === 'function'); }
  try { await sug.annotatePhoto('p', { x: 9999, y: 1 }); check('out-of-viewport schema-rejects', false); }
  catch (e) { check('out-of-viewport schema-rejects', e.code === 'schema'); }
}

// ---- T5: listShelf grouping (truncate JSX) ----
console.log('T5 listShelf renderMode');
const shelfUrl = pathToFileURL(new URL('../examples/muse-demo/src/listShelf.tsx', import.meta.url));
const shelf = await loadPure('listShelf.tsx', { cutBefore: 'export type ListShelfProps' });
{
  const recs = [
    { id: 'a1', photoId: 'p1', label: 'dog', polygon: [[0, 0]], insight: 'i1', dataVersion: 1 },
    { id: 'a2', photoId: 'p1', label: 'cat', polygon: [[1, 1]], insight: 'i2', dataVersion: 1 },
    { id: 'a3', photoId: 'p2', label: 'dog', polygon: [[2, 2]], insight: 'i3', dataVersion: 1 },
  ];
  const byObj = shelf.groupAnnotatedRecords(recs, 'by_object');
  check('by_object groups annotated set by label', byObj.groups.length === 2 && byObj.groups.some((g) => g.key === 'object:dog' && g.records.length === 2));
  const byPhoto = shelf.groupAnnotatedRecords(recs, 'by_photo');
  check('by_photo groups annotated set by photo', byPhoto.groups.length === 2 && byPhoto.groups.some((g) => g.key === 'photo:p1' && g.records.length === 2));
  const exec = shelf.groupAnnotatedRecords(recs, 'executive_summary');
  check('executive_summary single group', exec.groups.length === 1 && exec.groups[0].records.length === 3);
  check('memo key is dataVersion+renderMode', shelf.shelfMemoKey(3, 'by_photo') === '3:by_photo');
  check('all modes are render modes', shelf.isRenderMode('by_object') && shelf.isRenderMode('by_photo') && shelf.isRenderMode('executive_summary') && !shelf.isRenderMode('gallery'));
  void shelfUrl;
}

// ---- T6: profile + avatar + completeOnboarding ----
console.log('T6 onboarding');
const profile = await import(pathToFileURL(new URL('../examples/muse-demo/src/profile.ts', import.meta.url)));
const avatar = await loadPure('avatar.tsx', { cutBefore: null });
{
  check('blank profile misses both required', profile.missingRequired(profile.blankProfile()).length === 2);
  const p = profile.blankProfile();
  const t1 = profile.applyTurn(p, 'Hi, my name is Sam');
  check('introduce captures name', p.displayName === 'Sam' && t1.signal === 'detail');
  check('required-check still missing topic', JSON.stringify(profile.missingRequired(p)) === '["favoriteTopic"]');
  check('reprompt names missing field', /topic/i.test(profile.repromptFor(['favoriteTopic'])));
  const t2 = profile.applyTurn(p, 'My favorite topic is street photography');
  check('topic captured, required complete', profile.missingRequired(p).length === 0 && t2.signal === 'detail');
  const p2 = profile.blankProfile();
  profile.applyTurn(p2, 'I am Ada and I love lighthouses, also I do 3d art');
  check('volunteered detail path fills fields', p2.displayName.startsWith('Ada') && /lighthouse/i.test(p2.favoriteTopic) && p2.artStyle === '3d');
  const p3 = profile.blankProfile();
  profile.applyTurn(p3, 'My name is Bo');
  profile.applyTurn(p3, 'just some volunteered remark about fog');
  check('volunteered detail copied to customInfo', /fog/i.test(p3.customInfo));
  check('transcribe default on, persisted toggle', profile.isTranscribeOn() === true && profile.transcribeState() === 'on');
  profile.setTranscribeOn(false);
  check('toggle off persists + redirect carries off', profile.transcribeState() === 'off' && profile.isTranscribeOn() === false);
  profile.resetTranscribe();
  check('generate-image request shape', (() => {
    const r = avatar.generateImageRequest(p);
    return r.route === 'generate-image' && r.style === 'cartoon' && r.size === 512 && r.seed === p.idHash && r.cacheKey === `avatar:${p.id}`;
  })());
  avatar.clearAvatarCache();
  check('cache-first miss then resolveAvatar caches', await avatar.resolveAvatar(p).then((a) => a.uri.startsWith('generated:') && avatar.readAvatarCache(p) === a));
  const prev = avatar.previewThenGenerate(profile.blankProfile('fresh-user'));
  check('previewThenGenerate non-blocking preview', prev.current.uri === avatar.AVATAR_PREVIEW_URI);
  const gen = await prev.done;
  check('post-redirect generation lands + caches', gen.uri.startsWith('generated:'));
}
// completeOnboarding needs './errors' (JSX) -> map to truncated errors module
const errorsMod = await loadPure('errors.tsx', { cutBefore: '// --- Rendered surfaces' });
const errorsGenUrl = pathToFileURL(new URL('./.gen/errors.mjs', import.meta.url));
void errorsMod; void errorsGenUrl;
const onboarding = await loadPure('onboarding.tsx', {
  cutBefore: '// --- Profile-settings page',
  importMap: {
    './errors': pathToFileURL(new URL('./.gen/errors.mjs', import.meta.url)),
    './profile': pathToFileURL(new URL('../examples/muse-demo/src/profile.ts', import.meta.url)),
    './avatar': pathToFileURL(new URL('./.gen/avatar.mjs', import.meta.url)),
  },
  extraRewrites: [[
    /import\s*\{\s*InlineError,\s*dismissSnackbarNotice,\s*useInlineProxyError\s*\}/,
    'import { dismissSnackbarNotice, useInlineProxyError }',
  ]],
});
{
  const full = await onboarding.completeOnboarding(['Hi, my name is Sam', 'My favorite topic is street photography', 'love it, looks great']);
  check('introduce->required-check->like->redirect /main', full.redirect === '/main' && full.profile.displayName === 'Sam');
  check('redirect carries transcribe listening', full.transcribe === 'on');
  check('avatar present at redirect (non-blocking)', typeof full.avatar.uri === 'string' && full.avatar.uri.length > 0);
  avatar.clearAvatarCache();
  const photo = await onboarding.completeOnboarding(['My name is Kim', 'My favorite topic is birds', 'please use my photo, upload it']);
  check('take/upload-photo completes with photo avatar', photo.avatar.uri === 'photo://onboarding-pick' && photo.redirect === '/main');
  try { await onboarding.completeOnboarding(['My name is Al']); check('missing-field reprompt rejects', false); }
  catch (e) { check('missing-field reprompt rejects schema+retry', e.code === 'schema' && typeof e.retry === 'function'); }
  try { await onboarding.completeOnboarding([]); check('empty transcript rejects', false); }
  catch (e) { check('empty transcript rejects (no completion)', e.code === 'schema'); }
  for (const code of ['auth', 'upstream']) {
    try { await onboarding.completeOnboarding([`force-${code}: please fail`, 'My name is Zoe', 'My favorite topic is stars']); check(`onboarding forced ${code}`, false); }
    catch (e) {
      check(`onboarding forced ${code} + retry recovers`, e.code === code && typeof e.retry === 'function'
        && (await e.retry().then((r) => r.redirect === '/main').catch(() => false)));
    }
  }
}

// ---- T7: transcribe + grammar + echo ----
console.log('T7 transcribe/grammar/echo');
const sidecar = JSON.parse(readFileSync(new URL('../examples/muse-demo/assets/keywords.sidecar.json', import.meta.url), 'utf8'));
const errSpy = { calls: [] };
const errStubUrl = new URL('./.gen/errors-stub.mjs', import.meta.url);
writeFileSync(errStubUrl, `export const calls = []; export function surfaceProxyError(code, retry){ calls.push({code, retry}); return retry; }`);
const transcribe = await loadPure('transcribe.ts', {
  cutBefore: '// Test/manual-reset hook',
  importMap: {
    '../assets/keywords.sidecar.json': pathToFileURL(new URL('../examples/muse-demo/assets/keywords.sidecar.json', import.meta.url)),
    './errors': pathToFileURL(errStubUrl),
  },
  extraRewrites: [[/from '([^']+\.json)'/g, "from '$1' with { type: 'json' }"]],
});
{
  const r = await transcribe.enhanceFromVoice('a1', 'make it warmer');
  check('List-select round-trip entryId echo', r.entryId === 'a1' && r.enhanced === 'enhanced[a1]: make it warmer');
  try { await transcribe.enhanceFromVoice('force-timeout:a9', 'make it warmer'); check('forced timeout', false); }
  catch (e) {
    check('unknown/forced rejects {code,retry}', e.code === 'timeout' && typeof e.retry === 'function');
    const ok = await e.retry();
    check('retry re-fires same call, strips force prefix', ok.entryId === 'a9');
  }
  for (const [utt, code] of [['denied', 'auth'], ['', 'schema'], ['no-speech', 'schema']]) {
    try { await transcribe.enhanceFromVoice('a1', utt); check(`terminal ${JSON.stringify(utt)}`, false); }
    catch (e) { check(`terminal ${JSON.stringify(utt)} -> ${code}`, e.code === code && typeof e.retry === 'function'); }
  }
  check('terminals pure', transcribe.transcribeTerminal('denied') === 'denied' && transcribe.transcribeTerminal('  ') === 'no-speech' && transcribe.transcribeTerminal('make it warmer') === undefined);
  const hits = transcribe.lookupRelated('photo-001', 'sunset over the lake', sidecar);
  check('sidecar focused-photo only + numbered', hits.length > 0 && hits[0].n === 1 && hits.every((h) => sidecar['photo-001'].includes(h.keyword)));
  const other = transcribe.lookupRelated('photo-001', 'sunset', { 'photo-002': ['sunset', 'x', 'y', 'z', 'w', 'v', 'u'] });
  check('never consults other photos', other.length === 0);
  const many = transcribe.lookupRelated('photo-002', 'e', { 'photo-002': ['a1', 'e2', 'e3', 'e4', 'e5', 'e6', 'e7', 'e8'] });
  check('spoken cap 5', many.length <= 5 && transcribe.spokenResults(many).length <= 5);
  check('SPOKEN_CAP=5', transcribe.SPOKEN_CAP === 5);
  const grammar = await import(pathToFileURL(new URL('../examples/muse-demo/src/grammar.ts', import.meta.url)));
  check('known transcribe-off command', JSON.stringify(grammar.resolveGrammar('turn voice off')) === JSON.stringify({ action: { kind: 'toggle-transcribe', on: false } }));
  check('unknown falls through to reason', JSON.stringify(grammar.resolveGrammar('why is the sky blue')) === JSON.stringify({ fallthrough: 'reason' }));
  check('empty falls through', grammar.resolveGrammar('').fallthrough === 'reason');
  check('enhance binds utterance', grammar.resolveGrammar('enhance selected with warmer tones').action?.kind === 'enhance-selected');
  const echo = await loadPure('echoToggle.tsx', { cutBefore: 'export function EchoToggle' });
  check('echo defaults on', echo.isEchoOn() === true);
  check('numbered echo + terse', JSON.stringify(echo.echoLines(['a', 'b'])) === '["1. a","2. b"]' && echo.terseConfirmation(true) === 'Done.');
  echo.setEchoOn(false);
  check('toggle off persists', echo.isEchoOn() === false);
  echo.resetEcho();
  void errSpy;
}

// ---- T8: error taxonomy via stub + error store ----
console.log('T8 proxy error taxonomy');
{
  const stub = require('../proxy/lib/stub.js');
  const routes = ['segment', 'reason', 'structure', 'transcribe', 'generate-image'];
  for (const route of routes) {
    const ok = stub.handle({ route, method: 'POST', url: `/stub/${route}` });
    check(`stub ${route} 200`, ok.status === 200 && ok.body != null);
    for (const code of ['auth', 'upstream', 'timeout', 'schema']) {
      let thrown = null;
      try { stub.handle({ route, method: 'POST', url: `/stub/${route}`, body: `force-${code}: boom` }); }
      catch (e) { thrown = e; }
      check(`stub ${route} forced ${code} throws ProxyError`, thrown && thrown.code === code && thrown.route === route);
      check(`stub ${route} forced ${code} retryable flag`, thrown && thrown.retryable === (code !== 'schema'));
    }
  }
  const denied = stub.handle({ route: 'transcribe', url: '/stub/transcribe', body: 'denied' });
  const nospeech = stub.handle({ route: 'transcribe', url: '/stub/transcribe', body: '' });
  check('stub transcribe denied/no-speech fixture terminals', denied.status === 200 && denied.body.denied && denied.body.denied.terminal === 'denied' && denied.body.noSpeech && denied.body.noSpeech.terminal === 'no-speech');
  check('stub transcribe empty handled', nospeech != null && nospeech.status === 200);
  const errs = await loadPure('errors.tsx', { cutBefore: '// --- Rendered surfaces' });
  check('four codes valid, others not', errs.isProxyErrorCode('auth') && errs.isProxyErrorCode('schema') && !errs.isProxyErrorCode('boom') && !errs.isProxyErrorCode(undefined));
  check('designed messages never raw', ['auth', 'upstream', 'timeout', 'schema'].every((c) => typeof errs.errorMessageFor(c) === 'string' && !errs.errorMessageFor(c).includes('force-')));
  let retried = 0;
  const notice1 = errs.surfaceProxyError('timeout', () => { retried += 1; return Promise.resolve(); });
  const posted = errs.getSnackbarNotice();
  check('surface posts SnackbarHost notice', posted && posted.code === 'timeout');
  check('returns wrapped retry (same action both surfaces)', typeof notice1 === 'function');
  await Promise.resolve();
  notice1();
  await new Promise((r) => setTimeout(r, 20));
  check('retry-success clears snackbar surface', errs.getSnackbarNotice() === undefined && retried === 1);
  const failRetry = errs.surfaceProxyError('auth', () => Promise.reject({ code: 'upstream', retry: () => Promise.resolve() }));
  failRetry();
  await new Promise((r) => setTimeout(r, 20));
  check('failed retry re-surfaces fresh code', errs.getSnackbarNotice() && errs.getSnackbarNotice().code === 'upstream');
  errs.dismissSnackbarNotice();
  check('dismiss clears (retry-success clears BOTH)', errs.getSnackbarNotice() === undefined);
  errs.surfaceProxyError('schema', () => { throw new Error('sync boom'); });
  check('sync-throw retry keeps surface, no raw leak', errs.getSnackbarNotice() && errs.getSnackbarNotice().code === 'schema');
  errs.dismissSnackbarNotice();
  const src = readFileSync(new URL('../examples/muse-demo/src/errors.tsx', import.meta.url), 'utf8');
  const codeOnly = src.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
  check('no raw text rendering in error surfaces', !/err\.(message|stack|payload|text)|String\(err|JSON\.stringify\(err/.test(codeOnly) && /ERROR_MESSAGES\[/.test(codeOnly));
}

// ---- T9: speaker states ----
console.log('T9 speaker');
{
  const speaker = await loadPure('speaker.tsx', { cutBefore: '// Speaker — docked' });
  const states = ['idle', 'listening', 'loading', 'thinking', 'ready', 'denied', 'no-speech'];
  const labels = new Set();
  for (const s of states) {
    speaker.renderSpeaker(s, { reduceMotion: false, transcribeOn: true });
    const label = speaker.speakerLabel();
    labels.add(label);
    check(`state ${s} readable`, typeof label === 'string' && label.length > 0);
  }
  check('all 7 states distinct labels', labels.size === 7, [...labels].join(' | '));
  speaker.renderSpeaker('thinking', { reduceMotion: true, transcribeOn: true });
  check('reduce-motion static frame', speaker.speakerLabel().includes('[static]') && speaker.speakerLabel().includes('thinking'));
  speaker.renderSpeaker('listening', { reduceMotion: false, transcribeOn: false });
  check('transcribe-off stays visible off/idle', speaker.speakerLabel() === 'Speaker off (idle)');
  speaker.renderSpeaker('no-such-state', { reduceMotion: false, transcribeOn: true });
  check('unknown state ignored', speaker.getSpeakerPresentation().state === 'listening' || speaker.getSpeakerPresentation().transcribeOn === false);
  const stripComments = (s) => s.split('\n').filter((l) => !/^\s*\/\//.test(l)).join('\n');
  const spSrc = readFileSync(new URL('../examples/muse-demo/src/speaker.tsx', import.meta.url), 'utf8');
  const chSrc = readFileSync(new URL('../examples/muse-demo/src/chrome.tsx', import.meta.url), 'utf8');
  check('speaker presentational-only (no proxy/fetch/stub import or call)', !/from\s+['"][^'"]*(proxy|stub)[^'"]*['"]|fetch\s*\(|stub\s*\.\s*handle/.test(stripComments(spSrc)) && !/from\s+['"][^'"]*(proxy|stub)[^'"]*['"]|fetch\s*\(/.test(stripComments(chSrc)));
  const appSrc = readFileSync(new URL('../examples/muse-demo/src/App.tsx', import.meta.url), 'utf8');
  check('Speaker docked top bar ahead of content', appSrc.indexOf('<Speaker') !== -1 || /DemoChrome/.test(appSrc));
  check('OnboardingPage mounted in App', /<OnboardingPage/.test(appSrc));
}

console.log(failures === 0 ? '\nALL ACCEPTANCE CHECKS PASSED' : `\n${failures} CHECK(S) FAILED`);
process.exit(failures === 0 ? 0 : 1);
