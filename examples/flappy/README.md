# flappy

Playable flappy game demo (`examples/flappy`).

## Build

```sh
muse-native build examples/flappy
```

Or via the demos script:

```sh
./scripts/build-demos.sh flappy
```

## Run

```sh
muse-native run examples/flappy
```

## Engine import

The demo imports `@thirdparty/box2d-sample` (declared in
`examples/flappy/package.json`, so a clean checkout resolves with no manual
install step):

```ts
import { createWorld, step, readBodies, destroy } from '@thirdparty/box2d-sample';
```

Exports used:

- `createWorld` — opens the physics world each frame tick (`App.tsx` frame
  loop) and each `stepGame` transition (`src/loop.ts`).
- `step` — advances the world once per frame: `step(world, dt)` with
  `dt = 1/60` in `App.tsx`, and once per `stepGame` call in `src/loop.ts`.
- `readBodies` — returns body snapshots (`{ id, x, y, angle }`) mapped onto
  engine primitives for drawing in `App.tsx`.
- `destroy` — releases the world handle (`App.tsx` effect cleanup;
  `finally` in `src/loop.ts`).

No vendored copy of the package lives in `examples/flappy`; rendering uses
engine primitives only.
