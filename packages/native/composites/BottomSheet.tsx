// FR-M3-003 / AC-M3-005 — BottomSheet tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched. Layers via OverlayHost per RULE-M3-006 in the sibling `layer`
// key. Nests tier-1/2 only (OverlayHost, Surface, Text, Divider) with
// token-role-only props.

export interface BottomSheetProps {
  open: boolean;
  content?: unknown;
  [key: string]: unknown;
}

export function BottomSheet(props?: BottomSheetProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'BottomSheet',
    props: next,
    layer: {
      type: 'OverlayHost',
      props: { content: { type: 'Surface', props: { tone: 'surface-container-low' } } },
    },
    content: [
      { type: 'Surface', props: { tone: 'surface-container-low' } },
      { type: 'Divider', props: { orientation: 'horizontal', color: 'outline' } },
      { type: 'Text', props: { variant: 'body-medium', color: 'on-surface' } },
    ],
  };
}
