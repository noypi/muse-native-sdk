import { useMemo } from 'react';
import { View, Text } from '@muse/native';
import type { Polygon } from './suggestions';

// T5 — MuseList shelf for the muse-demo photo annotator (AC-SAMPLE-023,
// FR-MUSE-003 demo binding). Shelves annotated records only (never the
// gallery); renderMode by_object/by_photo/executive_summary restructures the
// annotated set alone, memoized by (dataVersion + renderMode).

export type RenderMode = 'by_object' | 'by_photo' | 'executive_summary';

export type AnnotatedRecord = {
  id: string;
  photoId: string;
  label: string;
  polygon: Polygon;
  insight: string;
  dataVersion: number;
};

export type ShelfGroup = {
  key: string;
  title: string;
  records: AnnotatedRecord[];
};

export type ShelfView = {
  summary: string;
  groups: ShelfGroup[];
};

const RENDER_MODES: RenderMode[] = ['by_object', 'by_photo', 'executive_summary'];

export function isRenderMode(value: unknown): value is RenderMode {
  return (
    value === 'by_object' || value === 'by_photo' || value === 'executive_summary'
  );
}

// Pure grouping over annotated records only. The gallery manifest is never
// consulted here, so a renderMode switch cannot restructure the gallery.
export function groupAnnotatedRecords(
  records: AnnotatedRecord[],
  renderMode: RenderMode,
): ShelfView {
  const annotated = Array.isArray(records) ? records : [];
  if (renderMode === 'by_photo') {
    const byPhoto = new Map<string, AnnotatedRecord[]>();
    for (const record of annotated) {
      const list = byPhoto.get(record.photoId) || [];
      list.push(record);
      byPhoto.set(record.photoId, list);
    }
    return {
      summary: `${annotated.length} annotations across ${byPhoto.size} photos`,
      groups: [...byPhoto.entries()].map(([photoId, items]) => ({
        key: `photo:${photoId}`,
        title: photoId,
        records: items,
      })),
    };
  }
  if (renderMode === 'by_object') {
    const byLabel = new Map<string, AnnotatedRecord[]>();
    for (const record of annotated) {
      const list = byLabel.get(record.label) || [];
      list.push(record);
      byLabel.set(record.label, list);
    }
    return {
      summary: `${annotated.length} annotations across ${byLabel.size} objects`,
      groups: [...byLabel.entries()].map(([label, items]) => ({
        key: `object:${label}`,
        title: label,
        records: items,
      })),
    };
  }
  return {
    summary: `${annotated.length} annotations`,
    groups: [
      {
        key: 'executive_summary',
        title: 'Summary',
        records: annotated,
      },
    ],
  };
}

// Memo key for the shelf: (dataVersion + renderMode). dataVersion must bump
// whenever the annotated set changes so a stale grouping is never reused.
export function shelfMemoKey(dataVersion: number, renderMode: RenderMode): string {
  return `${dataVersion}:${renderMode}`;
}

export type ListShelfProps = {
  records: AnnotatedRecord[];
  dataVersion: number;
  renderMode: RenderMode;
  onRenderMode?: (mode: RenderMode) => void;
};

export function ListShelf({ records, dataVersion, renderMode, onRenderMode }: ListShelfProps) {
  const view = useMemo(
    () => groupAnnotatedRecords(records, renderMode),
    // Memoized by (dataVersion + renderMode): the annotated records array is
    // replaced (new identity) on every dataVersion bump by contract.
    // eslint-disable-next-line react-hooks/exhaustive-deps
    [shelfMemoKey(dataVersion, renderMode)],
  );

  return (
    <View>
      <Text>{`List shelf dataVersion=${dataVersion} mode=${renderMode}`}</Text>
      {RENDER_MODES.map((mode) => (
        <View key={mode} onTouchStart={() => onRenderMode && onRenderMode(mode)}>
          <Text>{mode}</Text>
        </View>
      ))}
      <Text>{view.summary}</Text>
      {view.groups.map((group) => (
        <View key={group.key}>
          <Text>{group.title}</Text>
          {group.records.map((record) => (
            <Text key={record.id}>{`${record.label} ${record.photoId} ${record.insight}`}</Text>
          ))}
        </View>
      ))}
    </View>
  );
}

export default ListShelf;
