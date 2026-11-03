'use strict';

// T3 — demo kind map (fallback-kinds Batch 2, AC-SAMPLE-023).
//
// Demo-descriptor seam: `mapDemoKind(demoKind)` translates the four known
// demo descriptor kinds to gate-known generic comps (`lens→Image`,
// `field→TextInput`, `list→Card`, `ErrorSnackbar→SnackbarHost`). Every
// other input fails closed NAMED (`unknown demo kind '<kind>'`) — never
// substituted, never a generic. Gate Real is
// `engine/runtime/src/gallery_present.cpp :: IsKnownGalleryComponent`.

const EXACT_MAP = {
  lens: 'Image',
  field: 'TextInput',
  list: 'Card',
  ErrorSnackbar: 'SnackbarHost',
};

function mapDemoKind(demoKind) {
  if (typeof demoKind === 'string' && Object.hasOwn(EXACT_MAP, demoKind)) {
    return EXACT_MAP[demoKind];
  }
  throw new Error(`unknown demo kind '${String(demoKind)}'`);
}

module.exports = { mapDemoKind };
