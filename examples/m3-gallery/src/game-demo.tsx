// Throwaway fixture owned by run fr-pkg-001..007; real gallery is FR-SAMPLE-001/FR-M3-007; do not extend.
import { View } from '@muse/native';
import { createWorld, step, readBodies, destroy } from '../../packages/box2d-sample/src/index.js';
import type { Body, WorldId } from '../../packages/box2d-sample/src/index.js';

const FIXED_DT = 1 / 60;

// Box2D sample demo (FR-PKG-006 AC-PKG-014, T8): the frame loop lives here in
// TypeScript, stepping the package world once per frame and mapping the
// plain body snapshots onto engine primitives. Zero drawing calls go into
// the package — it only integrates physics (no timer, no loop, no renderer).
export function tickDemo(world: WorldId): Body[] {
  step(world, FIXED_DT);
  return readBodies(world);
}

export default function GameDemo() {
  const world: WorldId = createWorld();
  const bodies: Body[] = tickDemo(world);
  const boxes = bodies.map((b) => (
    <View key={b.id} x={b.x} y={b.y} rotation={b.angle} />
  ));
  destroy(world);
  return <View>{boxes}</View>;
}
