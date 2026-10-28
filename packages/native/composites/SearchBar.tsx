// FR-M3-003 / AC-M3-005 — SearchBar tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (TextInput, Surface, Icon) with token-role-only props.

export interface SearchBarProps {
  value?: string;
  placeholder?: string;
  enabled?: boolean;
  [key: string]: unknown;
}

export function SearchBar(props?: SearchBarProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'SearchBar',
    props: next,
    content: [
      { type: 'TextInput', props: { tone: 'surface-container-high' } },
      { type: 'Surface', props: { tone: 'surface-container-high' } },
      { type: 'Icon', props: { name: 'search', color: 'on-surface' } },
    ],
  };
}
