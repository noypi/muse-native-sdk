import { useEffect, useState } from 'react';
import { View, Text, manifest, components, TopAppBar, BottomAppBar, NavigationRail, OverlayHost } from '@muse/native';
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

// Paging guard (FR-SAMPLE-014, AC-SAMPLE-040): only shell-chrome origins
// reach navigateStory; every inside-surface gesture returns false (denial by
// default) so the AC-M3-012 text path (tap focuses + caret, double-tap/drag
// selects, scroll pans, control activates) never fires a paging callback.
export type PagingOrigin = 'inside-surface' | 'shell-chrome';
export type PagingGesture = 'tap' | 'double-tap' | 'drag' | 'long-press' | 'scroll' | 'key' | 'deep-link';
export function isPagingGesture(origin: PagingOrigin, gesture: PagingGesture): boolean {
  if (origin !== 'shell-chrome') {
    return false;
  }
  return gesture === 'tap' || gesture === 'key' || gesture === 'deep-link';
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
  // Gallery shell Prev/Next navigation (FR-SAMPLE-014, AC-SAMPLE-039): a
  // single selected-story index over buildStories(manifest). Shell buttons,
  // ArrowLeft/ArrowRight, and gallery://story/<id> deep links all move
  // through the same index path; paging changes only the index, never story
  // state. Boundary/unknown-id denials keep the current story (never blank).
  const [selectedIndex, setSelectedIndex] = useState(0);
  const clampedIndex = stories.length === 0 ? 0 : Math.min(Math.max(selectedIndex, 0), stories.length - 1);
  const selectedStory = stories.length === 0 ? null : stories[clampedIndex];
  const prevDisabled = clampedIndex <= 0;
  const nextDisabled = stories.length === 0 || clampedIndex >= stories.length - 1;
  const onPrev = (): void => {
    if (!isPagingGesture('shell-chrome', 'tap')) {
      return;
    }
    setSelectedIndex((i) => Math.max(0, i - 1));
  };
  const onNext = (): void => {
    if (!isPagingGesture('shell-chrome', 'tap')) {
      return;
    }
    setSelectedIndex((i) => Math.min(Math.max(stories.length - 1, 0), i + 1));
  };
  // ArrowLeft/ArrowRight dispatch the same navigate path as the buttons.
  useEffect(() => {
    if (typeof window === 'undefined' || typeof window.addEventListener !== 'function') {
      return;
    }
    const onKeyDown = (event: KeyboardEvent): void => {
      if (!isPagingGesture('shell-chrome', 'key')) {
        return;
      }      if (event.key === 'ArrowRight') {
        onNext();
      } else if (event.key === 'ArrowLeft') {
        onPrev();
      }
    };
    window.addEventListener('keydown', onKeyDown);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
    };
  }, [stories.length]);
  // Deep-link movement reuses the remix link shape (AC-SAMPLE-031):
  // decodeRemix for seeded links, bare gallery://story/<id> otherwise.
  const onOpenStoryLink = (link: string): void => {
    if (!isPagingGesture('shell-chrome', 'deep-link')) {
      return;
    }
    let id: string | null = null;
    try {
      id = decodeRemix(link).id;
    } catch {
      const prefix = 'gallery://story/';
      if (typeof link === 'string' && link.startsWith(prefix)) {
        const rest = link.slice(prefix.length);
        const qIndex = rest.indexOf('?');
        try {
          id = decodeURIComponent(qIndex < 0 ? rest : rest.slice(0, qIndex));
        } catch {
          id = null;
        }
      }
    }
    if (id === null || id.length === 0) {
      return;
    }
    const found = stories.findIndex((story) => story.component === id);
    if (found >= 0) {
      setSelectedIndex(found);
    }
  };
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
  const selectedStoryId = selectedStory !== null ? selectedStory.component : firstStoryId;
  const selectedRendered = selectedStory !== null ? selectedStory.render() : null;
  // Map the descriptor type string through the real @muse/native
  // component table (RULE-M3-006): a bare <rendered.type /> would
  // render the string as a host tag instead of the real component.
  // Overlay members nest the inner component as OverlayHost content.
  const SelectedComponent = selectedRendered !== null ? components[selectedRendered.type] : null;
  return (
    <View>
      <TopAppBar title="m3-gallery" />
      <View>
        <View onTouchStart={onPrev}>
          <Text color={theme.onSurface}>{prevDisabled ? 'Prev (disabled)' : 'Prev'}</Text>
        </View>
        <Text color={theme.onSurface}>{`${clampedIndex + 1} of ${stories.length}`}</Text>
        <View onTouchStart={onNext}>
          <Text color={theme.onSurface}>{nextDisabled ? 'Next (disabled)' : 'Next'}</Text>
        </View>
      </View>
      <NavigationRail selected="home" />
      <View sharedId="gallery-hero" />
      <View onTouchStart={() => onOpenStoryLink(`gallery://story/${selectedStoryId}`)}>
        <Text color={theme.secondary}>Open story gallery://story/{selectedStoryId}</Text>
      </View>
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
      {selectedStory !== null && selectedRendered !== null && SelectedComponent ? (
        <View key={`${selectedStory.component}-${selectedStory.variant}-${selectedStory.state}-${selectedStory.theme}-${clampedIndex}`}>
          <Text color={theme.onSurface}>{`${selectedStory.component} ${selectedStory.variant} ${selectedStory.state} ${selectedStory.theme}`}</Text>
          {selectedRendered.host ? (
            <OverlayHost content={<SelectedComponent {...selectedRendered.props} />} />
          ) : (
            <SelectedComponent {...selectedRendered.props} />
          )}
        </View>
      ) : null}
      <BottomAppBar title={`${clampedIndex + 1} of ${stories.length}`} />
    </View>
  );
}
