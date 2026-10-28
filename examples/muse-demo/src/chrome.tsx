// T9 — demo chrome docking the persistent speaker (AC-SAMPLE-037).
// `<DemoChrome/>` renders the speaker docked in a top bar on every page, ahead
// of the page content: visible without scrolling and in-flow, so it never
// covers interactive content (Lens canvas, Field, List rows, manual form).
// Strictly presentational — no proxy call here or in `./speaker`.

import type { ReactNode } from 'react';
import { View, Text } from '@muse/native';
import { Speaker } from './speaker';

export type DemoChromeProps = {
  children?: ReactNode;
  page: 'onboarding' | 'main';
};

// DemoChrome — top-docked speaker bar plus the page below. The bar renders
// first (header position, in-flow layout) so the speaker is on screen without
// scrolling and overlaps no input/button; narrow viewports keep the same order
// (A-T9-2: geometry asserted at the demo reference viewport).
export function DemoChrome({ children, page }: DemoChromeProps) {
  return (
    <View>
      <View>
        <Text>{page === 'onboarding' ? 'muse-demo onboarding' : 'muse-demo'}</Text>
        <Speaker />
      </View>
      {children}
    </View>
  );
}

export default DemoChrome;
