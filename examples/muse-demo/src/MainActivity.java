package com.muse.host;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.Choreographer;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Native host Activity (Option A, Q-PACKAGING-002).
 *
 * Loads libRuntime.so, reads assets/tree.mui.bin, and calls the
 * TreeBridge JNI bridge to instantiate the engine tree and attach the
 * render surface. Rendering itself stays in engine/runtime natively.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {
    static {
        System.loadLibrary("Runtime");
    }

    private long engineHandle = 0;

    /** Documented max tree-asset size: 16 MiB (lower end of review's 16–32 MB bound). */
    private static final int MAX_TREE_BYTES = 16 * 1024 * 1024;

    /** Instantiate the engine tree from raw tree bytes. Returns opaque handle or 0. */
    private static native long nativeLoadTree(byte[] treeBytes);

    /** Render one frame for the given engine handle; no-op when handle is 0. */
    private static native void nativeRenderFrame(long handle);

    /** Attach the ANativeWindow surface for the given engine handle. */
    private static native void nativeAttachSurface(long handle, android.view.Surface surface);

    /** Release the attached surface without destroying the handle. */
    private static native void nativeDetachSurface(long handle);

    /**
     * Dispatch a raw touch event to the native tap chain. Forwards raw
     * MotionEvent values only; the node resolves natively from laid-out
     * bounds. Returns the claimed node id, or -1 when nothing fired.
     */
    private static native int nativeDispatchTouch(
            long handle, int action, float x, float y, int pointerId, long timestampMs);

    private static final String TAG = "muse-host";

    /** True while the surface lives; gates the Choreographer render loop. */
    private volatile boolean surfaceLive = false;

    /** True between onPause and onResume; gates loop restart while hidden. */
    private volatile boolean paused = false;

    /** First-frame-present log fires at most once per process. */
    private boolean firstFrameLogged = false;

    /** Continuous render loop: one native frame per vsync while surface lives. */
    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            if (!surfaceLive || paused) {
                return;
            }
            Choreographer.getInstance().postFrameCallback(frameCallback);
            if (engineHandle == 0) {
                return;
            }
            try {
                nativeRenderFrame(engineHandle);
            } catch (Throwable ignored) {
                // Fail-closed: a bad frame never kills the loop.
                return;
            }
            if (!firstFrameLogged) {
                firstFrameLogged = true;
                Log.i(TAG, "first-frame-present: engine frame rendered");
            }
        }
    };

    private void startRenderLoop() {
        if (paused || surfaceLive) {
            return;
        }
        surfaceLive = true;
        Choreographer.getInstance().postFrameCallback(frameCallback);
    }

    private void stopRenderLoop() {
        surfaceLive = false;
        try {
            Choreographer.getInstance().removeFrameCallback(frameCallback);
        } catch (Throwable ignored) {
        }
    }

    /** Opaque route string that arrived before the engine was ready; drained after load. */
    private String pendingOpaqueLink = null;

    /** Bounded opaque stash: single slot, OS intent URIs are short; oversize dropped fail-closed. */
    private static final int MAX_OPAQUE_LINK_CHARS = 2048;

    /**
     * Stash an inbound VIEW intent as an opaque string. No scheme/host/id
     * parsing here; the JS routing layer resolves the stashed string once
     * the engine is ready.
     */
    private void stashOpaqueIntent(Intent intent) {
        if (intent == null) {
            return;
        }
        String raw = null;
        try {
            raw = intent.getDataString();
        } catch (Throwable ignored) {
            raw = null;
        }
        if (raw == null || raw.isEmpty()) {
            return;
        }
        if (raw.length() > MAX_OPAQUE_LINK_CHARS) {
            return;
        }
        pendingOpaqueLink = raw;
    }

    /**
     * JS-readable drain of the stashed opaque link. Returns the stashed
     * string once, clearing the slot; never parses (AC-NAV-044 stash rule).
     * The JS routing layer resolves it via the router once ready.
     */
    String consumePendingOpaqueLink() {
        String link = pendingOpaqueLink;
        pendingOpaqueLink = null;
        return link;
    }

    /**
     * Injected delivery seam for the taken opaque string. Production wires
     * this to the JS router channel (the same {@code forwardIntent} path
     * {@code App.tsx} uses); unset means log-only. Never parses.
     */
    interface OpaqueLinkSink {
        void accept(String opaqueLink);
    }

    static volatile OpaqueLinkSink opaqueLinkSink = null;

    /**
     * Handoff into the JS routing layer. Host never parses; the stashed
     * opaque string stays available for the JS drain above. Same channel
     * serves cold-start (onCreate) and warm (onNewIntent) arrivals.
     */
    void forwardOpaqueToRouter(String opaqueLink) {
        if (opaqueLink == null || opaqueLink.isEmpty()) {
            return;
        }
        OpaqueLinkSink sink = opaqueLinkSink;
        if (sink != null) {
            try {
                sink.accept(opaqueLink);
            } catch (Throwable ignored) {
                // Fail-closed: a bad sink never crashes the host.
            }
        }
        Log.i(TAG, "opaque-intent-ready engineHosted=true");
        // Consume-once: caller passes the taken value; slot already cleared.
    }

    /** Drain the cold-start stash into the router once ready (unparsed). */
    private void drainPendingOpaqueLink() {
        if (pendingOpaqueLink == null || engineHandle == 0) {
            return;
        }
        String link = consumePendingOpaqueLink();
        forwardOpaqueToRouter(link);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        surfaceView.setOnTouchListener((v, event) -> {
            if (engineHandle == 0) {
                return false;
            }
            int index = event.getActionIndex();
            try {
                float x = event.getX(index);
                float y = event.getY(index);
                if (!Float.isFinite(x) || !Float.isFinite(y)) {
                    // Fail-closed: non-finite touch never reaches native.
                    return true;
                }
                nativeDispatchTouch(
                        engineHandle,
                        event.getActionMasked(),
                        x,
                        y,
                        event.getPointerId(index),
                        event.getEventTime());
            } catch (Throwable ignored) {
                // Fail-closed: touch never crashes the host.
            }
            return true;
        });
        setContentView(surfaceView);
        byte[] treeBytes = readTreeAsset();
        if (treeBytes != null) {
            engineHandle = nativeLoadTree(treeBytes);
        }
        // Cold-start VIEW: stash opaque, then drain unparsed now that the
        // handle exists (warm arrivals drain via onNewIntent below).
        stashOpaqueIntent(getIntent());
        drainPendingOpaqueLink();
    }

    /**
     * Warm VIEW delivery (singleTask): forward the intent as an opaque
     * string with no parsing. The MAIN/LAUNCHER path above is untouched.
     */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        stashOpaqueIntent(intent);
        drainPendingOpaqueLink();
    }

    @Override
    protected void onPause() {
        paused = true;
        try {
            Choreographer.getInstance().removeFrameCallback(frameCallback);
        } catch (Throwable ignored) {
        }
        super.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        paused = false;
        // Restart only when the surface is still attached (detach-safe).
        if (surfaceLive) {
            try {
                Choreographer.getInstance().removeFrameCallback(frameCallback);
            } catch (Throwable ignored) {
            }
            Choreographer.getInstance().postFrameCallback(frameCallback);
        }
    }

    @Override
    protected void onDestroy() {
        stopRenderLoop();
        try {
            nativeDetachSurface(engineHandle);
        } catch (Throwable ignored) {
        }
        engineHandle = 0;
        super.onDestroy();
    }

    /** Reads the bundled tree asset (assets/tree.mui.bin). Returns null on failure. */
    private byte[] readTreeAsset() {
        try (InputStream in = getAssets().open("tree.mui.bin");
                ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) != -1) {
                if (out.size() + n > MAX_TREE_BYTES) {
                    throw new IOException("tree-too-large: tree asset exceeds 16 MiB bound");
                }
                out.write(buf, 0, n);
            }
            return out.toByteArray();
        } catch (IOException e) {
            return null;
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Attach the render surface, then run the continuous loop.
        try {
            nativeAttachSurface(engineHandle, holder.getSurface());
        } catch (Throwable ignored) {
        }
        startRenderLoop();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        try {
            nativeAttachSurface(engineHandle, holder.getSurface());
        } catch (Throwable ignored) {
        }
        startRenderLoop();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        stopRenderLoop();
        try {
            nativeDetachSurface(engineHandle);
        } catch (Throwable ignored) {
        }
    }
}
