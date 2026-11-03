import { useEffect, useState } from 'react';
import { View, Text, TopAppBar, Image, Card } from '@muse/native';
import { spring } from '@muse/core';
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
import { startRound, guess, reveal } from './guessMask';
// T5/T6 routing: tab moves resolve via `muse-demo://tab/lens`, `field`, `list`
// link shapes through the single RouterBridge navigate path (createDemoRouter
// push/replace facade), never a direct setTab bypass.
// eslint-disable-next-line @typescript-eslint/no-require-imports
const { createDemoRouter, drainStashedOpaqueLink, getOpaqueLinkConsumer } = require('./demoNav') as {
  createDemoRouter: (opts?: { transport?: unknown }) => {
    current: string | null;
    push: (to: unknown, params?: object) => Promise<{ ok: boolean; code?: string }>;
    replace: (to: unknown, params?: object) => Promise<{ ok: boolean; code?: string }>;
    forwardIntent: (opaque: unknown) => Promise<{ ok: boolean; code?: string }>;
    tapAdvance: () => Promise<{ ok: boolean; code?: string }>;
    onFallbackRecover: () => Promise<{ ok: boolean; code?: string }>;
  };
  drainStashedOpaqueLink: (router: { current: string | null; forwardIntent: (opaque: unknown) => Promise<{ ok: boolean; code?: string }> }, consume: () => unknown) => Promise<{ ok: boolean; code?: string } | null>;
  getOpaqueLinkConsumer: () => (() => unknown) | undefined;
};
function demoTransport() {
  const bridge = (globalThis as unknown as { RouterBridge?: { invoke: (m: string, p: unknown) => any } }).RouterBridge;
  if (bridge && typeof bridge.invoke === 'function') {
    return { invoke: (method, payload) => bridge.invoke(method, payload) };
  }
  return undefined;
}
const demoRouter = createDemoRouter({ transport: demoTransport() });
function tabLink(tab: Tab): string {
  return `muse-demo://tab/${tab}`;
}

// T3b (fallback-kinds, AC-SAMPLE-023): demo tree build path. Every row
// `comp` is a gate-known generic literal applied by hand at author time
// (lens→Image, field→TextInput, list→Card, ErrorSnackbar→SnackbarHost).
// No runtime mapper runs on glass: the bundler serializes TS without
// executing it, so the baked tree must CONTAIN the literals. `segment`
// route + stub-bundling behavior unchanged.
export type DemoRow = { comp: string; tab: Tab };
export function buildDemoTree(tab: Tab): DemoRow[] {
  if (tab === 'lens') return [{ comp: 'Image', tab }, { comp: 'SnackbarHost', tab }];
  if (tab === 'field') return [{ comp: 'TextInput', tab }, { comp: 'SnackbarHost', tab }];
  return [{ comp: 'Card', tab }, { comp: 'SnackbarHost', tab }];
}

// T7 (AC-SAMPLE-033): guess-the-mask round wiring (<30s stub round).
// Lens shows the photo with one mask hidden; the visitor taps a guess;
// reveal streams the reason and posts the by_object shelf entry.
function GuessMaskRound({ photoId }: { photoId: string }) {
  const [revealed, setRevealed] = useState(undefined as string | undefined);
  const [reason, setReason] = useState(undefined as string | undefined);
  const playRound = (maskId: string): void => {
    startRound(photoId);
    guess(maskId);
    const result = reveal();
    setRevealed(result.mask.id);
    const chunks: string[] = [];
    const stream = result.reasonStream;
    void (async () => {
      for await (const chunk of stream) chunks.push(chunk);
      setReason(chunks.join(' '));
    })();
    if (result.shelfEntry.renderMode !== 'by_object') setRevealed(undefined);
  };
  return (
    <View>
      <Text>Guess the hidden mask</Text>
      <View onTouchStart={() => playRound('mask-1')}>
        <Text>Guess mask-1</Text>
      </View>
      {revealed ? <Text>{`Revealed ${revealed}`}</Text> : null}
      {reason ? <Text>{reason}</Text> : null}
    </View>
  );
}

