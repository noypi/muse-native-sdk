// FR-M3-003 / AC-M3-005 — Carousel tier-3 composite (T2).
// `{ type, props }` descriptor factory; `props` echoes the caller props
// untouched and composition lives in sibling `content`. Nests tier-1/2 only
// (Scroll, Stack, Image, Surface) with token-role-only props.

export interface CarouselProps {
  index?: number;
  content?: unknown;
  [key: string]: unknown;
}

export function Carousel(props?: CarouselProps) {
  const next = props === undefined ? {} : props;
  return {
    type: 'Carousel',
    props: next,
    content: [
      { type: 'Scroll', props: { orientation: 'horizontal' } },
      { type: 'Stack', props: { direction: 'row' } },
      { type: 'Image', props: { contentFit: 'cover' } },
      { type: 'Surface', props: { tone: 'surface' } },
    ],
  };
}
