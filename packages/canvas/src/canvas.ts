// T6 composition root for T5 contract (AC-CANVAS-005 / FR-CANVAS-001).
// Plain TypeScript in a .tsx host (no JSX syntax): maps retained scene
// descriptors onto DrawCmds using EXISTING PrimitiveKinds only, deterministically
// per t with no wall-clock reads. Unknown kinds fail closed named.

// Single canonical error: defined in recorder.ts, re-exported here so cross-module instanceof holds.
import { CanvasUnknownKindError } from './recorder.ts';
export { CanvasUnknownKindError };

export interface Descriptor {
  verb?: string;
  kind: string;
  [field: string]: unknown;
}

export interface DrawCmd {
  kind: string;
  [field: string]: unknown;
}

const KNOWN_KINDS = new Set([
  'Rect',
  'RoundedRect',
  'Image',
  'Text',
  'StateLayer',
  'Ripple',
  'ElevationShadow',
  'CircleArc',
  'VectorIcon',
  'ShapedText',
  'OverlayScrim',
]);

export function emitFrame(scene: Descriptor[], t: number): DrawCmd[] {
  return scene.map((d) => {
    if (!KNOWN_KINDS.has(d.kind)) {
      throw new CanvasUnknownKindError(String(d.kind));
    }
    return { ...d, t };
  });
}