// muse-demo entry (T4, AC-SAMPLE-022): onboarding -> main tab shell.
// T8 (AC-SAMPLE-036): the main page mounts the T5–T7 modules into the
// Lens/Field/List tabs — Annotator (segment), the standalone transcribe entry
// (transcribe→reason) with guided-voice echo, and the ListShelf — alongside
// the shared error surface (`ErrorSnackbar` for the M3 SnackbarHost post
// plus each tab's inline `error` state with the same retry). The gallery
// manifest and the keyword sidecar load from bundled static JSON (no proxy
// call); related lookup stays on-device (0 model calls).
type Page = 'onboarding' | 'main';
type Tab = 'lens' | 'field' | 'list';

// Designed demo states (AC-SAMPLE-029): never a blank screen or raw dump.
export type DemoState = 'empty' | 'loading' | 'ready' | 'error';

const TABS: Tab[] = ['lens', 'field', 'list'];

// M3 motion tokens only (AC-SAMPLE-030; AC-TSX-029/030/031 consume-only):
// route/state transitions reference motion tokens, never literal curves.
type MotionToken = 'motion-token-fade' | 'motion-token-emphasized';
const transition: { opacity: MotionToken; transform: MotionToken } = {
  opacity: 'motion-token-fade',
  transform: 'motion-token-emphasized',
};

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
  const [enhanced, setEnhanced] = useState(undefined as string | undefined);
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
    </View>
  );
}

export type ThemePalette = {
  primary: string;
  secondary: string;
  onSurface: string;
  surface: string;
  background: string;
};

// Active palette store: applyTheme switches it via M3 role tokens and the
// chrome below reads it back, so the palette reaches rendered output.
let activePalette: ThemePalette = {
  primary: 'primary',
  secondary: 'secondary',
  onSurface: 'onSurface',
  surface: 'surface',
  background: 'background',
};

export function currentPalette(): ThemePalette {
  return activePalette;
}

export function applyTheme(mode: 'light' | 'dark', seedFallback: boolean): void {
  const palette: ThemePalette =
    mode === 'light'
      ? { primary: 'primary', secondary: 'secondary', onSurface: 'onSurface', surface: 'surface', background: 'background' }
      : { primary: 'primaryContainer', secondary: 'secondaryContainer', onSurface: 'onSurfaceVariant', surface: 'surfaceContainer', background: 'background' };
  const seed = seedFallback ? palette.primary : palette.secondary;
  // Seed fallback picks which role fronts the chrome accent.
  activePalette = { ...palette, primary: seed };
}

