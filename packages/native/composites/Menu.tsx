// FR-M3-003 / AC-M3-005 — Menu tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched. Layers via OverlayHost per RULE-M3-006 in the sibling `layer`
// key with a tier-1/2 `anchor`. Nests tier-1/2 only (OverlayHost, Surface,
// ListItem, Text) with token-role-only props.

export interface MenuProps {
  open: boolean;
  anchor?: unknown;
  content?: unknown;
  [key: string]: unknown;
}

export function Menu(props?: MenuProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Menu',
    props: next,
    layer: {
      type: 'OverlayHost',
      props: { content: { type: 'Surface', props: { tone: 'surface-container' } } },
    },
    anchor: { type: 'Text', props: { variant: 'body-medium', color: 'on-surface' } },
    content: [
      { type: 'Surface', props: { tone: 'surface-container' } },
      { type: 'ListItem', props: { tone: 'surface-container' } },
      { type: 'Text', props: { variant: 'body-large', color: 'on-surface' } },
    ],
  };
}
