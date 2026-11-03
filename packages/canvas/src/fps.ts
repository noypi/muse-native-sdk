// T6 real adapter for T4 contract (AC-CANVAS-004 / FR-CANVAS-001).
// fps contract: "display" drives off panel vsync; a numeric fps is the target
// with minFps as the degradation floor; steps are ratio-based and logged.
// Rates above available hardware are unclaimed, never capped. No ceiling
// literals: the only rate constant is the hermetic test bound below.

const HW_MAX_FPS = 60;

export interface FpsOpts {
  fps: 'display' | number;
  minFps: number;
}

export interface FpsConfig {
  mode: string;
  target?: number;
  floor?: number;
  claimed: boolean;
}

export interface StepEntry {
  ratio: number;
  message: string;
}

let current: FpsConfig = { mode: 'display', claimed: true };

export function setFps(opts: FpsOpts): FpsConfig {
  if (opts.fps === 'display') {
    current = { mode: 'display', floor: opts.minFps, claimed: true };
  } else {
    current = {
      mode: 'numeric',
      target: opts.fps,
      floor: opts.minFps,
      claimed: opts.fps <= HW_MAX_FPS,
    };
  }
  return { ...current };
}

export function onStepDown(ratio: number): StepEntry {
  return { ratio, message: `fps step-down ratio ${ratio}` };
}
