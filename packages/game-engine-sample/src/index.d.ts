// Generated type surface for @muse/game-engine-sample (FR-PKG-006, T8).
// Must match the manifest arity/arg kinds: createWorld/0, step/2,
// readBodies/1, destroy/1.

export type WorldId = number;

export interface Body {
  id: number;
  x: number;
  y: number;
  angle: number;
}

export function createWorld(): WorldId;
export function step(world: WorldId, dt: number): void;
export function readBodies(world: WorldId): Body[];
export function destroy(world: WorldId): void;
