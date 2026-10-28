// T7 — guided-voice echo toggle for muse-demo (AC-SAMPLE-034).
// Profile-settings toggle, persisted, default on. Echo on: every voice action
// renders a numbered on-screen echo with a tappable fallback per row, so
// guided voice stays usable without hearing the speaker. Echo off: terse
// confirmations only. Either way every voice action keeps a touch equivalent —
// the standalone transcribe entry below never wraps an input Field.

import { useState } from 'react';
import { View, Text } from '@muse/native';

// --- Persisted echo toggle (default on) --------------------------------------
// Module-level persistence mirrors the global transcribe toggle in
// `./profile.ts`: on by default, survives tab switches, carried by the
// onboarding → main redirect via the profile-settings page.
let echoOn = true;

export function isEchoOn(): boolean {
  return echoOn;
}

export function setEchoOn(next: boolean): void {
  echoOn = next;
}

export function resetEcho(): void {
  echoOn = true;
}

// Numbered on-screen echo lines for a voice action (`1. …`, `2. …`, …).
export function echoLines(items: string[]): string[] {
  return items.map((item, index) => `${index + 1}. ${item}`);
}

// Terse confirmation for echo-off mode: one short line, no numbered list.
export function terseConfirmation(done: boolean): string {
  return done ? 'Done.' : 'Listening.';
}

export type VoiceAction = {
  key: string;
  label: string;
};

export type EchoToggleProps = {
  value?: boolean;
  onChange?: (next: boolean) => void;
};

// Profile-settings echo toggle. Touching the row flips the persisted value;
// the manual fallback note stays visible so voice-off users keep full access.
export function EchoToggle({ value, onChange }: EchoToggleProps = {}) {
  const [on, setOn] = useState<boolean>(value !== null && value !== undefined ? value : isEchoOn());

  const flip = () => {
    const next = !on;
    setOn(next);
    setEchoOn(next);
    if (onChange) onChange(next);
  };

  return (
    <View>
      <Text>{on ? 'Guided-voice echo on' : 'Guided-voice echo off'}</Text>
      <View onTouchStart={flip}>
        <Text>{on ? 'echo icon on' : 'echo icon off'}</Text>
      </View>
      {on ? null : (
        <View>
          <Text>Terse confirmations; every voice action below keeps its touch equivalent</Text>
        </View>
      )}
    </View>
  );
}

export type GuidedVoiceProps = {
  actions: VoiceAction[];
  onAction?: (key: string) => void;
};

// Standalone voice entry with guided echo. Standalone: renders its own entry
// row and never wraps an input Field. Echo on: numbered on-screen echo where
// every row is tappable (the touch equivalent). Echo off: a terse
// confirmation line plus the same tappable rows, so no voice action is ever
// voice-only.
export function GuidedVoice({ actions, onAction }: GuidedVoiceProps) {
  const [heard, setHeard] = useState<boolean>(isEchoOn());
  const lines = echoLines(actions.map((entry) => entry.label));

  const fire = (key: string) => {
    if (onAction) onAction(key);
  };

  const flip = () => {
    const next = !heard;
    setHeard(next);
    setEchoOn(next);
  };

  if (!heard) {
    return (
      <View>
        <Text>{terseConfirmation(true)}</Text>
        {actions.map((entry) => (
          <View key={entry.key} onTouchStart={() => fire(entry.key)}>
            <Text>{entry.label}</Text>
          </View>
        ))}
        <View onTouchStart={flip}>
          <Text>echo icon off</Text>
        </View>
      </View>
    );
  }

  return (
    <View>
      {lines.map((line, index) => {
        const entry = actions[index];
        return (
          <View key={entry ? entry.key : `echo:${index}`} onTouchStart={() => entry && fire(entry.key)}>
            <Text>{line}</Text>
          </View>
        );
      })}
      <View onTouchStart={flip}>
        <Text>echo icon on</Text>
      </View>
    </View>
  );
}

export default EchoToggle;
