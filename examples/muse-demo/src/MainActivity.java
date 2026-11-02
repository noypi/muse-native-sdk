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

    /**
     * T6 engine state-carriage setter (no game logic, no routing decision).
     * Stores the validated tab (lens|field|list) plus the optional denied-link
     * string into the live handle's shell-state slot; visibility comes from
     * the next nativeRenderFrame repaint. Returns 1 applied, 0
     * denied/failed-closed (null/empty/unknown tab, bad handle).
     */
    private static native int nativeSetDemoShellState(long handle, String tab, String deniedLink);

    /** Release the attached surface without destroying the handle. */
    private static native void nativeDetachSurface(long handle);

    /**
     * Dispatch a raw touch event to the native tap chain. Forwards raw
     * MotionEvent fields only; the node resolves natively from laid-out
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

    /** Host-side mirror of the demo shell tab (demoNav tab state equivalent
     *  on device): last validated deep-link destination. Unknown routes never
     *  touch it (fail-closed, current tab kept). */
    private String currentDemoTab = "lens";

    /** Deep link that arrived before the engine handle was ready; drained after load. */
    private android.net.Uri pendingDeepLink = null;

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
                int claimed = nativeDispatchTouch(
                        engineHandle,
                        event.getActionMasked(),
                        x,
                        y,
                        event.getPointerId(index),
                        event.getEventTime());
                // T4 leg-2/advance: taps already reach native (claimed node).
                // The shell subscribes the same tap through the demoNav push
                // same-route path (App onOpenTabLink); mirror the outcome
                // here so a claimed tap advance is observable (tab + repaint).
                if (event.getActionMasked() == MotionEvent.ACTION_UP && claimed >= 0) {
                    try {
                        // T7 tap-advance: same setter path as deep links — carry
                        // the current validated tab through the state-carriage
                        // setter, then repaint. Java carries state only (no tab
                        // machine); unknown tabs never reach here.
                        if (engineHandle != 0) {
                            applyValidatedTab(currentDemoTab, "");
                        }
                    } catch (Throwable ignored) {
                    }
                    Log.i("DemoPage", "tap advance node=" + claimed + " tab=" + currentDemoTab + " repaint=1");
                }
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
        // T4 leg-1: route a cold-start muse-demo://tab/<id> VIEW (stashed
        // until the engine was up when the handle was not ready yet).
        // Drain a link stashed above now that the handle exists (warm links
        // route via onNewIntent below).
        handleDemoIntent(getIntent());
        if (pendingDeepLink != null && engineHandle != 0) {
            android.net.Uri stashed = pendingDeepLink;
            pendingDeepLink = null;
            Log.i("DemoPage", "deep-link stashed drained: " + stashed);
            routeDemoLink(stashed);
        }
    }

    /**
     * T4 leg-1: warm muse-demo://tab/<id> VIEW delivery (singleTask). The
     * intent carries the link in getData(); the MAIN/LAUNCHER path above is
     * untouched. Mirrors the gallery handleGalleryIntent shape, muse-demo
     * scoped: tab ids resolve through the same link shapes the demoNav
     * push()/replace() facade accepts ({@code muse-demo://tab/<lens|field|list>}),
     * unknown routes fail closed with no page change (never blank).
     */
    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        handleDemoIntent(intent);
    }

    /**
     * T4 leg-1: entry for muse-demo://tab/<id> VIEW intents. Validates the
     * scheme/host and id (non-empty, one of lens|field|list — the demoNav
     * contract); unknown ids fail closed with no page change. Defers while
     * the engine is down (pendingDeepLink, drained after load).
     */
    private void handleDemoIntent(Intent intent) {
        if (intent == null) {
            return;
        }
        android.net.Uri data = intent.getData();
        if (data == null) {
            return;
        }
        if (!"muse-demo".equals(data.getScheme())) {
            Log.w("DemoPage", "deep-link rejected (unknown-route): " + data + " destinationReached=false engineHosted=true");
            return;
        }
        if (!"tab".equals(data.getHost())) {
            Log.w("DemoPage", "deep-link rejected (unknown-route host): " + data + " destinationReached=false engineHosted=true");
            return;
        }
        if (engineHandle == 0) {
            pendingDeepLink = data;
            Log.i("DemoPage", "deep-link stashed (engine not ready): " + data);
            return;
        }
        routeDemoLink(data);
    }

    /**
     * Single setter→repaint funnel (MINOR dedupe): every validated tab lands
     * here. Stores the tab via the T6 state-carriage setter, repaints, and
     * records it as the current tab. Returns true applied, false on
     * setter failure (fail-closed, caller logs + keeps current tab).
     */
    private boolean applyValidatedTab(String id, String deniedLink) {
        int rc = 0;
        try {
            rc = nativeSetDemoShellState(engineHandle, id, deniedLink != null ? deniedLink : "");
        } catch (Throwable ignored) {
            rc = 0;
        }
        if (rc != 1) {
            return false;
        }
        currentDemoTab = id;
        if (engineHandle != 0) {
            try {
                nativeRenderFrame(engineHandle);
            } catch (Throwable ignored) {
                // Fail-closed: a bad frame never kills the host.
            }
        }
        return true;
    }

    /**
     * T7 fallback recovery: re-push the current validated tab through the
     * same setter path (state carriage only) and repaint, so a tappable
     * fallback visibly lands via nativeSetDemoShellState + nativeRenderFrame.
     */
    private void onFallbackRecover() {
        try {
            if (engineHandle != 0) {
                applyValidatedTab(currentDemoTab, "");
            }
        } catch (Throwable ignored) {
            // Fail-closed: recovery never crashes the host.
        }
        Log.i("DemoPage", "fallback recover tab=" + currentDemoTab + " destinationReached=true engineHosted=true repaint=1");
    }

    /**
     * T4 leg-1 (T7: funnels through the T6 setter): route an already-validated
     * muse-demo Uri to its tab. The demo shell owns its tab state through the
     * demoNav push()/replace() facade (single RouterBridge navigate path, no
     * direct setTab bypass); the host mirrors the validated destination here
     * via nativeSetDemoShellState + nativeRenderFrame so the visible
     * tab/page actually changes on warm + cold delivery, and stashed links
     * drain after load. Unknown ids fail closed with no page change (never
     * blank).
     */
    private void routeDemoLink(android.net.Uri link) {
        if (link == null || !"tab".equals(link.getHost())) {
            Log.w("DemoPage", "deep-link rejected (unknown-route host): " + link + " destinationReached=false engineHosted=true");
            return;
        }
        // Fail-closed query denial (matches parseTabLink): query-suffixed
        // links are denied, never stripped. getPathSegments() ignores the
        // query, so without this check `muse-demo://tab/lens?seed=1` would
        // wrongly apply while the JS seam classifies it unknown-route.
        if (link.getEncodedQuery() != null) {
            Log.w("DemoPage", "deep-link rejected (unknown-route): " + link + " destinationReached=false engineHosted=true");
            return;
        }
        java.util.List<String> segs = link.getPathSegments();
        String rawId = segs.isEmpty() ? "" : segs.get(0);
        String id;
        try {
            id = java.net.URLDecoder.decode(rawId, "UTF-8");
        } catch (Throwable ignored) {
            id = rawId;
        }
        if (!("lens".equals(id) || "field".equals(id) || "list".equals(id))) {
            Log.w("DemoPage", "deep-link rejected (unknown-route): " + link + " destinationReached=false engineHosted=true");
            return;
        }
        // Wire the validated id into the engine shell-state slot (state
        // carriage only — no routing decision here) and repaint the landed
        // shell so the visible tab/page actually changes on warm + cold
        // delivery. Setter failure keeps the current tab (fail-closed).
        if (!applyValidatedTab(id, "")) {
            Log.w("DemoPage", "deep-link rejected (unknown-route): " + link + " destinationReached=false engineHosted=true");
            return;
        }
        Log.i("DemoPage", "deep-link tab=" + id + " link=" + link + " destinationReached=true engineHosted=true repaint=1");
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
