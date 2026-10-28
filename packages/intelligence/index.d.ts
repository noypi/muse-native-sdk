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
