// FR-M3-003 / AC-M3-005 — TopAppBar tier-3 composite (T2).
// Descriptor factory keeping the `{ type, props }` shape: `type` equals the
// composite name and `props` echoes the caller props untouched. Composition
// lives in the sibling `content` key so echoed props stay exactly the
// caller's props. Nested descriptors are tier-1/2 only (Surface, Text, Icon,
// Divider) with token-role-only props (no literal colour/dimension).
// The `size` prop (small|medium|large) selects a distinct title style per
// documented configuration (ASSUMPTION C2: variants are prop values).

export interface TopAppBarProps {
  title?: string;
  size?: 'small' | 'medium' | 'large';
  navigationIcon?: string;
  actions?: unknown;
  [key: string]: unknown;
}

const TITLE_VARIANT: Record<string, string> = {
  small: 'title-large',
  medium: 'headline-small',
  large: 'headline-medium',
};

export function TopAppBar(props?: TopAppBarProps) {
  const next = props === undefined ? {} : props;
  const size = typeof (next as Record<string, unknown>).size === 'string'
    ? (next as Record<string, unknown>).size as string
    : 'small';
  return {
    type: 'TopAppBar',
    props: next,
    content: [
      { type: 'Surface', props: { tone: 'surface' } },
      { type: 'Text', props: { variant: TITLE_VARIANT[size] ?? TITLE_VARIANT.small, color: 'on-surface' } },
      { type: 'Icon', props: { name: 'menu', color: 'on-surface' } },
      { type: 'Divider', props: { orientation: 'horizontal', color: 'outline' } },
    ],
  };
}
