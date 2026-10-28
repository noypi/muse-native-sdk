import { useCallback, useEffect, useRef, useState } from 'react';
import { View, Text } from '@muse/native';
import {
  annotatePhoto,
  promoteSuggestion,
  requestOnSettle,
  SUGGESTION_VIEWPORT_PX,
  MAX_SUGGESTIONS,
} from './suggestions';
import type { Polygon, AnnotateError } from './suggestions';

// Re-export the plan interface so the Lens tab's entry point is discoverable
// from the rendering module as well as from `./suggestions`.
export { annotatePhoto };

// T5 — Lens tab tap-to-mask (AC-SAMPLE-023, FR-MUSE-003 demo binding).
// Tap fires `annotatePhoto` on settle (768px viewport, memoized by
// photoId+point); the mask renders solid and up to 3 auto-suggestions render
// dashed; promoting a suggestion reuses the polygon (no re-segment call).
// The single MuseField below pins to the active mask with
// context="masked:<label>:<photoId>" and privacy="auto".

export type AnnotatorProps = {
  photoId: string;
  label: string;
  point?: { x: number; y: number };
};

function fieldContext(label: string, photoId: string): string {
  return `masked:${label}:${photoId}`;
}

function pointsText(polygon: Polygon): string {
  return polygon.map((pt) => `(${pt[0]},${pt[1]})`).join(' ');
}

export function Annotator({ photoId, label, point }: AnnotatorProps) {
  const [mask, setMask] = useState<Polygon | undefined>(undefined);
  const [suggestions, setSuggestions] = useState<Polygon[]>([]);
  const [activeIndex, setActiveIndex] = useState<number>(-1);
  const [errorCode, setErrorCode] = useState<string | undefined>(undefined);
  const cancelRef = useRef<(() => void) | undefined>(undefined);

  const tap = useCallback(
    (next: { x: number; y: number }) => {
      if (cancelRef.current) cancelRef.current();
      setErrorCode(undefined);
      cancelRef.current = requestOnSettle(
        photoId,
        next,
        (result) => {
          setMask(result.mask);
          setSuggestions(result.suggestions);
          setActiveIndex(-1);
        },
        (err: AnnotateError) => {
          setErrorCode(err.code);
        },
      );
    },
    [photoId],
  );

  useEffect(() => {
    if (point) tap(point);
    return () => {
      if (cancelRef.current) cancelRef.current();
    };
  }, [photoId, point, tap]);

  const promote = useCallback(
    (index: number) => {
      const picked = promoteSuggestion(suggestions, index);
      setMask(picked);
      setActiveIndex(index);
    },
    [suggestions],
  );

  const context = fieldContext(label, photoId);

  return (
    <View>
      <Text>{`Lens ${SUGGESTION_VIEWPORT_PX}px photo:${photoId}`}</Text>
      <View onTouchStart={() => tap({ x: 384, y: 384 })}>
        <Text>Tap to mask</Text>
      </View>
      {mask ? <Text>{`mask: ${pointsText(mask)}`}</Text> : <Text>mask: none</Text>}
      {suggestions.slice(0, MAX_SUGGESTIONS).map((polygon, index) => (
        <View key={`${photoId}:${index}`} onTouchStart={() => promote(index)}>
          <Text>{`dashed suggestion ${index + 1}${index === activeIndex ? ' (active)' : ''}: ${pointsText(polygon)}`}</Text>
        </View>
      ))}
      {errorCode ? <Text>{`error: ${errorCode}`}</Text> : null}
      <Text>{`Field context=${context} privacy=auto`}</Text>
    </View>
  );
}

export default Annotator;
