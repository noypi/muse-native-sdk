'use strict';

// Sample Box2D-style physics package (FR-PKG-006, T8).
// Physics only: deterministic body integration under constant pull.
// No timer, no loop, no renderer — stepping is driven by the host
// (see examples/m3-gallery/src/game-demo.tsx), which calls step()
// once per frame from TypeScript.

const PULL_Y = -9.8;
const FLOOR_Y = 0;

let nextWorld = 1;
const worlds = new Map();

function checkWorld(world, fnName) {
  if (typeof world !== 'number' || !worlds.has(world)) {
    const err = new Error(`${fnName}: unknown world ${String(world)}`);
    err.code = 'E_WORLD';
    throw err;
  }
  return worlds.get(world);
}

// createWorld(): WorldId — open a world seeded with two bodies.
function createWorld() {
  const id = nextWorld;
  nextWorld += 1;
  worlds.set(id, {
    id,
    seq: 2,
    bodies: [
      { id: 1, x: 0, y: 10, angle: 0, dx: 1.5, dy: 0, spin: 0.2 },
      { id: 2, x: 4, y: 14, angle: 0.5, dx: -1.0, dy: 0, spin: -0.1 },
    ],
  });
  return id;
}

// step(world: WorldId, dt: number): void — advance one fixed tick.
function step(world, dt) {
  const rec = checkWorld(world, 'step');
  if (typeof dt !== 'number' || !(dt > 0)) {
    const err = new Error(`step: dt must be a positive number, got ${String(dt)}`);
    err.code = 'E_ARG';
    throw err;
  }
  for (const b of rec.bodies) {
    b.dy += PULL_Y * dt;
    b.x += b.dx * dt;
    b.y += b.dy * dt;
    b.angle += b.spin * dt;
    if (b.y < FLOOR_Y) {
      b.y = FLOOR_Y;
      b.dy = -b.dy * 0.6;
    }
  }
}

// readBodies(world: WorldId): Body[] — plain data snapshots only.
function readBodies(world) {
  const rec = checkWorld(world, 'readBodies');
  return rec.bodies.map((b) => ({ id: b.id, x: b.x, y: b.y, angle: b.angle }));
}

// destroy(world: WorldId): void — release the world handle.
function destroy(world) {
  checkWorld(world, 'destroy');
  worlds.delete(world);
}

module.exports = { createWorld, step, readBodies, destroy };
