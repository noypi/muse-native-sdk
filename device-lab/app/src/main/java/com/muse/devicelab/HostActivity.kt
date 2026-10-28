package com.muse.devicelab

import android.app.Activity
import android.os.Bundle
import android.widget.FrameLayout

/**
 * Single-Activity host for the device-lab producers (FR-RUNTIME-008).
 *
 * Per Q-RUNTIME-003 the scaffold is exactly one Activity; the engine is
 * hosted through the T2 JNI bridge ([EngineHost]) rather than a standalone
 * widget. Evidence always resolves to the app-specific external files dir
 * via [getExternalFilesDir] (RULE-RUNTIME-005); overrides retarget the
 * on-device path only, never the verdict.
 */
class HostActivity : Activity() {

    private var engineHandle: Long = 0L

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val root = FrameLayout(this)
        setContentView(root)
        // EngineHost owns the JNI bridge (Runtime_create / Runtime_destroy);
        // this Activity only holds the opaque handle for its lifetime.
        engineHandle = EngineHost.create()
        EngineHost.attachSurface(engineHandle, root)
    }

    /**
     * Default evidence directory: the app-specific external files dir.
     * Never a shared root; callers may retarget the path only.
     */
    fun defaultEvidenceDir(): java.io.File {
        // getExternalFilesDir(null) -> app-specific external files dir.
        return getExternalFilesDir(null) ?: filesDir
    }

    override fun onDestroy() {
        EngineHost.destroy(engineHandle)
        engineHandle = 0L
        super.onDestroy()
    }
}
