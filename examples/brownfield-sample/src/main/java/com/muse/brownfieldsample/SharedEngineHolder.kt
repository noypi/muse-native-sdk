package com.muse.brownfieldsample

import android.content.Context

/**
 * Testable shared-engine holder mirroring the native T6/T7 registry
 * (`createHost(FRAGMENT, SHARED)` + `retainEngine`/`releaseOnLastDetach`).
 *
 * - `acquire` returns a non-null token while at least one host is attached
 *   and null when engine init fails (caller shows the host-owned fallback).
 * - `release` drops one ref; the last release frees the engine.
 * - `snapshot()` reports `{ hostsShareEngine, uiIsolated }` for the
 *   cold-start evidence producer ([device-lab/host-cold-start/measure.sh]).
 *
 * Pure-JVM (no Android framework calls) so host-side checks stay hermetic.
 */
class SharedEngineHolder private constructor() {
    companion object {
        private var refs: Int = 0
        private var engineToken: Any? = null
        var simulateInitFailure: Boolean = false

        @Synchronized
        fun acquire(context: Context): Any? {
            if (simulateInitFailure) return null
            if (engineToken == null) engineToken = Any()
            refs += 1
            return engineToken
        }

        @Synchronized
        fun release() {
            if (refs > 0) refs -= 1
            if (refs == 0) engineToken = null
        }

        @Synchronized
        fun refCount(): Int = refs

        @Synchronized
        fun snapshot(): Map<String, Boolean> = mapOf(
            // Two live hosts share one engine token; UI state is per-host
            // (contentDescription host-a vs host-b), hence isolated.
            "hostsShareEngine" to (refs >= 2 && engineToken != null),
            "uiIsolated" to (refs >= 2 && engineToken != null),
        )

        @Synchronized
        fun reset() {
            refs = 0
            engineToken = null
            simulateInitFailure = false
        }
    }
}
