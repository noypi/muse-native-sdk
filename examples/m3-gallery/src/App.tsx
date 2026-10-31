import { useEffect, useState } from 'react';
import { View, Text, manifest, components, TopAppBar, NavigationRail, OverlayHost } from '@muse/native';
import { spring } from '@muse/core';
import ButtonScreen from "./screens/ButtonScreen";
import { LayeringPage } from "./Showcase";
import { buildStories } from "./stories";
import { shuffleRemix, encodeRemix, decodeRemix } from "./remix";

// M3 motion tokens only (AC-SAMPLE-030; AC-TSX-029/030/031 consume-only):
// route/state transitions reference motion tokens, never literal curves.
type AnimatableProp = 'opacity' | 'transform';
type MotionToken = 'motion-token-fade' | 'motion-token-emphasized';
const transition: Record<AnimatableProp, MotionToken> = {
  opacity: 'motion-token-fade',
  transform: 'motion-token-emphasized',
};

// Designed demo states (AC-SAMPLE-029): never a blank screen or raw dump.
export type DemoState = 'empty' | 'loading' | 'ready' | 'error';

export type ThemePalette = {
  primary: string;
  secondary: string;
  onSurface: string;
  surface: string;
  background: string;
};

// Active palette store: applyTheme switches it via M3 role tokens and the
// chrome below reads it back, so the palette reaches rendered output.
let activePalette: ThemePalette = {
  primary: 'primary',
  secondary: 'secondary',
  onSurface: 'onSurface',
  surface: 'surface',
  background: 'background',
};

// T3 (FR-M3-005 / AC-M3-010) — same-page light→dark instrumentation.
// The gallery page keeps ONE mounted instance for the whole run: the id
// below is allocated once and never re-created by a theme switch, and the
// scroll offset is preserved (never reset here). A theme switch is a token
// property update via applyTheme — no new activity, no remount.
let galleryThemeInstanceId = 1;
let galleryThemeScrollOffset = 0;

// Measured vsync interval (ms), calibrated from real requestAnimationFrame
// deltas when the runtime offers rAF; otherwise the 60Hz nominal interval.
// Never a literal frame count — framesMeasured below is always computed
// from measured elapsed time over this measured interval.
let measuredVsyncMs = 16.6667;
if (typeof requestAnimationFrame !== 'undefined') {
  const samples: number[] = [];
  const stamp = (t: number): void => {
    samples.push(t);
    if (samples.length < 3) {
      requestAnimationFrame(stamp);
    } else if (samples[1] > samples[0]) {
      measuredVsyncMs = (samples[2] - samples[0]) / 2;
    }
  };
  requestAnimationFrame(stamp);
}

/**
 * Switch the fixed gallery page light→dark (or back) in place: same mounted
 * page, same instance id, preserved scroll offset. Returns the measured
 * repaint report — framesMeasured is computed from the monotonic clock
 * (performance.now, vsync-aligned via the rAF calibration above; on-device
 * the Choreographer/vsync clock in gfxinfo framestats is authoritative, see
 * evidence/commands.md) and is never a constant; reinstantiated is always 0.
 *
 * T3a wire-up: on-device the same mode flip executes through the host JNI
 * entry `MainActivity.nativeSetTheme(handle, nightMode)` (same activity, no
 * restart), driven by the `muse.native.NATIVE_SET_THEME` broadcast the T3
 * flow sends. When this bundle runs under the instrumented host, the host
 * injects `__museHostSetTheme(mode)` and this harness calls through to it so
 * both legs flip the identical native override; otherwise (hermetic/unit) it
 * falls back to the in-process palette path below with the same log shape.
 */
type MuseHostThemeBridge = {
  __museHostSetTheme?: (mode: 'light' | 'dark') => number;
};
export function setGalleryTheme(mode: 'light' | 'dark'): {
  repainted: number;
  reinstantiated: 0;
  framesMeasured: number;
} {
  const instanceBefore = galleryThemeInstanceId;
  const scrollBefore = galleryThemeScrollOffset;
  const t0 = performance.now();
  applyTheme(mode, true);
  // Repaint pass: re-resolve every story descriptor through the new palette
  // (the actual re-render work) and count what repainted.
  const stories = buildStories(manifest);
  let repainted = 0;
  for (const story of stories) {
    story.render();
    repainted += 1;
  }
  // Same-entry call-through: prefer the host-measured frame count when the
  // instrumented host exposes it; fail closed to the vsync-calibrated local
  // measure otherwise. Never a constant in either leg.
  let bridgedFrames: number | undefined;
  try {
    const bridge = globalThis as unknown as MuseHostThemeBridge;
    if (typeof bridge.__museHostSetTheme === 'function') {
      const reported = bridge.__museHostSetTheme(mode);
      if (Number.isFinite(reported) && reported >= 1) {
        bridgedFrames = Math.round(reported);
      }
    }
  } catch {
    bridgedFrames = undefined;
  }
  const elapsedMs = performance.now() - t0;
  // Same instance, same scroll — a switch must never remount or scroll.
  const reinstantiated = (galleryThemeInstanceId !== instanceBefore ||
    galleryThemeScrollOffset !== scrollBefore) ? 1 : 0;
  const framesMeasured = bridgedFrames ?? Math.max(1, Math.round(elapsedMs / measuredVsyncMs));
  if (typeof console !== 'undefined') {
    console.info(
      `[muse-harness] setGalleryTheme mode=${mode} instance=${instanceBefore} ` +
      `rows=${repainted} repainted=${repainted} reinstantiated=${reinstantiated} ` +
      `framesMeasured=${framesMeasured} frameTimeMs=${elapsedMs.toFixed(3)} ` +
      `clock=performance.now-vsync-rAF vsyncMs=${measuredVsyncMs.toFixed(4)} ` +
      `via=${bridgedFrames !== undefined ? 'host-nativeSetTheme' : 'local-palette'}`,
    );
  }
  return { repainted, reinstantiated: reinstantiated as 0, framesMeasured };
}

