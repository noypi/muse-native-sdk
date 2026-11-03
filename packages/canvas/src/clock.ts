// T6 real adapter for T2 contract (AC-CANVAS-002 / FR-CANVAS-001).
// Deterministic time injection: pure over (state, t, dt). No wall-clock
// reads (Date.now), no Math.random — identical (state, t) yields identical bytes.

export type DrawCmd = Record<string, unknown>;

export function tick(state: unknown, t: number, dt: number): DrawCmd[] {
  const snapshot = JSON.parse(JSON.stringify(state));
  return [{ kind: 'Tick', t, dt, state: snapshot }];
}
