// FR-M3-003 / AC-M3-005 — Dialog tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched (open/closed configurations render distinctly via the echoed
// `open` prop). Layers via OverlayHost per RULE-M3-006: the sibling `layer`
// key carries the OverlayHost descriptor. Nests tier-1/2 only (OverlayHost,
// Surface, Text, Button) with token-role-only props.

export interface DialogProps {
  open: boolean;
  title: string;
  content?: unknown;
  actions?: unknown;
  // L3 scrim-tap-dismiss contract passthrough (FR-M3-010 / AC-M3-022):
  // the composite forwards the dismiss prop only — scrim hit-target
  // geometry stays in the L2 presenter. Either name satisfies the hook.
  onDismiss?: () => void;
  onScrimTap?: () => void;
  [key: string]: unknown;
}

export function Dialog(props?: DialogProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Dialog',
    props: next,
    layer: {
      type: 'OverlayHost',
      props: { content: { type: 'Surface', props: { tone: 'surface-container-high' } } },
    },
    content: [
      { type: 'Surface', props: { tone: 'surface-container-high' } },
      { type: 'Text', props: { variant: 'headline-small', color: 'on-surface' } },
      { type: 'Button', props: { tone: 'primary' } },
    ],
  };
}