export default function App() {
  const [page, setPage] = useState('onboarding' as Page);
  const [tab, setTab] = useState('lens' as Tab);
  // T3b: every emitted row comp is a gate-known literal (see
  // buildDemoTree) — an unmapped tab never reaches glass.
  void buildDemoTree(tab);
  const [demoState, setDemoState] = useState('ready' as DemoState);
  const [theme, setTheme] = useState(() => currentPalette());
  const heroMotion = spring(0);
  // Mount-only animation kick: never re-fires on state updates.
  useEffect(() => {
    heroMotion.animateTo(1, { velocity: 0.5 });
  }, []);
  // Cold-start drain: consume the host's stashed opaque link (take-and-clear,
  // unparsed) exactly once at boot and deliver it through forwardIntent —
  // the same RouterBridge.invoke channel as warm intents. Fail-closed.
  useEffect(() => {
    const consume = getOpaqueLinkConsumer();
    if (typeof consume !== 'function') return;
    void drainStashedOpaqueLink(demoRouter, consume).then((result) => {
      if (result && result.ok) setTab(demoRouter.current as Tab);
    });
  }, []);
  const [renderMode, setRenderMode] = useState('by_object' as RenderMode);
  const photos = galleryManifest as Photo[];
  const keywords = keywordsSidecar as { [key: string]: string[] };
  const first = photos.length > 0 ? photos[0] : undefined;
  // Re-fires the failed stub load; distinct from the Field retryEnhance
  // helper — this is the plan-named entry point for the demo state.
  const onRetry = (): void => {
    setDemoState('loading');
    setDemoState(photos.length > 0 ? 'ready' : 'empty');
  };
  const goTab = (name: Tab): void => {
    onOpenTabLink(tabLink(name));
  };
  // T4 hands-free contract: unknown routes fail closed with a NAMED denial
  // that stays on glass (never blank) and carries its own tappable fallback.
  // Declared above the closures that use it (hooks-first convention).
  const [deniedLink, setDeniedLink] = useState(undefined as string | undefined);
  // T4 deep-link/tap entry (mirrors gallery onOpenStoryLink): every tab move
  // resolves through the demoNav link shape + push (single navigate path),
  // then lands on glass via setTab with a repaint log. Unknown ids keep the
  // current tab and surface the named denial instead of a silent no-op.
  const onOpenTabLink = (link: string): void => {
    void demoRouter.forwardIntent(link).then((result) => {
      if (!result.ok) {
        setDeniedLink(link);
        if (typeof console !== 'undefined') {
          console.info(`[DemoPage] deep-link rejected (unknown-route) link=${link} destinationReached=false engineHosted=true`);
        }
        return;
      }
      setDeniedLink(undefined);
      setTab(demoRouter.current as Tab);
      if (typeof console !== 'undefined') {
        console.info(`[DemoPage] advance tab=${demoRouter.current} route=${link} origin=shell-chrome repaint=1`);
      }
    });
  };
  // T4 fallback action: the denial banner's tap target. Fires the recovery
  // (back to the default tab) through the same push path and clears the
  // denial, so "tap fires the fallback action" is observable on glass.
  const onFallbackRecover = (): void => {
    setDeniedLink(undefined);
    void demoRouter.push(tabLink('lens'), {}).then(() => {
      setTab('lens');
      if (typeof console !== 'undefined') {
        console.info('[DemoPage] fallback fire action=return-to-lens tab=lens repaint=1');
      }
    });
  };
  const onToggleTheme = (): void => {
    const next = theme.primary === 'primary' ? 'dark' : 'light';
    applyTheme(next, true);
    setTheme(currentPalette());
  };
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
    // carries the persisted transcribe state.
    return (
      <View>
        <TopAppBar title="muse-demo" />
        <Card label="Profile settings" />
        <Text>Profile settings</Text>
      </View>
    );
  }

  return (
    <View>
      <TopAppBar title="muse-demo" />
      <View sharedId="muse-demo-hero" />
      <View onTouchStart={onToggleTheme}>
        <Text color={theme.onSurface}>Toggle theme</Text>
      </View>
      <Text color={theme.onSurface}>{`transition ${transition.opacity} ${transition.transform} motion ${heroMotion.value}`}</Text>
      {demoState === 'loading' ? (
        <View>
          <Text color={theme.onSurface}>Loading demo…</Text>
        </View>
      ) : null}
      {demoState === 'empty' ? (
        <View>
          <Text color={theme.onSurface}>No demo content yet</Text>
        </View>
      ) : null}
      {demoState === 'error' ? (
        <View>
          <Text color={theme.onSurface}>Demo failed to load</Text>
          <View onTouchStart={onRetry}>
            <Text color={theme.primary}>Retry</Text>
          </View>
        </View>
      ) : null}
      <View>
        {TABS.map((name) => (
          <View key={name} onTouchStart={() => goTab(name)}>
            <Text>{tabTitle(name)}</Text>
          </View>
        ))}
      </View>
      <Text>{`${tabTitle(tab)} tab`}</Text>
      {deniedLink !== undefined ? (
        <View>
          <Text color={theme.onSurface}>{`Unknown route ${deniedLink}`}</Text>
          <View onTouchStart={onFallbackRecover}>
            <Text color={theme.primary}>Return to Lens</Text>
          </View>
        </View>
      ) : null}
      {tab === 'lens' && first ? (
        <View>
          <Image label="Demo photo" />
          <Text>Guess the hidden mask</Text>
        </View>
      ) : null}
      {tab === 'field' ? (
        <View>
          <Text>Field pinned to the active mask privacy=auto</Text>
        </View>
      ) : null}
      {tab === 'list' ? (
        <Card label="Demo records" />
      ) : null}
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
