// FR-M3-003 / AC-M3-005 — ButtonGroup tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (Button, Surface, Text) with token-role-only props.

export interface ButtonGroupProps {
  selected: string;
  enabled?: boolean;
  content?: unknown;
  [key: string]: unknown;
}

export function ButtonGroup(props?: ButtonGroupProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'ButtonGroup',
    props: next,
    content: [
      { type: 'Button', props: { tone: 'surface', selected: next.selected } },
      { type: 'Surface', props: { tone: 'surface' } },
      { type: 'Text', props: { variant: 'label-large', color: 'on-surface' } },
    ],
  };
}
