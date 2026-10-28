// FR-M3-003 / AC-M3-005 — SnackbarHost tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched. Layers via OverlayHost per RULE-M3-006 in the sibling `layer`
// key. Nests tier-1/2 only (OverlayHost, Surface, Text, Button) with
// token-role-only props.

export interface SnackbarHostProps {
  message?: string;
  content?: unknown;
  [key: string]: unknown;
}

export function SnackbarHost(props?: SnackbarHostProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SnackbarHost',
    props: next,
    layer: {
      type: 'OverlayHost',
      props: { content: { type: 'Surface', props: { tone: 'inverse-surface' } } },
    },
    content: [
      { type: 'Surface', props: { tone: 'inverse-surface' } },
      { type: 'Text', props: { variant: 'body-medium', color: 'inverse-on-surface' } },
      { type: 'Button', props: { tone: 'inverse-surface' } },
    ],
  };
}
