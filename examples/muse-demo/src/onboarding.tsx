import { useState } from 'react';
import { View, Text } from '@muse/native';
import {
  applyTurn,
  blankProfile,
  isTranscribeOn,
  missingRequired,
  repromptFor,
  setTranscribeOn,
  transcribeState,
  type Profile,
  type TranscribeState,
} from './profile';
import {
  photoPickAvatar,
  previewThenGenerate,
  readAvatarCache,
  writeAvatarCache,
  type Avatar,
} from './avatar';
import { InlineError, useInlineProxyError } from './errors';

// T6 — onboarding flow: profile-settings page first (AC-SAMPLE-023).
// MuseVoice listening by default with the global persisted on/off toggle
// (icon-off reveals the manual form fallback; transcribe stays off everywhere
// until re-enabled). Introduce → required-check → reprompt loop until required
// fields complete; volunteered detail is copied to optional custom info.
// Avatar resolves from the profile description (MuseImage generate-image);
// like-or-take/upload-photo completes settings. Redirect carries the persisted
// transcribe state. Resolves via the `reason` then `generate-image`
// routes/stub (turn reduction mirrors reason.onboarding; avatar mirrors
// generate-image fixtures — see proxy/lib/stub.js FIXTURES).

// RULE-MUSE-007 rejection shape (never raw text): the code names the class,
// `retry` re-fires the same call with the same arguments.
export type OnboardingErrorCode = 'auth' | 'upstream' | 'timeout' | 'schema';
export type OnboardingResult = {
  profile: Profile;
  avatar: Avatar;
  redirect: '/main';
  transcribe: TranscribeState;
};
export type OnboardingError = {
  code: OnboardingErrorCode;
  retry: () => Promise<OnboardingResult>;
};

export type { Profile, Avatar };

// completeOnboarding(transcript) — introduce → required-check → reprompt loop
// until required fields complete, then avatar generate/like-or-photo, then
// redirect with the persisted transcribe state. Avatar fetch is cache-first,
// generate-image on 404/empty, async post-redirect with progressive preview —
// generation never blocks completion: the promise resolves at redirect time
// with the current (cached or preview) avatar while generation refreshes the
// cache afterwards.
export function completeOnboarding(transcript: string[]): Promise<OnboardingResult> {
  const turns = Array.isArray(transcript) ? transcript : [];
  // T8 forced-error simulation for the reason/generate-image path
  // (AC-SAMPLE-036): a `force-<code>:` marker rejects once with that code so
  // the page can surface SnackbarHost + inline error with a working retry;
  // `retry` re-fires the same call, which resolves once the (simulated)
  // fixture recovers. Mirrors the `./suggestions` / `./transcribe` prefix
  // convention and the stub `force-<code>:` trigger in proxy/lib/stub.js.
  const forcedKey = turns.join('\n');
  const forcedMatch = /force-(auth|upstream|timeout|schema):/.exec(forcedKey);
  const forcedCode = forcedMatch ? (forcedMatch[1] as OnboardingErrorCode) : undefined;
  if (forcedCode && !consumedForce.has(forcedKey)) {
    consumedForce.add(forcedKey);
    const err: OnboardingError = {
      code: forcedCode,
      retry: () => completeOnboarding(transcript),
    };
    return Promise.reject(err);
  }
  const profile = blankProfile();
  let photoPicked: string | undefined;

  for (const utterance of turns) {
    if (typeof utterance !== 'string') continue;
    const { signal } = applyTurn(profile, utterance);
    if (signal === 'photo') {
      // Take/upload completes settings once required fields hold; otherwise
      // the required loop continues and the pick still applies at the end.
      photoPicked = 'onboarding-pick';
      if (missingRequired(profile).length === 0) break;
      continue;
    }
    if (signal === 'like') {
      // Liking the (preview) avatar completes settings once required fields
      // hold; otherwise the required loop continues.
      if (missingRequired(profile).length === 0) break;
    }
  }

  const missing = missingRequired(profile);
  if (missing.length > 0) {
    // Reprompt loop, not completion: reject per RULE-MUSE-007 so the caller
    // renders the reprompt and retries with more turns. Avatar failure must
    // still not block completion, but missing required fields must not
    // complete — including the empty-transcript boundary.
    const reprompt = repromptFor(missing);
    const err: OnboardingError = {
      code: 'schema',
      retry: () => completeOnboarding([...turns, repromptResponseHint(missing)]),
    };
    void reprompt;
    return Promise.reject(err);
  }

  if (photoPicked) {
    const avatar = photoPickAvatar(photoPicked);
    writeAvatarCache(profile, avatar);
    return Promise.resolve({
      profile,
      avatar,
      redirect: '/main',
      transcribe: transcribeState(),
    });
  }

  // Avatar never blocks completion: resolve at redirect time with the
  // cache-first value (or progressive preview), then generate post-redirect.
  const cached = readAvatarCache(profile);
  if (cached && cached.uri) {
    return Promise.resolve({
      profile,
      avatar: cached,
      redirect: '/main',
      transcribe: transcribeState(),
    });
  }
  const { current, done } = previewThenGenerate(profile);
  // Post-redirect generation continues after resolve; a late failure keeps
  // the preview rather than failing onboarding.
  done.catch(() => undefined);
  return Promise.resolve({
    profile,
    avatar: current,
    redirect: '/main',
    transcribe: transcribeState(),
  });
}

