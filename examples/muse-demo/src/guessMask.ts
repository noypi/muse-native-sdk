// T7 (AC-SAMPLE-033): guess-the-mask round against the stub proxy only.
// Stub fixtures are inline dummy data — no proxy contract changes, no
// network (Q-MUSE-004: web image search stays out), no secrets/config.
export type Mask = {
  id: string;
  label: string;
  polygon: number[][];
};

export type RevealResult = {
  mask: Mask;
  reasonStream: AsyncIterable<string>;
  shelfEntry: { renderMode: 'by_object' };
};

// Inline stub fixtures (dummy data, no proxy call).
const STUB_MASKS: Mask[] = [
  {
    id: 'mask-1',
    label: 'tap-region',
    polygon: [
      [10, 10],
      [60, 10],
      [60, 60],
      [10, 60],
    ],
  },
  {
    id: 'mask-2',
    label: 'edge-region',
    polygon: [
      [20, 20],
      [70, 20],
      [70, 70],
      [20, 70],
    ],
  },
];

const STUB_REASONS: string[] = [
  'stub reason: region matches the hidden mask',
  'stub reason: contour aligns with the photo fixture',
];

let activePhotoId: string | undefined;
let activeGuessId: string | undefined;
let activeHiddenId: string | undefined;

function findMask(maskId: string): Mask {
  const found = STUB_MASKS.find((m) => m.id === maskId);
  if (!found) {
    throw new Error(`unknown maskId: ${maskId}`);
  }
  return found;
}

export function startRound(photoId: string): void {
  if (!photoId || photoId.trim().length === 0) {
    throw new Error('startRound requires a non-empty photoId');
  }
  activePhotoId = photoId;
  activeGuessId = undefined;
  // The hidden mask is fixed per round (deterministic fixture pick), never
  // the visitor's guess: reveal reports this mask as the truth.
  const hidden = STUB_MASKS[photoId.length % STUB_MASKS.length];
  if (!hidden) {
    throw new Error('startRound: no stub masks available');
  }
  activeHiddenId = hidden.id;
}

export function guess(maskId: string): void {
  if (!activePhotoId) {
    throw new Error('guess before startRound: call startRound(photoId) first');
  }
  if (!maskId || maskId.trim().length === 0) {
    throw new Error('guess requires a non-empty maskId');
  }
  // Validate against the stub fixture set.
  findMask(maskId);
  activeGuessId = maskId;
}

async function* streamReasons(): AsyncIterable<string> {
  for (const chunk of STUB_REASONS) {
    yield chunk;
  }
}

export function reveal(): {
  mask: Mask;
  reasonStream: AsyncIterable<string>;
  shelfEntry: { renderMode: 'by_object' };
} {
  if (!activePhotoId) {
    throw new Error('reveal before startRound: call startRound(photoId) first');
  }
  if (!activeGuessId) {
    throw new Error('reveal before guess: call guess(maskId) first');
  }
  if (!activeHiddenId) {
    throw new Error('reveal before startRound: call startRound(photoId) first');
  }
  // Reveal the hidden mask for this round — not the guess — then consume
  // the round so stale state cannot leak into the next one.
  const mask = findMask(activeHiddenId);
  const reasonStream: AsyncIterable<string> = streamReasons();
  const shelfEntry: { renderMode: 'by_object' } = { renderMode: 'by_object' };
  activePhotoId = undefined;
  activeGuessId = undefined;
  activeHiddenId = undefined;
  return { mask, reasonStream, shelfEntry };
}
