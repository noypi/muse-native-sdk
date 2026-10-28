// T7 — deterministic on-device grammar for muse-demo guided voice
// (AC-SAMPLE-034).
// Pure and synchronous: known Tier-1 utterances resolve to an `{action}`
// without any proxy call; unknown utterances fall through to `reason` as
// content (never executed). Empty utterances fall through as well — callers
// that need the no-speech terminal use `transcribeTerminal` in
// `./transcribe.ts` instead.

// Tier-1 voice actions. Every action keeps a touch equivalent in the demo
// chrome (see `./echoToggle.tsx`); the grammar only names the action, it never
// performs it.
export type Action =
  | { kind: 'toggle-transcribe'; on: boolean }
  | { kind: 'toggle-echo'; on: boolean }
  | { kind: 'select-tab'; tab: 'lens' | 'field' | 'list' }
  | { kind: 'lookup-related'; query: string }
  | { kind: 'enhance-selected'; utterance: string };

export type GrammarResult = { action: Action } | { fallthrough: 'reason' };

const TRANSCRIBE_OFF_RE = /(turn|switch|toggle).*(voice|transcribe|listening)\s+off|^(voice|transcribe)\s+off|mute( voice)?/i;
const TRANSCRIBE_ON_RE = /(turn|switch|toggle).*(voice|transcribe|listening)\s+(back\s+)?on|^(voice|transcribe)\s+on|unmute/i;
const ECHO_OFF_RE = /(turn|switch|toggle).*echo\s+off|^echo\s+off/i;
const ECHO_ON_RE = /(turn|switch|toggle).*echo\s+(back\s+)?on|^echo\s+on/i;
const TAB_RE = /(?:go to|open|show|switch to)\s+(?:the\s+)?(lens|field|list)(?:\s+tab)?/i;
const RELATED_RE = /(?:show|find|look up|related|more like|similar)\s+(?:related\s+)?(?:for\s+|about\s+|like\s+)?(.+)/i;
const ENHANCE_RE = /(?:enhance|improve|update|edit)(?:\s+(?:this|the|selected|entry))?(?:\s+(?:with|to|as)\s+)?(.+)/i;

// resolveGrammar(utterance) — deterministic on-device Tier-1 binding, no proxy
// call. Returns `{action}` for a known command, `{fallthrough: 'reason'}` for
// anything else (including empty utterances). Fallthrough text is treated as
// data for the `reason` route, never executed as a command.
export function resolveGrammar(utterance: string): GrammarResult {
  const text = (utterance || '').trim();
  if (!text) return { fallthrough: 'reason' };

  if (ECHO_OFF_RE.test(text)) return { action: { kind: 'toggle-echo', on: false } };
  if (ECHO_ON_RE.test(text)) return { action: { kind: 'toggle-echo', on: true } };
  if (TRANSCRIBE_OFF_RE.test(text)) return { action: { kind: 'toggle-transcribe', on: false } };
  if (TRANSCRIBE_ON_RE.test(text)) return { action: { kind: 'toggle-transcribe', on: true } };

  const tab = TAB_RE.exec(text);
  if (tab) {
    const name = tab[1].toLowerCase() as 'lens' | 'field' | 'list';
    return { action: { kind: 'select-tab', tab: name } };
  }

  const related = RELATED_RE.exec(text);
  if (related && related[1].trim()) {
    return { action: { kind: 'lookup-related', query: related[1].trim().slice(0, 120) } };
  }

  const enhance = ENHANCE_RE.exec(text);
  if (enhance && enhance[1].trim()) {
    return { action: { kind: 'enhance-selected', utterance: enhance[1].trim().slice(0, 280) } };
  }

  return { fallthrough: 'reason' };
}

// isKnownCommand(utterance) — true when the utterance binds to a Tier-1 action
// on-device (no proxy call needed); false when it falls through to `reason`.
export function isKnownCommand(utterance: string): boolean {
  const result = resolveGrammar(utterance);
  return 'action' in result;
}