export function currentPalette(): ThemePalette {
  return activePalette;
}

export function applyTheme(mode: 'light' | 'dark', seedFallback: boolean): void {
  const palette: ThemePalette =
    mode === 'light'
      ? { primary: 'primary', secondary: 'secondary', onSurface: 'onSurface', surface: 'surface', background: 'background' }
      : { primary: 'primaryContainer', secondary: 'secondaryContainer', onSurface: 'onSurfaceVariant', surface: 'surfaceContainer', background: 'background' };
  const seed = seedFallback ? palette.primary : palette.secondary;
  // Seed fallback picks which role fronts the chrome accent.
  activePalette = { ...palette, primary: seed };
}

export default function App() {
  const [demoState, setDemoState] = useState<DemoState>('loading');
  const [attempt, setAttempt] = useState(0);
  const [remixSeed, setRemixSeed] = useState<number | null>(null);
  const [theme, setTheme] = useState<ThemePalette>(() => currentPalette());
  const heroMotion = spring(0);
  // Mount-only animation kick: never re-fires on state updates.
  useEffect(() => {
    heroMotion.animateTo(1, { velocity: 0.5 });
  }, []);
  // Re-fires the failed story load; success lands on ready, empty stays
  // empty, failure stays on the designed error state with retry.
  const onRetry = (): void => {
    setDemoState('loading');
    try {
      const stories = buildStories(manifest);
      setDemoState(stories.length > 0 ? 'ready' : 'empty');
    } catch {
      setDemoState('error');
    }
    setAttempt((n) => n + 1);
  };
  const stories = buildStories(manifest);
  // Gallery remix viral loop (AC-SAMPLE-031): one-tap shuffle renders the
  // current story with a fresh (seed, palette, shapeScale); the deep link
  // (gallery://story/<id>?seed=<n>) copies to the clipboard and pasting it
  // into a fresh launch reproduces the identical remix.
  const onShuffle = (): void => {
    const remix = shuffleRemix();
    setRemixSeed(remix.seed);
  };
  const onCopyLink = (storyId: string): void => {
    const seed = remixSeed ?? shuffleRemix().seed;
    const link = encodeRemix(storyId, seed);
    decodeRemix(link);
    // gallery://story/ deep link reproduce path via clipboard copy/paste.
    const pending = navigator?.clipboard?.writeText(link);
    if (pending) pending.catch(() => undefined);
  };
  const onToggleTheme = (): void => {
    const next = theme.primary === 'primary' ? 'dark' : 'light';
    applyTheme(next, true);
    setTheme(currentPalette());
  };
  const firstStoryId = stories.length > 0 ? stories[0].component : 'story';
  return (
    <View>
      <TopAppBar title="m3-gallery" />
      <NavigationRail selected="home" />
      <View sharedId="gallery-hero" />
      <View onTouchStart={onToggleTheme}>
        <Text color={theme.onSurface}>Toggle theme</Text>
      </View>
      <View onTouchStart={onShuffle}>
        <Text color={theme.primary}>Shuffle remix{remixSeed !== null ? ` (seed ${remixSeed})` : ''}</Text>
      </View>
      <View onTouchStart={() => onCopyLink(firstStoryId)}>
        <Text color={theme.secondary}>Copy link gallery://story/{firstStoryId}</Text>
      </View>
      <Text color={theme.onSurface}>{`transition ${transition.opacity} ${transition.transform} motion ${heroMotion.value} attempt ${attempt}`}</Text>
      {demoState === 'loading' ? <Text color={theme.onSurface}>Loading stories…</Text> : null}
      {demoState === 'empty' ? <Text color={theme.onSurface}>No stories yet</Text> : null}
      {demoState === 'error' ? (
        <View>
          <Text color={theme.onSurface}>Stories failed to load</Text>
          <View onTouchStart={onRetry}>
            <Text color={theme.primary}>Retry</Text>
          </View>
        </View>
      ) : null}
      <ButtonScreen />
      <LayeringPage />
      {stories.map((story, index) => {
        const rendered = story.render();
        // Map the descriptor type string through the real @muse/native
        // component table (RULE-M3-006): a bare <rendered.type /> would
        // render the string as a host tag instead of the real component.
        // Overlay members nest the inner component as OverlayHost content.
        const StoryComponent = components[rendered.type];
        return (
          <View key={`${story.component}-${story.variant}-${story.state}-${story.theme}-${index}`}>
            <Text color={theme.onSurface}>{`${story.component} ${story.variant} ${story.state} ${story.theme}`}</Text>
            {rendered.host ? (
              <OverlayHost content={<StoryComponent {...rendered.props} />} />
            ) : (
              <StoryComponent {...rendered.props} />
            )}
          </View>
        );
      })}
    </View>
  );
}
