// FR-M3-003 / AC-M3-005 — NavigationBar tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (Surface, Stack, Icon, Text, Ripple) with token-role-only props.

export interface NavigationBarProps {
  selected: string;
  enabled?: boolean;
  content?: unknown;
  [key: string]: unknown;
}

export function NavigationBar(props?: NavigationBarProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'NavigationBar',
    props: next,
    content: [
      { type: 'Surface', props: { tone: 'surface-container' } },
      { type: 'Stack', props: { direction: 'row' } },
      { type: 'Icon', props: { name: 'home', color: 'on-surface' } },
      { type: 'Text', props: { variant: 'label-medium', color: 'on-surface', selected: next.selected } },
      { type: 'Ripple', props: { color: 'primary' } },
    ],
  };
}
