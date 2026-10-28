import { useState } from 'react';
import { View, Text } from '@muse/native';
import galleryManifest from '../assets/gallery.manifest.json';
import keywordsSidecar from '../assets/keywords.sidecar.json';

// muse-demo entry (T4, AC-SAMPLE-022): onboarding -> main tab shell.
// Shell only — later batches fill behavior (T5 annotator, T6 onboarding,
// T7 transcribe, T8 errors, T9 speaker), so each tab renders its
// placeholder over the bundled static data. The gallery manifest and the
// keyword sidecar load from bundled static JSON (no proxy call); related
// lookup stays on-device (0 model calls).
type Page = 'onboarding' | 'main';
type Tab = 'lens' | 'field' | 'list';

const TABS: Tab[] = ['lens', 'field', 'list'];

function tabTitle(tab: Tab): string {
  if (tab === 'lens') return 'Lens';
  if (tab === 'field') return 'Field';
  return 'List';
}

export default function App() {
  const [page, setPage] = useState<Page>('onboarding');
  const [tab, setTab] = useState<Tab>('lens');
  const photos = galleryManifest as {
    filename: string;
    gallery: string;
    album: string;
    photoId: string;
    thumbnail: string;
  }[];
  const keywords = keywordsSidecar as Record<string, string[]>;
  const first = photos.length > 0 ? photos[0] : undefined;

  if (page === 'onboarding') {
    return (
      <View>
        <Text>Welcome to muse-demo</Text>
        <Text>Annotate your photos with Lens, Field, and List.</Text>
        <View onTouchStart={() => setPage('main')}>
          <Text>Get started</Text>
        </View>
      </View>
    );
  }

  return (
    <View>
      <View>
        {TABS.map((name) => (
          <View key={name} onTouchStart={() => setTab(name)}>
            <Text>{tabTitle(name)}</Text>
          </View>
        ))}
      </View>
      <Text>{`${tabTitle(tab)} tab`}</Text>
      {first ? (
        <View>
          <Text>{first.filename}</Text>
          <Text>{`gallery: ${first.gallery}`}</Text>
          <Text>{`keywords: ${(keywords[first.photoId] || []).join(', ')}`}</Text>
        </View>
      ) : null}
    </View>
  );
}
