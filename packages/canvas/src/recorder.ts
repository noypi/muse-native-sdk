// T6 real adapter for T1 contract (AC-CANVAS-001 / FR-CANVAS-001).
// Retained HTML-flavored recorder: verbs map 1:1 onto EXISTING PrimitiveKinds
// from engine/runtime/include/runtime/renderer.h — no kinds defined here.
// Nothing paints during recording; unknown verbs fail closed named.

export class CanvasUnknownKindError extends Error {
  kind: string;
  constructor(kind: string) {
    super(`unknown canvas verb: ${kind}`);
    this.name = 'CanvasUnknownKindError';
    this.kind = kind;
  }
}

export interface Descriptor {
  kind: string;
  paints: number;
  [field: string]: unknown;
}

const VERB_TO_KIND: Record<string, string> = {
  fillRect: 'Rect',
  drawImage: 'Image',
  arc: 'CircleArc',
  fillText: 'ShapedText',
  save: 'StateLayer',
  restore: 'StateLayer',
  translate: 'StateLayer',
};

export function record(verb: string, payload: unknown): Descriptor {
  const kind = VERB_TO_KIND[verb];
  if (kind === undefined) {
    throw new CanvasUnknownKindError(verb);
  }
  const fields =
    payload !== null && typeof payload === 'object'
      ? (payload as Record<string, unknown>)
      : {};
  return { ...fields, kind, paints: 0 };
}
