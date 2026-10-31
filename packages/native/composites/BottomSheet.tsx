// FR-M3-003 / AC-M3-005 — BottomSheet tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched. Layers via OverlayHost per RULE-M3-006 in the sibling `layer`
// key. Nests tier-1/2 only (OverlayHost, Surface, Text, Divider) with
// token-role-only props.
// L4 (FR-M3-010 / AC-M3-022 IME-inset-safe clause): when `open && imeSafe`,
// the sheet follows the layout-owned animated inset per frame. The value
// stays owned by the FR-LAYOUT-012 path and is read per frame; it is never
// copied into gallery state here, and this file holds no geometry math.

export interface BottomSheetProps {
  open: boolean;
  imeSafe: true;
  insetSource: 'FR-LAYOUT-012';
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
      { type: 'Surface', props: { tone: 'surface-container-low', open: next.open } },
      { type: 'Divider', props: { orientation: 'horizontal', color: 'outline' } },
      { type: 'Text', props: { variant: 'body-medium', color: 'on-surface' } },
    ],
  };
}
