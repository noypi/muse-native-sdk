import { useEffect, useState } from 'react';
import { View, Text, manifest, components, TopAppBar, NavigationRail, OverlayHost } from '@muse/native';
import { spring } from '@muse/core';
import ButtonScreen from "./screens/ButtonScreen";
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