// Minimal answering turn the retry path appends so a bare retry can complete
// in manual/stub runs; real callers retry with the user's actual reprompt
// answer instead.
function repromptResponseHint(missing: ReturnType<typeof missingRequired>): string {
  if (missing.includes('displayName')) return 'My name is Sam';
  return 'My favorite topic is street photography';
}

// T8 forced-error once-only state (see completeOnboarding above).
const consumedForce = new Set<string>();

// Test/manual-reset hook: clears forced-error state.
export function clearOnboardingState(): void {
  consumedForce.clear();
}

// --- Profile-settings page (presentational shell over the same flow) --------

export type OnboardingPageProps = {
  onDone?: (result: OnboardingResult) => void;
};

export function OnboardingPage({ onDone }: OnboardingPageProps = {}) {
  const [listening, setListening] = useState<boolean>(isTranscribeOn());
  const [lines, setLines] = useState<string[]>([]);
  // T8 inline error state (AC-SAMPLE-036): designed message + the same retry
  // posted to the SnackbarHost via `fire` — never raw text. Kept separate
  // from the reprompt `notice` below so proxy errors and missing-info
  // reprompts never overwrite each other.
  const inlineError = useInlineProxyError();
  const stateRef = { current: inlineError };
  stateRef.current = inlineError;
  const [notice, setNotice] = useState<string | undefined>(undefined);

  // Route a rejection: the required-check reprompt loop (missing fields,
  // no forced-error marker) keeps the designed reprompt notice; a forced
  // proxy error (transcript carries a `force-<code>:` marker for the
  // reason/generate-image path) surfaces via SnackbarHost + inline error.
  const routeRejection = (
    transcript: string[],
    err: OnboardingError,
    retry: () => void,
  ) => {
    const forced = transcript.some(
      (line) =>
        typeof line === 'string' && /force-(auth|upstream|timeout|schema):/.test(line),
    );
    if (!forced) {
      setNotice(`missing info (${err.code}); please answer to continue`);
      return;
    }
    stateRef.current.fire(err.code, retry);
  };

  const toggleVoice = () => {
    const next = !listening;
    setListening(next);
    // Persist globally: off reveals the manual form fallback and keeps
    // transcribe off everywhere until re-enabled.
    setTranscribeOn(next);
  };

  const finish = (transcript: string[]) => {
    // Retry re-fires the same onboarding call; success clears both surfaces.
    const retryFinish = () => {
      completeOnboarding(transcript).then(
        (result) => {
          inlineError.clear();
          if (onDone) onDone(result);
        },
        (err: OnboardingError) => {
          routeRejection(transcript, err, retryFinish);
        },
      );
    };
    completeOnboarding(transcript).then(
      (result) => {
        inlineError.clear();
        if (onDone) onDone(result);
      },
      (err: OnboardingError) => {
        routeRejection(transcript, err, retryFinish);
      },
    );
  };

  return (
    <View>
      <Text>Profile settings</Text>
      <Text>{listening ? 'MuseVoice listening' : 'MuseVoice off'}</Text>
      <View onTouchStart={toggleVoice}>
        <Text>{listening ? 'voice icon on' : 'voice icon off'}</Text>
      </View>
      {listening ? null : (
        <View>
          <Text>Manual form fallback</Text>
          <Text>Name and favorite topic fields; transcribe stays off everywhere until re-enabled</Text>
        </View>
      )}
      <View onTouchStart={() => finish(lines)}>
        <Text>Introduce yourself</Text>
      </View>
      {notice ? <Text>{notice}</Text> : null}
      {inlineError.error ? <InlineError error={inlineError.error} /> : null}
    </View>
  );
}

export default OnboardingPage;
