import { useEffect, useRef, useState } from 'react';
import { View, Text, TopAppBar } from '@muse/native';
import { createWorld, step, readBodies, destroy } from '@muse/game-engine-sample';
import { stepGame, resetGame } from './loop';
import type { GameState } from './loop';

const FIXED_DT = 1 / 60;

// Flappy demo entry (T6, AC-SAMPLE-017): the frame loop lives here in
// TypeScript, advancing the pure `stepGame` transition once per frame and
// mapping the resulting state onto engine primitives. The engine physics
// package only integrates (`step`); zero drawing calls go into it, and it is
// imported by its scoped specifier (RULE-SAMPLE-001), never by a relative
// path into packages/.
export default function App() {
  const [game, setGame] = useState<GameState>(() => resetGame());
  const [bodies, setBodies] = useState<{ id: number; x: number; y: number; angle: number }[]>([]);
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
        return stepGame(prev, input, FIXED_DT);
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
      setGame(resetGame());
    } else {
      inputRef.current = 'tap';
    }
  };

  return (
    <View onTouchStart={tap}>
      <TopAppBar title="flappy" />
      <View>
        <Text>HUD</Text>
        <Text>score: {game.score}</Text>
      </View>
      <View x={0} y={game.birdY} />
      {bodies.map((b) => (
        <View key={b.id} x={b.x} y={b.y} rotation={b.angle} />
      ))}
      {game.status === 'game-over' ? <Text>game over — tap to restart</Text> : null}
    </View>
  );
}
