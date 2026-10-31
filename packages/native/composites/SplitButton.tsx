// FR-M3-003 / AC-M3-005 — SplitButton tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (Button, IconButton, Icon, Divider) with token-role-only props.

export interface SplitButtonProps {
  label: string;
  enabled?: boolean;
  icon?: string;
  [key: string]: unknown;
}

export function SplitButton(props?: SplitButtonProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SplitButton',
    props: next,
    content: [
      { type: 'Button', props: { tone: 'primary', label: next.label } },
      { type: 'IconButton', props: { tone: 'primary' } },
      { type: 'Icon', props: { name: 'expand', color: 'on-primary' } },
      { type: 'Divider', props: { orientation: 'vertical', color: 'on-primary' } },
    ],
  };
}
