// T9 — persistent MuseVoiceSpeaker (AC-SAMPLE-037).
// `renderSpeaker(state, opts)` is strictly presentational: it records which
// turn state the docked speaker shows and never issues a proxy call (no route
// import, no fetch, no stub access). The `<Speaker/>` below renders that
// recorded presentation; `reduceMotion` drops motion (static frame per state,
// meaning unchanged); `transcribeOn: false` renders off/idle, never hidden.

import { useEffect, useState } from 'react';
import { View, Text } from '@muse/native';

export type SpeakerState =
  | 'idle'
  | 'listening'
  | 'loading'
  | 'thinking'
  | 'ready'
  | 'denied'
  | 'no-speech';

export type SpeakerOpts = {
  reduceMotion: boolean;
  transcribeOn: boolean;
};

const VALID_STATES: ReadonlySet<string> = new Set([
  'idle',
  'listening',
  'loading',
  'thinking',
  'ready',
  'denied',
  'no-speech',
]);

export function isSpeakerState(value: unknown): value is SpeakerState {
  return typeof value === 'string' && VALID_STATES.has(value);
}

// Distinct static presentation per state so all seven turn states are readable
// at a glance (shape/label preserved with or without motion).
const STATE_LABELS: Record<SpeakerState, string> = {
  idle: 'Speaker idle',
  listening: 'Speaker listening…',
  loading: 'Speaker loading…',
  thinking: 'Speaker thinking…',
  ready: 'Speaker ready',
  denied: 'Speaker denied — microphone off',
  'no-speech': 'Speaker no-speech — nothing heard',
};

type Presentation = {
  state: SpeakerState;
  reduceMotion: boolean;
  transcribeOn: boolean;
};

let current: Presentation = {
  state: 'idle',
  reduceMotion: false,
  transcribeOn: true,
};
const listeners = new Set<() => void>();

function emit(): void {
  for (const notify of [...listeners]) {
    try {
      notify();
    } catch (err) {
      void err;
    }
  }
}

function subscribeNotify(notify: () => void): () => void {
  listeners.add(notify);
  return () => {
    listeners.delete(notify);
  };
}

export function getSpeakerPresentation(): Presentation {
  return current;
}

// renderSpeaker(state, opts) — strictly presentational, never a proxy call.
// Unknown state values render nothing new (typed union admits only the seven
// states; the call is ignored). Transcribe off keeps the speaker visible
// showing off/idle rather than hiding it.
export function renderSpeaker(state: SpeakerState, opts: SpeakerOpts): void {
  if (!isSpeakerState(state)) return;
  if (!opts || typeof opts.reduceMotion !== 'boolean' || typeof opts.transcribeOn !== 'boolean') return;
  current = {
    state,
    reduceMotion: opts.reduceMotion,
    transcribeOn: opts.transcribeOn,
  };
  emit();
}

export function resetSpeaker(): void {
  current = { state: 'idle', reduceMotion: false, transcribeOn: true };
  emit();
}

// Display label for the recorded presentation. Transcribe off always shows
// the off/idle line (never hidden); reduceMotion appends a static-frame marker
// while keeping the per-state label distinct.
export function speakerLabel(presentation: Presentation = current): string {
  if (!presentation.transcribeOn) return 'Speaker off (idle)';
  const base = STATE_LABELS[presentation.state];
  return presentation.reduceMotion ? `${base} [static]` : base;
}

// Speaker — docked voice-state readout. Always renders (never hidden, never
// null) so transcribe-off stays visible as off/idle. Every voice action keeps
// its touch equivalent elsewhere on the page (shared with T7 echoToggle); the
// hint below is static text, not a capture trigger — the speaker never
// initiates capture.
export function Speaker() {
  const [, setTick] = useState(0);
  useEffect(() => subscribeNotify(() => setTick((n) => n + 1)), []);
  const presentation = getSpeakerPresentation();
  return (
    <View>
      <Text>{speakerLabel(presentation)}</Text>
      <Text>Touch equivalents below for every voice action</Text>
    </View>
  );
}

export default Speaker;
