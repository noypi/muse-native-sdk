// FR-M3-003 / AC-M3-005 — NavigationDrawer tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (Surface, ListItem, Icon, Text, Divider) with token-role-only props.

export interface NavigationDrawerProps {
  selected: string;
  enabled?: boolean;
  content?: unknown;
  [key: string]: unknown;
}

export function NavigationDrawer(props?: NavigationDrawerProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'NavigationDrawer',
    props: next,
    content: [
      { type: 'Surface', props: { tone: 'surface-container-low' } },
      { type: 'ListItem', props: { tone: 'surface-container-low' } },
      { type: 'Icon', props: { name: 'home', color: 'on-surface' } },
      { type: 'Text', props: { variant: 'label-large', color: 'on-surface' } },
      { type: 'Divider', props: { orientation: 'horizontal', color: 'outline' } },
    ],
  };
}
