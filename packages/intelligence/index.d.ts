// T3 — @muse/intelligence type surface (Q-MUSE-003 settled shapes).
// `target` is StateHandle<string> from @muse/core (opaque here).

export type IconVariant = 'filled' | 'tonal' | 'outlined' | 'standard';

export interface MuseVoiceOptions {
  icon: string;
  activeIcon: string;
  iconVariant: IconVariant;
  activeVariant: IconVariant;
  confirmIcon?: string;
  /** StateHandle<string> from @muse/core — accepted opaquely, never shared. */
  target: unknown;
  preset?: string;
  /** Hermetic stand-in for muse.manifest.json registered icon tokens. */
  icons?: Set<string> | string[];
  /** Optional known-preset registry; absent registry fails closed. */
  presets?: Record<string, unknown> | Set<string> | string[];
}

export interface MuseVoiceControl {
  readonly turnId: string;
  status: string;
  readonly target: unknown;
  setStatus(next: string): void;
  icon(): { name: string; variant: IconVariant };
}

export interface MuseVoiceSpeaker {
  readonly turnId: string;
}

export interface VoiceState {
  readonly turnId: string;
  readonly target: unknown;
  readonly status: string;
}

export declare function createMuseVoice(
  opts: MuseVoiceOptions
): readonly [MuseVoiceControl, MuseVoiceSpeaker, VoiceState];
