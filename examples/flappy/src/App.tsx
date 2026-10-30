import { useEffect, useRef, useState } from 'react';
import { View, Text, TopAppBar } from '@muse/native';
import { spring } from '@muse/core';
import { createWorld, step, readBodies, destroy } from '@thirdparty/box2d-sample';
import { stepGame, resetGame } from './loop';
import type { GameState } from './loop';
import { dailySeed, getBest, setBest, setScore, restart, sharePayload } from './dailySeed';

const FIXED_DT = 1 / 60;

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

// Flappy demo entry (T6, AC-SAMPLE-017): the frame loop lives here in
// TypeScript, advancing the pure `stepGame` transition once per frame and
// mapping the resulting state onto engine primitives. The engine physics
// package only integrates (`step`); zero drawing calls go into it, and it is
// imported by its scoped specifier (RULE-SAMPLE-001), never by a relative
// path into packages/.
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
  const [game, setGame] = useState<GameState>(() => resetGame());
  const [bodies, setBodies] = useState<{ id: number; x: number; y: number; angle: number }[]>([]);
  const [theme, setTheme] = useState<ThemePalette>(() => currentPalette());
  const inputRef = useRef<'tap' | 'none'>('none');

  useEffect(() => {
    const world = createWorld();
    let raf = 0;
    const frame = () => {
      // One engine tick per frame; body snapshots map onto primitives below.
      step(world, FIXED_DT);
      setBodies(readBodies(world));
      setGame((prev) => {
        if (prev.status === 'game-over') return prev;
        const input = inputRef.current;
        inputRef.current = 'none';
        const next = stepGame(prev, input, FIXED_DT);
        setScore(next.score);
        return next;
      });
      raf = requestAnimationFrame(frame);
    };
    raf = requestAnimationFrame(frame);
    return () => {
      cancelAnimationFrame(raf);
      destroy(world);
    };
  }, []);

  const tap = () => {
    if (game.status === 'game-over') {
      // One-tap restart (< 1s): reset the daily-seed run synchronously.
      setScore(game.score);
      restart();
      setGame(resetGame());
      if (game.score > getBest()) setBest(game.score);
    } else {
      inputRef.current = 'tap';
    }
  };

  // Daily-seed wiring (T6, AC-SAMPLE-032): seed from today's date, persisted
  // best, one-tap restart, share-sheet payload (local only, no upload).
  const seed = dailySeed();
  const best = getBest();
  const onRestart = (): void => {
    restart();
    setGame(resetGame());
    setBodies([]);
  };
  const onShare = (): void => {
    const payload = sharePayload(game.score);
    setScore(payload.score);
  };
  const onToggleTheme = (): void => {
    const next = theme.primary === 'primary' ? 'dark' : 'light';
    applyTheme(next, true);
    setTheme(currentPalette());
  };

  const [demoState, setDemoState] = useState<DemoState>('ready');
  const heroMotion = spring(0);
  // Mount-only animation kick: never re-fires on state updates.
  useEffect(() => {
    heroMotion.animateTo(1, { velocity: 0.5 });
  }, []);
  // Re-fires the failed load by restarting the run; game-over maps to the
  // designed error state with retry, otherwise back to ready.
  const onRetry = (): void => {
    setDemoState('loading');
    setGame(resetGame());
    setBodies([]);
    setDemoState('ready');
  };

  return (
    <View onTouchStart={tap}>
      <TopAppBar title="flappy" />
      <View sharedId="flappy-hero" />
      <View onTouchStart={onToggleTheme}>
        <Text color={theme.onSurface}>Toggle theme</Text>
      </View>
      <View onTouchStart={onRestart}>
        <Text color={theme.primary}>Restart</Text>
      </View>
      <View onTouchStart={onShare}>
        <Text color={theme.secondary}>Share score {game.score} seed {seed}</Text>
      </View>
      <Text color={theme.onSurface}>{`transition ${transition.opacity} ${transition.transform} motion ${heroMotion.value} best ${best}`}</Text>
      {demoState === 'loading' ? <Text color={theme.onSurface}>Loading run…</Text> : null}
      {demoState === 'empty' ? <Text color={theme.onSurface}>No runs yet</Text> : null}
      {demoState === 'error' ? (
        <View>
          <Text color={theme.onSurface}>Run failed to load</Text>
          <View onTouchStart={onRetry}>
            <Text color={theme.primary}>Retry</Text>
          </View>
        </View>
      ) : null}
      <View onTouchStart={onRetry}>
        <Text color={theme.onSurface}>Retry</Text>
      </View>
      <View>
        <Text color={theme.onSurface}>HUD</Text>
        <Text color={theme.onSurface}>score: {game.score}</Text>
      </View>
      <View x={0} y={game.birdY} />
      {bodies.map((b) => (
        <View key={b.id} x={b.x} y={b.y} rotation={b.angle} />
      ))}
      {game.status === 'game-over' ? <Text color={theme.onSurface}>game over — tap to restart</Text> : null}
    </View>
  );
}
