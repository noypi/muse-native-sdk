// FR-M3-003 / AC-M3-005 — SegmentedButton tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (Chip, Surface, Text, Ripple) with token-role-only props.

export interface SegmentedButtonProps {
  selected?: string;
  enabled?: boolean;
  [key: string]: unknown;
}

export function SegmentedButton(props?: SegmentedButtonProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SegmentedButton',
    props: next,
    content: [
      { type: 'Chip', props: { tone: 'surface' } },
      { type: 'Surface', props: { tone: 'surface' } },
      { type: 'Text', props: { variant: 'label-large', color: 'on-surface' } },
      { type: 'Ripple', props: { color: 'primary' } },
    ],
  };
}
