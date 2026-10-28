// FR-M3-003 / AC-M3-005 — BottomAppBar tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (Surface, IconButton, Text, Icon) with token-role-only props.

export interface BottomAppBarProps {
  title?: string;
  content?: unknown;
  [key: string]: unknown;
}

export function BottomAppBar(props?: BottomAppBarProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'BottomAppBar',
    props: next,
    content: [
      { type: 'Surface', props: { tone: 'surface-container' } },
      { type: 'IconButton', props: { tone: 'surface-container' } },
      { type: 'Icon', props: { name: 'menu', color: 'on-surface' } },
      { type: 'Text', props: { variant: 'title-medium', color: 'on-surface' } },
    ],
  };
}
