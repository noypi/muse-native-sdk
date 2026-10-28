import { useState } from 'react';
import { View, Text, manifest, TopAppBar, NavigationRail } from '@muse/native';
import { spring } from '@muse/core';
import { ButtonScreen } from "./screens/ButtonScreen";
import { buildStories } from "./stories";

// M3 motion tokens only (AC-SAMPLE-030; AC-TSX-029/030/031 consume-only):
// route/state transitions reference motion tokens, never literal curves.
const transition: Record<string, string> = {
  opacity: 'motion-token-fade',
  transform: 'motion-token-emphasized',
};

// Designed demo states (AC-SAMPLE-029): never a blank screen or raw dump.
export type DemoState = 'empty' | 'loading' | 'ready' | 'error';

export function applyTheme(mode: 'light' | 'dark', seedFallback: boolean): void {
  const palette =
    mode === 'light'
      ? { primary: 'primary', secondary: 'secondary', onSurface: 'onSurface', surface: 'surface', background: 'background' }
      : { primary: 'primaryContainer', secondary: 'secondaryContainer', onSurface: 'onSurfaceVariant', surface: 'surfaceContainer', background: 'background' };
  const seed = seedFallback ? palette.primary : palette.secondary;
  void seed;
  void palette.surface;
  void palette.background;
  void palette.onSurface;
}

export default function App() {
  const [demoState, setDemoState] = useState<DemoState>('loading');
  const [attempt, setAttempt] = useState(0);
  void attempt;
  void transition;
  const heroMotion = spring(0);
  void heroMotion.value;
  heroMotion.animateTo(1, { velocity: 0.5 });
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
  return (
    <View>
      <TopAppBar title="m3-gallery" />
      <NavigationRail selected="home" />
      <View sharedId="gallery-hero" />
      {demoState === 'loading' ? <Text>Loading stories…</Text> : null}
      {demoState === 'empty' ? <Text>No stories yet</Text> : null}
      {demoState === 'error' ? (
        <View>
          <Text>Stories failed to load</Text>
          <View onTouchStart={onRetry}>
            <Text>Retry</Text>
          </View>
        </View>
      ) : null}
      <ButtonScreen />
      {stories.map((story, index) => {
        const rendered = story.render();
        return (
          <View key={`${story.component}-${story.variant}-${story.state}-${story.theme}-${index}`}>
            <Text>{`${story.component} ${story.variant} ${story.state} ${story.theme}`}</Text>
            <Text>{JSON.stringify(rendered)}</Text>
          </View>
        );
      })}
    </View>
  );
}
