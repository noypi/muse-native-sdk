// T6 — profile model for muse-demo onboarding (AC-SAMPLE-023).
// Pure logic: required-field check, reprompt loop helpers, and the global
// persisted transcribe toggle. The `reason`-route binding mirrors the stub
// `reason.onboarding` fixture (see proxy/lib/stub.js FIXTURES); the running
// demo resolves turns through that route, this module owns the transcript →
// profile reduction so the page component stays presentational.

export type ArtStyle = '3d' | 'cartoon';

export type TranscribeState = 'on' | 'off';

export type Profile = {
  id: string;
  idHash: number;
  displayName: string;
  favoriteTopic: string;
  artStyle: ArtStyle;
  customInfo: string;
  description: string;
};

// Required fields for completion: introduce → required-check → reprompt loops
// until both are non-empty. Everything else volunteered lands in customInfo.
export const REQUIRED_FIELDS = ['displayName', 'favoriteTopic'] as const;
export type RequiredField = (typeof REQUIRED_FIELDS)[number];

export const DEFAULT_PROFILE_ID = 'local-user';

export function hashId(id: string): number {
  let h = 2166136261;
  for (let i = 0; i < id.length; i += 1) {
    h ^= id.charCodeAt(i);
    h = Math.imul(h, 16777619);
  }
  return h >>> 0;
}

export function blankProfile(id: string = DEFAULT_PROFILE_ID): Profile {
  const idHash = hashId(id);
  return {
    id,
    idHash,
    displayName: '',
    favoriteTopic: '',
    artStyle: 'cartoon',
    customInfo: '',
    description: '',
  };
}

export function missingRequired(profile: Profile): RequiredField[] {
  const missing: RequiredField[] = [];
  if (!profile.displayName.trim()) missing.push('displayName');
  if (!profile.favoriteTopic.trim()) missing.push('favoriteTopic');
  return missing;
}

export function repromptFor(missing: RequiredField[]): string {
  if (missing.includes('displayName') && missing.includes('favoriteTopic')) {
    return 'What should I call you, and what is your favorite topic?';
  }
  if (missing.includes('displayName')) return 'What should I call you?';
  return 'What is your favorite topic?';
}

// --- Global persisted transcribe toggle ------------------------------------
// Listening by default. Icon-off reveals the manual form fallback and keeps
// transcribe off everywhere until re-enabled; the redirect carries this state.
let transcribeOn = true;

export function isTranscribeOn(): boolean {
  return transcribeOn;
}

export function transcribeState(): TranscribeState {
  return transcribeOn ? 'on' : 'off';
}

export function setTranscribeOn(next: boolean): void {
  transcribeOn = next;
}

export function resetTranscribe(): void {
  transcribeOn = true;
}

const TRANSCRIBE_OFF_RE = /(turn|switch|toggle).*(voice|transcribe|listening)\s+off|^(voice|transcribe)\s+off|mute( voice)?/i;
const TRANSCRIBE_ON_RE = /(turn|switch|toggle).*(voice|transcribe|listening)\s+(back\s+)?on|^(voice|transcribe)\s+on|unmute/i;

export function applyTranscribeToggle(utterance: string): boolean {
  if (TRANSCRIBE_ON_RE.test(utterance)) {
    transcribeOn = true;
    return true;
  }
  if (TRANSCRIBE_OFF_RE.test(utterance)) {
    transcribeOn = false;
    return true;
  }
  return false;
}

// --- Transcript → profile reduction (mirrors reason.onboarding turns) --------

const NAME_RE = /(?:my name is|call me|i'm|i am)\s+([a-zA-Z][a-zA-Z' .-]*)/i;
const TOPIC_RE = /(?:favorite|favourite)\s+(?:topic|subject|thing)\s+is\s+([^.;]+)|i\s+(?:love|like|enjoy)\s+([^.;]+)/i;
const ART_RE = /\b(3d|three-?d|cartoon)\b/i;
const PHOTO_RE = /\b(use (my|this|that) photo|upload|take (a )?picture|take (a )?photo|use photo)\b/i;
const LIKE_RE = /\b(like it|love it|keep it|looks (good|great)|perfect)\b/i;

function cleanCapture(raw: string): string {
  return raw.trim().replace(/\s+/g, ' ').slice(0, 120);
}

// Applies one transcript turn to the profile. Returns true when the turn was
// consumed as a toggle/photo/like signal rather than profile detail.
export function applyTurn(profile: Profile, utterance: string): { signal: 'toggle' | 'photo' | 'like' | 'detail' } {
  const text = utterance.trim();
  if (!text) return { signal: 'detail' };
  if (applyTranscribeToggle(text)) return { signal: 'toggle' };
  if (PHOTO_RE.test(text)) return { signal: 'photo' };
  if (LIKE_RE.test(text)) return { signal: 'like' };

  let detailed = false;
  const name = NAME_RE.exec(text);
  if (name && !profile.displayName) {
    profile.displayName = cleanCapture(name[1]);
    detailed = true;
  }
  const topic = TOPIC_RE.exec(text);
  const topicValue = topic ? cleanCapture(topic[1] || topic[2] || '') : '';
  if (topicValue && !profile.favoriteTopic) {
    profile.favoriteTopic = topicValue;
    detailed = true;
  }
  const art = ART_RE.exec(text);
  if (art) {
    const raw = art[1].toLowerCase();
    profile.artStyle = raw.startsWith('3') || raw.startsWith('three') ? '3d' : 'cartoon';
    detailed = true;
  }
  if (!detailed) {
    // Volunteered important detail is copied to optional custom info even
    // when not required — the required loop still continues until complete.
    const snippet = cleanCapture(text);
    if (snippet) {
      profile.customInfo = profile.customInfo ? `${profile.customInfo}; ${snippet}` : snippet;
    }
  }
  refreshDescription(profile);
  return { signal: 'detail' };
}

export function refreshDescription(profile: Profile): void {
  const who = profile.displayName || 'a new user';
  const topic = profile.favoriteTopic || 'photos';
  const extra = profile.customInfo ? ` Notes: ${profile.customInfo}.` : '';
  profile.description = `${who} who loves ${topic}.${extra}`.trim();
}
