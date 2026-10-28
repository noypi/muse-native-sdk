package com.muse.devicelab

import android.view.View

/**
 * Kotlin binding for the T2 JNI bridge (`engine/runtime` jni_bridge).
 *
 * The native side exposes only opaque handles (`Runtime_create` /
 * `Runtime_destroy`) plus `JNI_OnLoad`; no engine type crosses the JNI
 * boundary. Handles are observed here as [Long] (a pointer-sized slot).
 */
object EngineHost {

    init {
        // Loads libRuntime.so built for both ABIs under NDK r27 (T3 gate).
        System.loadLibrary("Runtime")
    }

    // JNI bridge entry points: opaque lifecycle, no engine types.
    // Native names: Java_com_muse_devicelab_EngineHost_nativeCreate, etc.
    private external fun nativeCreate(): Long
    external fun nativeDestroy(handle: Long)

    /**
     * Creates the engine-backed host via Runtime_create. Returns 0 on
     * failure (fail-closed; never throws across the boundary).
     */
    fun create(): Long = try {
        nativeCreate()
    } catch (_: UnsatisfiedLinkError) {
        0L
    }

    /** Destroys the host via Runtime_destroy; a zero handle is a no-op. */
    fun destroy(handle: Long) {
        if (handle == 0L) return
        try {
            nativeDestroy(handle)
        } catch (_: UnsatisfiedLinkError) {
            // Fail-closed: nothing to release without the JNI bridge.
        }
    }

    /** Attaches the engine surface to the host view (no-op stub for T4). */
    fun attachSurface(handle: Long, view: View) {
        if (handle == 0L) return
        view.setTag(com.muse.devicelab.R.id.engine_handle_tag, handle)
    }
}
