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

/** Alias for `createMuseVoice` (root-entry convenience). */
export declare const MuseVoice: typeof createMuseVoice;

/**
 * Free a target bound by `createMuseVoice` so voice controls can be
 * remounted on the same target object. While held, double-mount still
 * throws `{ code: 'shared-target' }`.
 */
export declare function releaseTarget(target: unknown): void;

export interface TurnHandle {
  readonly id: string;
  status: string;
  lastError: { code: string; route: string } | undefined;
  commit(targetId: string): boolean;
  deny(route?: string): void;
  noSpeech(route?: string): void;
  fail(code: string, route: string): void;
}

export declare function createTurn(): TurnHandle;

export interface StartTurnSeen {
  partials: string[];
  final: string;
  reasonDeltas: string[];
  partialTurnIds: Array<string | undefined>;
  finalTurnId: string | undefined;
  reasonTurnIds: Array<string | undefined>;
  route: string;
  latencyMs: number;
  reason: string;
  status: string;
  error: { code: string; route: string } | undefined;
  /** Resolves to this same outcome once all route promises settle. */
  done: Promise<StartTurnSeen>;
}

export declare function startTurn(
  control: unknown,
  audioChunk: unknown,
  deps?: Record<string, unknown>
): StartTurnSeen;

// T7 — MuseImage fallbackOf→generate-image (Q-MUSE-003 settled shapes).
// `size` is an opaque string token; `fallbackOf`/`cacheKey`/`preset` optional.

export type ImageStyle = 'avatar' | 'illustration' | 'banner' | 'sticker' | 'poster';

export interface MuseImageOptions {
  prompt: string;
  style: ImageStyle;
  size: string;
  seed: string;
  fallbackOf?: string;
  cacheKey?: string;
  preset?: string;
}

export interface GeneratedAsset {
  url: string;
  seed: string;
  width: number;
  height: number;
}

export interface MuseImageHandle {
  readonly props: MuseImageOptions;
  readonly cacheKey: string;
  resolveImage(): Promise<GeneratedAsset>;
}

export declare function MuseImage(
  props: MuseImageOptions,
  deps?: {
    fetchFallback?: (url: string) => Promise<{ status: number; body: GeneratedAsset | null }>;
    generateImage?: (req: Record<string, unknown>) => Promise<GeneratedAsset>;
    presets?: Record<string, unknown> | Set<string> | string[];
  }
): MuseImageHandle;

// T10 — Platform speech renderer (Android TTS + TalkBack, zero network).
// `voice`/`rate` are platform TTS settings passed to the bridge only, never
// upstream params. `prompt` shapes phrasing via the `reason` route only
// (wording/pauses), never a voice-model selector. No `speak` proxy route
// exists or fires. `controls` chrome belongs to T11 (renderer stays open for
// extension; this surface carries no controls descriptor).

export interface AndroidTtsBridge {
  speak(text: string): void;
  stop(): void;
  pause?(): void;
  resume?(): void;
  setRate?(rate: number): void;
  setVoice?(voice: string): void;
}

/** Resolvable children text: strings and nested arrays join to the script. */
export type TextBearing = unknown;

export type SpeechState = 'speaking' | 'paused' | 'interrupted' | 'idle';

export interface PlatformSpeechOptions {
  script?: string;
  children?: TextBearing;
  prompt?: string;
  voice?: string;
  rate?: number;
  tts: AndroidTtsBridge;
  controls?: 'bar' | 'minimal' | 'none';
}

export interface SpeechHandle {
  play(): void;
  pause(): void;
  stop(): void;
  setChildren(next: TextBearing): void;
  interrupt(reason: string): void;
  duck(): void;
  readonly state: SpeechState;
  readonly controls: { buttons: string[]; progress?: string; routeChip?: string };
  readonly lastError: { code: string; source: 'platform' } | undefined;
}

export declare function createPlatformSpeech(
  opts: PlatformSpeechOptions & {
    fetch?: unknown;
    audioFocus?: { duck(): void; restore(): void };
    lifecycle?: { onPause(fn: () => void): void; onResume(fn: () => void): void };
  }
): SpeechHandle;
