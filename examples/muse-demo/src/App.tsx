import { useEffect, useState } from 'react';
import { View, Text } from '@muse/native';
import galleryManifest from '../assets/gallery.manifest.json';
import keywordsSidecar from '../assets/keywords.sidecar.json';
import { Annotator } from './annotator';
import { ListShelf, type AnnotatedRecord, type RenderMode } from './listShelf';
import {
  enhanceFromVoice,
  lookupRelated,
  reportEnhanceError,
  type TranscribeError,
} from './transcribe';
import { EchoToggle, GuidedVoice } from './echoToggle';
import { ErrorSnackbar, InlineError, dismissSnackbarNotice, useInlineProxyError } from './errors';
import { DemoChrome } from './chrome';
import { isTranscribeOn } from './profile';
import { OnboardingPage } from './onboarding';
import { renderSpeaker } from './speaker';

// muse-demo entry (T4, AC-SAMPLE-022): onboarding -> main tab shell.
// T8 (AC-SAMPLE-036): the main page mounts the T5–T7 modules into the
// Lens/Field/List tabs — Annotator (segment), the standalone transcribe entry
// (transcribe→reason) with guided-voice echo, and the ListShelf — alongside
// the shared error surface (`<ErrorSnackbar/>` for the M3 SnackbarHost post
// plus each tab's inline `error` state with the same retry). The gallery
// manifest and the keyword sidecar load from bundled static JSON (no proxy
// call); related lookup stays on-device (0 model calls).
type Page = 'onboarding' | 'main';
type Tab = 'lens' | 'field' | 'list';

const TABS: Tab[] = ['lens', 'field', 'list'];

function tabTitle(tab: Tab): string {
  if (tab === 'lens') return 'Lens';
  if (tab === 'field') return 'Field';
  return 'List';
}

type Photo = {
  filename: string;
  gallery: string;
  album: string;
  photoId: string;
  thumbnail: string;
};

// Annotated set for the List shelf: one record per bundled photo (annotated
// records only — the gallery itself is never restructured).
function recordsFor(photos: Photo[]): { records: AnnotatedRecord[]; version: number } {
  const records = photos.map((photo, index) => ({
    id: `ann-${photo.photoId}`,
    photoId: photo.photoId,
    label: 'tap-region',
    polygon: [
      [10 + index * 8, 10],
      [60 + index * 8, 10],
      [60 + index * 8, 60],
      [10 + index * 8, 60],
    ],
    insight: 'recorded-spark-insight',
    dataVersion: 1,
  }));
  return { records, version: 1 };
}

// Standalone transcribe voice entry for the Field tab (T7 binding, T8 error
// surface). Standalone: never wraps an input Field. Selects a list entry and
// sends the enhancement via transcribe→reason; rejects `{code, retry}` per
// RULE-MUSE-007. Errors post to the SnackbarHost via `reportEnhanceError`
// and keep the same retry in the inline state below.
function FieldVoiceEntry({ entryId }: { entryId: string }) {
  const [enhanced, setEnhanced] = useState<string | undefined>(undefined);
  const inlineError = useInlineProxyError();

  const enhance = (utterance: string) => {
    // Retry re-fires the same transcribe→reason call; success clears both
    // surfaces (inline state + the posted SnackbarHost notice).
    const retryEnhance = () => {
      enhanceFromVoice(entryId, utterance).then(
        (result) => {
          inlineError.clear();
          dismissSnackbarNotice();
          setEnhanced(result.enhanced);
        },
        (err: TranscribeError) => {
          inlineError.fire(err.code, retryEnhance);
        },
      );
    };
    enhanceFromVoice(entryId, utterance).then(
      (result) => {
        inlineError.clear();
        dismissSnackbarNotice();
        setEnhanced(result.enhanced);
      },
      (err: TranscribeError) => {
        reportEnhanceError({ code: err.code, retry: retryEnhance });
        inlineError.fire(err.code, retryEnhance);
      },
    );
  };

  return (
    <View>
      <Text>{`Standalone voice entry for ${entryId}`}</Text>
      <View onTouchStart={() => enhance('make it warmer')}>
        <Text>Enhance selected entry by voice</Text>
      </View>
      {enhanced ? <Text>{enhanced}</Text> : null}
      {inlineError.error ? <InlineError error={inlineError.error} /> : null}
    </View>
  );
}

export default function App() {
  const [page, setPage] = useState<Page>('onboarding');
  const [tab, setTab] = useState<Tab>('lens');
  const [renderMode, setRenderMode] = useState<RenderMode>('by_object');
  const photos = galleryManifest as Photo[];
  const keywords = keywordsSidecar as Record<string, string[]>;
  const first = photos.length > 0 ? photos[0] : undefined;
  const shelf = recordsFor(photos);
  const related = first ? lookupRelated(first.photoId, first.filename) : [];

  // T9 (AC-SAMPLE-037): keep the docked speaker in sync with the visible page
  // and the persisted transcribe toggle. Presentational only — `renderSpeaker`
  // never issues a proxy call. Transcribe off shows off/idle (never hidden);
  // the manual tester drives the remaining turn states via `renderSpeaker`.
  useEffect(() => {
    renderSpeaker(page === 'onboarding' ? 'listening' : 'idle', {
      reduceMotion: false,
      transcribeOn: isTranscribeOn(),
    });
  }, [page]);

  if (page === 'onboarding') {
    // T6 profile-settings page (AC-SAMPLE-023): the full onboarding path
    // (MuseVoice toggle, manual-form fallback, introduce → required-check →
    // reprompt, avatar like-or-photo) is reachable here. `onDone` redirects to
    // `main`, which re-reads the persisted transcribe toggle — so the redirect
    // carries the persisted transcribe state. `<ErrorSnackbar/>` stays mounted
    // so forced-error notices posted by the page are visible.
    return (
      <DemoChrome page="onboarding">
      <View>
        <ErrorSnackbar />
        <OnboardingPage onDone={() => setPage('main')} />
      </View>
      </DemoChrome>
    );
  }

  return (
    <DemoChrome page="main">
    <View>
      <ErrorSnackbar />
      <View>
        {TABS.map((name) => (
          <View key={name} onTouchStart={() => setTab(name)}>
            <Text>{tabTitle(name)}</Text>
          </View>
        ))}
      </View>
      <Text>{`${tabTitle(tab)} tab`}</Text>
      {tab === 'lens' && first ? (
        <Annotator
          photoId={first.photoId}
          label={(keywords[first.photoId] || ['tap-region'])[0]}
        />
      ) : null}
      {tab === 'field' ? (
        <View>
          <Text>Field pinned to the active mask privacy=auto</Text>
          <FieldVoiceEntry entryId={shelf.records.length > 0 ? shelf.records[0].id : 'a1'} />
          <EchoToggle />
          <GuidedVoice
            actions={[
              { key: 'enhance', label: 'Enhance selected entry' },
              { key: 'related', label: related.length > 0 ? related[0].label : 'Related' },
            ]}
            onAction={() => undefined}
          />
        </View>
      ) : null}
      {tab === 'list' ? (
        <ListShelf
          records={shelf.records}
          dataVersion={shelf.version}
          renderMode={renderMode}
          onRenderMode={setRenderMode}
        />
      ) : null}
      {first ? (
        <View>
          <Text>{first.filename}</Text>
          <Text>{`gallery: ${first.gallery}`}</Text>
          <Text>{`keywords: ${(keywords[first.photoId] || []).join(', ')}`}</Text>
        </View>
      ) : null}
    </View>
    </DemoChrome>
  );
}
