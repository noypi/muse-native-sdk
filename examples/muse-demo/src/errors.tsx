// T8 — proxy error taxonomy surfacing (AC-SAMPLE-036, AC-SAMPLE-029,
// FR-MUSE-002 / RULE-MUSE-007).
// `surfaceProxyError(code, retry)` posts a designed (never raw) message to the
// M3 SnackbarHost plus the owning component's inline `error` state, both
// offering the SAME retry action. Retry re-fires the exact failed call; a
// failed retry keeps both surfaces with retry still offered (never a blank
// screen). Simultaneous errors stay independent per owner inline while the
// SnackbarHost shows the latest (A-T8-2).

import { useEffect, useState } from 'react';
import { View, Text, SnackbarHost } from '@muse/native';

export type ProxyErrorCode = 'auth' | 'upstream' | 'timeout' | 'schema';

export type ProxyErrorSurface = {
  code: ProxyErrorCode;
  retry: () => void;
};

// Designed messages per code — the only text ever rendered for proxy errors.
// Raw exception text and upstream payloads are never shown (RULE-MUSE-007).
const ERROR_MESSAGES: Record<ProxyErrorCode, string> = {
  auth: 'Sign-in needed. Check access and try again.',
  upstream: 'Service unavailable. Try again.',
  timeout: 'Request timed out. Try again.',
  schema: 'Unexpected response. Try again.',
};

const VALID_CODES: ReadonlySet<string> = new Set([
  'auth',
  'upstream',
  'timeout',
  'schema',
]);

export function isProxyErrorCode(value: unknown): value is ProxyErrorCode {
  return typeof value === 'string' && VALID_CODES.has(value);
}

export function errorMessageFor(code: ProxyErrorCode): string {
  return ERROR_MESSAGES[code];
}

// --- SnackbarHost post store (latest wins, A-T8-2) ----------------------------

type Notice = { id: number; code: ProxyErrorCode; retry: () => void };

let nextId = 1;
let current: Notice | undefined;
const listeners = new Set<() => void>();

function emit(): void {
  for (const notify of [...listeners]) {
    try {
      notify();
    } catch (err) {
      void err;
    }
  }
}

function subscribeNotify(notify: () => void): () => void {
  listeners.add(notify);
  return () => {
    listeners.delete(notify);
  };
}

export function getSnackbarNotice(): Notice | undefined {
  return current;
}

function clearNotice(id: number): void {
  if (current && current.id === id) {
    current = undefined;
    emit();
  }
}

// surfaceProxyError(code, retry) — post to the M3 SnackbarHost (module store
// rendered by `<ErrorSnackbar/>`) with the same retry the owning component
// keeps in its inline `error` state. Unknown codes are ignored (typed union
// admits only the four codes; never surface raw text). The retry wrapper
// keeps both surfaces on failure: a sync throw or a rejected promise leaves
// the notice posted with retry still offered; a resolving promise clears the
// notice it posted.
export function surfaceProxyError(code: ProxyErrorCode, retry: () => void): void {
  if (!isProxyErrorCode(code)) return;
  if (typeof retry !== 'function') return;
  const id = nextId;
  nextId += 1;
  const safeRetry = () => {
    let result: unknown;
    try {
      result = retry();
    } catch (err) {
      // Sync throw: surfaces remain with retry still offered.
      void err;
      return;
    }
    if (result && typeof (result as Promise<unknown>).then === 'function') {
      (result as Promise<unknown>).then(
        () => clearNotice(id),
        (err: unknown) => {
          // Async rejection carrying a fresh {code, retry} re-surfaces it;
          // any other rejection keeps the posted notice with retry offered.
          const next = err as { code?: unknown; retry?: unknown };
          if (next && isProxyErrorCode(next.code) && typeof next.retry === 'function') {
            surfaceProxyError(next.code, next.retry as () => void);
          }
        },
      );
    }
  };
  current = { id, code, retry: safeRetry };
  emit();
}

// --- Owning-component inline state (independent per owner, A-T8-2) ------------

// useInlineProxyError() — per-owner inline `error` state. `fire` stores the
// same retry locally AND posts it to the SnackbarHost, so both surfaces offer
// the identical action. `clear` dismisses the inline state (call on success).
export function useInlineProxyError(): {
  error: ProxyErrorSurface | undefined;
  fire: (code: ProxyErrorCode, retry: () => void) => void;
  clear: () => void;
} {
  const [error, setError] = useState<ProxyErrorSurface | undefined>(undefined);
  return {
    error,
    fire: (code: ProxyErrorCode, retry: () => void) => {
      if (!isProxyErrorCode(code)) return;
      surfaceProxyError(code, retry);
      setError({ code, retry });
    },
    clear: () => setError(undefined),
  };
}

// --- Rendered surfaces (designed state only, never raw text) ------------------

export function ErrorSnackbar() {
  const [, setTick] = useState(0);
  useEffect(() => subscribeNotify(() => setTick((n) => n + 1)), []);
  const notice = getSnackbarNotice();
  if (!notice) return null;
  return (
    <View>
      <SnackbarHost message={ERROR_MESSAGES[notice.code]} />
      <View onTouchStart={notice.retry}>
        <Text>Retry</Text>
      </View>
    </View>
  );
}

export type InlineErrorProps = {
  error: ProxyErrorSurface | undefined;
};

// InlineError — the owning component's inline `error` state rendered as a
// designed message plus the same retry action posted to the SnackbarHost.
// `error` carries only `{code, retry}` so raw text can never leak through.
export function InlineError({ error }: InlineErrorProps) {
  if (!error) return null;
  return (
    <View>
      <Text>{ERROR_MESSAGES[error.code]}</Text>
      <View onTouchStart={error.retry}>
        <Text>Retry</Text>
      </View>
    </View>
  );
}

export default ErrorSnackbar;
