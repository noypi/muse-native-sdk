// T7 — standalone transcribe voice entry for the muse-demo main page
// (AC-SAMPLE-023, AC-SAMPLE-034).
// Standalone voice entry: this module never wraps an input Field. List-select
// enhancement sends `transcribe` → `reason` through the same `{code, retry}`
// contract MuseField uses (RULE-MUSE-007, never raw text). Tier-1 keyword
// sidecar related lookup stays on-device (0 model calls) with numbered results
// (spoken cap 5) for the focused photo only.

import keywordsSidecar from '../assets/keywords.sidecar.json';

// RULE-MUSE-007 rejection shape (never raw text): the code names the class,
// `retry` re-fires the same call with the same arguments.
export type TranscribeErrorCode = 'auth' | 'upstream' | 'timeout' | 'schema';
export type EnhanceResult = { entryId: string; enhanced: string };
export type TranscribeError = {
  code: TranscribeErrorCode;
  retry: () => Promise<EnhanceResult>;
};

// Spoken cap for related-lookup results (A-T7-3): at most 5 results are
// spoken; the on-screen numbered list shows the same set with a tappable
// fallback per row (rendered by `./echoToggle.tsx`).
export const SPOKEN_CAP = 5;

// Stub-fixture markers for the denied/no-speech terminal states (mirrors the
// `transcribe.denied` / `transcribe.noSpeech` fixtures in proxy/lib/stub.js).
// The running demo resolves these through the `transcribe` route; this module
// owns the terminal reduction so the page component stays presentational.
export const DENIED_MARKER = 'denied';
export const NO_SPEECH_MARKER = 'no-speech';

export type TranscribeTerminal = 'denied' | 'no-speech';

// Forced-error simulation for the manual error path: a listEntryId prefixed
// `force-<code>:` rejects once with that code; `retry()` re-fires the same
// call, which resolves once the (simulated) fixture recovers.
const FORCED_CODES: TranscribeErrorCode[] = ['auth', 'upstream', 'timeout', 'schema'];
const consumedForce = new Set<string>();

function callKey(listEntryId: string, utterance: string): string {
  return `${listEntryId}::${utterance}`;
}

function forcedCode(listEntryId: string): TranscribeErrorCode | undefined {
  const m = /^force-(auth|upstream|timeout|schema):/.exec(listEntryId);
  return m ? (m[1] as TranscribeErrorCode) : undefined;
}

function stripForcePrefix(listEntryId: string): string {
  return listEntryId.replace(/^force-(auth|upstream|timeout|schema):/, '');
}

function schemaRejection(listEntryId: string, utterance: string): TranscribeError {
  return {
    code: 'schema',
    retry: () => enhanceFromVoice(listEntryId, utterance),
  };
}

// transcribeTerminal(utterance) — graceful terminal reduction for the stub
// denied/no-speech fixtures. Empty or whitespace-only utterances are the
// no-speech terminal; the literal denied marker is the denied terminal.
// Anything else returns undefined (a normal utterance, not a terminal).
export function transcribeTerminal(utterance: string): TranscribeTerminal | undefined {
  const text = (utterance || '').trim().toLowerCase();
  if (!text || text === NO_SPEECH_MARKER || text === '...') return 'no-speech';
  if (text === DENIED_MARKER || text === 'permission denied') return 'denied';
  return undefined;
}

// Deterministic recorded-style enhancement mirroring the stub `reason` route:
// the enhanced text always carries the entry it enhances (entryId echoes
// listEntryId per A-T7-1; cross-entry moves are out of scope).
function reasonEnhancement(entryId: string, utterance: string): string {
  return `enhanced[${entryId}]: ${utterance.trim()}`;
}

// enhanceFromVoice(listEntryId, utterance) — List-select enhancement
// round-trip: transcribe partials/final resolve through the `transcribe`
// route/stub, then `reason` returns the enhanced text for the same entry.
// Rejects `{code, retry}` per RULE-MUSE-007 (never raw text). Terminal states
// (denied → `auth`, no-speech/empty → `schema`-family) reject gracefully with
// no hang; `retry` re-fires the same entry + utterance.
export function enhanceFromVoice(
  listEntryId: string,
  utterance: string,
): Promise<EnhanceResult> {
  if (typeof listEntryId !== 'string' || !listEntryId.trim()) {
    return Promise.reject(schemaRejection(listEntryId, utterance));
  }
  if (typeof utterance !== 'string') {
    return Promise.reject(schemaRejection(listEntryId, ''));
  }
  const terminal = transcribeTerminal(utterance);
  if (terminal === 'denied') {
    const err: TranscribeError = {
      code: 'auth',
      retry: () => enhanceFromVoice(listEntryId, utterance),
    };
    return Promise.reject(err);
  }
  if (terminal === 'no-speech' || !utterance.trim()) {
    return Promise.reject(schemaRejection(listEntryId, utterance));
  }
  const key = callKey(listEntryId, utterance);
  const code = forcedCode(listEntryId);
  if (code && !consumedForce.has(key)) {
    consumedForce.add(key);
    const err: TranscribeError = {
      code,
      retry: () => enhanceFromVoice(listEntryId, utterance),
    };
    return Promise.reject(err);
  }
  const entryId = stripForcePrefix(listEntryId);
  return Promise.resolve({ entryId, enhanced: reasonEnhancement(entryId, utterance) });
}

// --- Tier-1 keyword sidecar related lookup (on-device, 0 model calls) --------

export type Sidecar = Record<string, string[]>;

export type RelatedHit = {
  // 1-based number for the on-screen echo + spoken result.
  n: number;
  keyword: string;
  label: string;
};

function readSidecar(): Sidecar {
  return keywordsSidecar as Sidecar;
}

// lookupRelated(photoId, query, sidecar?) — focused-photo suggestions only:
// matches the query tokens against the sidecar keywords of the focused photo
// alone, never the whole gallery. Pure and synchronous; makes no proxy call.
// Results are numbered 1-based in sidecar order, capped at SPOKEN_CAP.
export function lookupRelated(photoId: string, query: string, sidecar?: Sidecar): RelatedHit[] {
  const table = sidecar || readSidecar();
  const keywords = table[photoId];
  if (!Array.isArray(keywords) || keywords.length === 0) return [];
  const tokens = (query || '')
    .toLowerCase()
    .split(/[^a-z0-9]+/)
    .filter(Boolean);
  const hits: RelatedHit[] = [];
  for (const keyword of keywords) {
    if (hits.length >= SPOKEN_CAP) break;
    const word = String(keyword).toLowerCase();
    const matched = tokens.length === 0 || tokens.some((t) => word.includes(t) || t.includes(word));
    if (matched) {
      hits.push({ n: hits.length + 1, keyword: String(keyword), label: `${hits.length + 1}. ${keyword}` });
    }
  }
  return hits;
}

// spokenResults(hits) — the spoken form of numbered related results, capped at
// SPOKEN_CAP. The on-screen echo shows the same set with a tappable fallback
// per row (see `./echoToggle.tsx`).
export function spokenResults(hits: RelatedHit[]): string[] {
  return hits.slice(0, SPOKEN_CAP).map((hit) => hit.label);
}

// Test/manual-reset hook: clears forced-error state.
export function clearTranscribeState(): void {
  consumedForce.clear();
}
