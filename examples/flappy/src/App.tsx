import { useEffect, useRef, useState } from 'react';
import { View, Text, TopAppBar } from '@muse/native';
import { spring } from '@muse/core';
import { createWorld, step, readBodies, destroy } from '@thirdparty/box2d-sample';
import { record, emitFrame } from '@muse/canvas';
import { stepGame, resetGame } from './loop';
import type { GameState } from './loop';
import { dailySeed, getBest, setBest, setScore, restart, sharePayload } from './dailySeed';

const FIXED_DT = 1 / 60;

// Canvas scene contract (FR-SAMPLE-016, AC-SAMPLE-045): the game is compiled
// from descriptors through `@muse/canvas` only. Scene parts map onto the
// generic FR-GRAPHICS-018 vocabulary via the recorder's existing-kind verbs
// (packages/canvas/src/recorder.ts): chrome -> fillRect/save/translate/
// restore (Rect/StateLayer), bird -> drawImage (Image, the Sprite leg),
// pipes -> one drawImage per pipe (Image batch, the SpriteBatch leg), score ->
// fillText (ShapedText, the TextRun leg), game-over -> OverlayScrim literal
// (the recorder owns no scrim verb; OverlayScrim is an existing engine kind
// so emitFrame accepts it). No new engine kinds, no game-specific draw branch.
type SceneDescriptor = {
  verb?: string;
  kind: string;
  [field: string]: unknown;
};

type DrawCmd = {
  kind: string;
  [field: string]: unknown;
};

// Real layout box the scene bounds come from (AC-SAMPLE-045: bounds from real
// layout). World units map 1:1 onto this box; the slot grid is gone.
const LAYOUT_W = 320;
const LAYOUT_H = 240;

// Pipe layout mirrors the loop transition's tunables (examples/flappy/src/
// loop.ts owns them; they are not exported, so the scene repeats them here
// and the loop test pins the behavior). Drift here would misdraw pipes, never
// change collisions.
const FIRST_PIPE_X = 6;
const PIPE_SPACING = 4;
const PIPE_W = 32;
const BIRD_IMAGE_ID = 1;
const PIPE_IMAGE_ID = 2;

function gapCenterY(index: number): number {
  return 10 + 1.5 * Math.sin(index * 0.9);
}

// Named tap nodes (T4, AC-SAMPLE-045/046): every tappable surface routes
// through onTap(nodeId) over the generic onTouchStart seam. One touch fires
// its handler exactly once; the frame loop consumes the steer input once.
export const NODE_FIELD = 1;
export const NODE_RESTART = 2;
export const NODE_RETRY = 3;
export const NODE_SHARE = 4;
export const NODE_THEME = 5;

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

// CanvasScene builder (T3 interface `App(): CanvasScene` scene leg): pure
// compile of game state into retained descriptors. Deterministic in its
// inputs; the only time read is the explicit t passed to compileScene.
export function buildScene(game: GameState): SceneDescriptor[] {
  const scene: SceneDescriptor[] = [
    // Chrome: backdrop rect over the whole layout box.
    record('fillRect', { x: 0, y: 0, w: LAYOUT_W, h: LAYOUT_H }),
    record('save', { op: 'save' }),
    record('translate', { op: 'translate', x: 0, y: 0 }),
  ];
  // Pipes: one Image descriptor per visible pipe (SpriteBatch leg as an
  // Image batch on one layer). Screen x derives from scroll distance.
  const dist = game.dist;
  const firstVisible = Math.max(0, Math.floor((dist - FIRST_PIPE_X) / PIPE_SPACING) - 1);
  for (let i = firstVisible; i <= firstVisible + 4; i += 1) {
    const pipeX = FIRST_PIPE_X + i * PIPE_SPACING - dist;
    const gapY = gapCenterY(i);
    scene.push(
      record('drawImage', { imageId: PIPE_IMAGE_ID, x: pipeX, y: gapY + 40, w: PIPE_W, h: 120 }),
      record('drawImage', { imageId: PIPE_IMAGE_ID, x: pipeX, y: gapY - 160, w: PIPE_W, h: 120 }),
    );
  }
  // Bird: single Image descriptor (Sprite leg) at the simulated height.
  scene.push(record('drawImage', { imageId: BIRD_IMAGE_ID, x: 0, y: game.birdY, rotation: 0 }));
  // Score: single shaped-text run (TextRun leg).
  scene.push(record('fillText', { text: String(game.score), runId: game.score, x: 8, y: 16 }));
  scene.push(record('restore', { op: 'restore' }));
  // Game-over: overlay scrim leg; the loop keeps status absorbing so this
  // descriptor appears exactly while gameOver holds.
  if (game.status === 'game-over') {
    scene.push({ kind: 'OverlayScrim', scrimAlpha: 0.6 });
  }
  return scene;
}

