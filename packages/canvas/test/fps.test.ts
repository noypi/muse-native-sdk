// Batch 1 T4 RED pin — fps accounting, ratio steps logged, testable at/below 60.
// Spec: AC-CANVAS-004 (FR-CANVAS-001). Interface (plan verbatim):
//   setFps(opts: {fps: "display"|number; minFps: number}): void;
//   onStepDown(ratio: number): logEntry
// Detail: default display-vsync; numeric fps=target/minFps=floor; no 120-literals;
// >hw rates unclaimed. Stepping testable at/below 60fps.
// Hermetic: node:test + node:assert/strict only. No device, no network.
// RED by construction: ../src/fps.ts does not exist (Batch 3 owns src/).

import { describe, it } from 'node:test';
import assert from 'node:assert/strict';

// @ts-expect-error RED: implementation lands in Batch 3 (T6)
import { setFps, onStepDown } from '../src/fps.ts';

describe('T4 fps accounting', () => {
  it('default is display-vsync', () => {
    const cfg = setFps({ fps: 'display', minFps: 30 });
    assert.equal(cfg.mode, 'display');
  });

  it('numeric fps is the target and minFps is the floor', () => {
    const cfg = setFps({ fps: 60, minFps: 30 });
    assert.equal(cfg.target, 60);
    assert.equal(cfg.floor, 30);
  });

  it('step-down is ratio-based and logged (testable at/below 60)', () => {
    setFps({ fps: 60, minFps: 30 });
    const entry = onStepDown(0.5);
    assert.match(String(entry.message ?? entry), /0\.5/);
    assert.ok(entry.ratio === 0.5);
  });

  it('rates above available hardware are unclaimed, not capped', () => {
    const cfg = setFps({ fps: 90, minFps: 30 });
    assert.equal(cfg.claimed, false);
  });
});