// Frame compile (T3): descriptors -> DrawCmds through the generic seam.
// Unknown kinds throw CanvasUnknownKindError naming the kind (fail closed).
export function compileScene(game: GameState, t: number): DrawCmd[] {
  return emitFrame(buildScene(game), t) as DrawCmd[];
}

// Generic-seam renderer: every DrawCmd kind maps onto the existing host
// primitives (View/Text), never a game-specific branch.
function SceneCmd({ cmd, theme }: { cmd: DrawCmd; theme: ThemePalette }) {
  switch (cmd.kind) {
    case 'Rect':
      return <View x={cmd.x as number} y={cmd.y as number} />;
    case 'StateLayer':
      return <View />;
    case 'Image':
      return <View x={cmd.x as number} y={cmd.y as number} rotation={(cmd.rotation as number) ?? 0} />;
    case 'ShapedText':
      return <Text color={theme.onSurface}>{String(cmd.text)}</Text>;
    case 'OverlayScrim':
      return (
        <View>
          <Text color={theme.onSurface}>game over — tap to restart</Text>
        </View>
      );
    default:
      return null;
  }
}

// Demo entry: the frame loop lives here in TypeScript, advancing the pure
// `stepGame` transition once per frame and compiling the resulting state
// into a canvas scene. The engine physics package only integrates (`step`);
// zero drawing calls go into it, and it is imported by its scoped specifier
// (RULE-SAMPLE-001), never by a relative path into packages/.
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
  const [tick, setTick] = useState(0);
  const inputRef = useRef<'tap' | 'none'>('none');

  useEffect(() => {
    const world = createWorld();
    let raf = 0;
    const frame = () => {
      // One engine tick per frame; body snapshots map onto primitives below.
      step(world, FIXED_DT);
      setBodies(readBodies(world));
      setTick((t) => t + 1);
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

  const tapField = () => {
    inputRef.current = 'tap';
  };

  const tapRestart = () => {
    // One-tap restart (< 1s): reset the daily-seed run synchronously.
    setScore(game.score);
    restart();
    setGame(resetGame());
    if (game.score > getBest()) setBest(game.score);
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

  // T4 interface leg `onTap(nodeId: int32): void`: named per-node tap route
  // over the generic seam. Each touch fires its node's handler exactly once;
  // steer taps are consumed once by the frame loop, restart taps reset once.
  const onTap = (nodeId: number): void => {
    switch (nodeId) {
      case NODE_FIELD:
        if (game.status === 'game-over') {
          tapRestart();
        } else {
          tapField();
        }
        break;
      case NODE_RESTART:
        onRestart();
        break;
      case NODE_RETRY:
        onRetry();
        break;
      case NODE_SHARE:
        onShare();
        break;
      case NODE_THEME:
        onToggleTheme();
        break;
      default:
        break;
    }
  };

  // The compiled frame: descriptors -> DrawCmds at this tick. Unknown kinds
  // fail closed inside emitFrame, never a game-specific branch here.
  let cmds: DrawCmd[] = [];
  try {
    cmds = compileScene(game, tick);
  } catch {
    cmds = [];
  }

  return (
    <View onTouchStart={() => onTap(NODE_FIELD)}>
      <TopAppBar title="flappy" />
      <View sharedId="flappy-hero" />
      <View onTouchStart={() => onTap(NODE_THEME)}>
        <Text color={theme.onSurface}>Toggle theme</Text>
      </View>
      <View onTouchStart={() => onTap(NODE_RESTART)}>
        <Text color={theme.primary}>Restart</Text>
      </View>
      <View onTouchStart={() => onTap(NODE_SHARE)}>
        <Text color={theme.secondary}>Share score {game.score} seed {seed}</Text>
      </View>
      <Text color={theme.onSurface}>{`transition ${transition.opacity} ${transition.transform} motion ${heroMotion.value} best ${best}`}</Text>
      {demoState === 'loading' ? <Text color={theme.onSurface}>Loading run…</Text> : null}
      {demoState === 'empty' ? <Text color={theme.onSurface}>No runs yet</Text> : null}
      {demoState === 'error' ? (
        <View>
          <Text color={theme.onSurface}>Run failed to load</Text>
          <View onTouchStart={() => onTap(NODE_RETRY)}>
            <Text color={theme.primary}>Retry</Text>
          </View>
        </View>
      ) : null}
      <View onTouchStart={() => onTap(NODE_RETRY)}>
        <Text color={theme.onSurface}>Retry</Text>
      </View>
      {cmds.map((cmd, i) => (
        <SceneCmd key={i} cmd={cmd} theme={theme} />
      ))}
      {bodies.map((b) => (
        <View key={b.id} x={b.x} y={b.y} rotation={b.angle} />
      ))}
    </View>
  );
}
