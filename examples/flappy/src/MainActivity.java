package com.muse.host;

import android.app.Activity;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
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
 *
 * T8 (amended): input forwarding + continuous frame loop. Touch input
 * follows the gallery shell shape (examples/m3-gallery/src/MainActivity.java):
 * setOnTouchListener forwards raw MotionEvent fields + coordinates to the
 * engine seam (nativeDispatchTouch — same entry, same field order as the
 * gallery shell; node resolution stays native). The render loop pumps
 * nativeRenderFrame while the surface lives (started in surfaceCreated,
 * stopped in surfaceDestroyed). No game logic in Java; the game stays in TS.
 */
public class MainActivity extends Activity implements SurfaceHolder.Callback {
    static {
        System.loadLibrary("Runtime");
    }

    private long engineHandle = 0;

    /** T8: frame-loop pump (~60Hz) while the surface lives. */
    private final Handler renderHandler = new Handler(Looper.getMainLooper());
    private boolean renderLoopRunning = false;
    private final Runnable renderTick = new Runnable() {
        @Override
        public void run() {
            if (!renderLoopRunning) {
                return;
            }
            if (engineHandle != 0) {
                try {
                    nativeRenderFrame(engineHandle);
                } catch (Throwable ignored) {
                    // Fail-closed: a frame failure never kills the loop.
                }
            }
            renderHandler.postDelayed(this, 16);
        }
    };

    /** Instantiate the engine tree from raw tree bytes. Returns opaque handle or 0. */
    private static native long nativeLoadTree(byte[] treeBytes);

    /** Attach the ANativeWindow surface for the given engine handle. */
    private static native void nativeAttachSurface(long handle, android.view.Surface surface);

    /** Render one frame for the given engine handle; no-op when handle is 0. */
    private static native void nativeRenderFrame(long handle);

    /**
     * Dispatch a raw touch event to the native tap chain (gallery input
     * entry shape). Forwards raw MotionEvent fields only — never a
     * pre-decided node id; the node resolves natively from laid-out bounds.
     * Returns the claimed-and-fired node id, or -1 when nothing fired.
     */
    private static native int nativeDispatchTouch(
            long handle, int action, float x, float y, int pointerId, long timestampMs);

    /** Release the attached surface without destroying the handle. */
    private static native void nativeDetachSurface(long handle);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        // T8 input path (gallery shell shape): every touch goes to the
        // native tap chain first with raw MotionEvent fields. MOVE history
        // is batched so the native UP slop verdict sees full displacement.
        surfaceView.setOnTouchListener((v, event) -> {
            if (engineHandle == 0) {
                return false;
            }
            int action = event.getActionMasked();
            int index = event.getActionIndex();
            if (action == MotionEvent.ACTION_MOVE) {
                int historySize = event.getHistorySize();
                for (int h = 0; h < historySize; h++) {
                    nativeDispatchTouch(
                            engineHandle,
                            action,
                            event.getHistoricalX(index, h),
                            event.getHistoricalY(index, h),
                            event.getPointerId(index),
                            event.getHistoricalEventTime(h));
                }
            }
            int claimed = nativeDispatchTouch(
                    engineHandle,
                    action,
                    event.getX(index),
                    event.getY(index),
                    event.getPointerId(index),
                    event.getEventTime());
            Log.d("MainActivity",
                    "touch x=" + event.getX(index)
                            + " y=" + event.getY(index)
                            + " claimed=" + claimed);
            if (action == MotionEvent.ACTION_UP) {
                nativeRenderFrame(engineHandle);
            }
            return true;
        });
        setContentView(surfaceView);
        byte[] treeBytes = readTreeAsset();
        if (treeBytes != null) {
            engineHandle = nativeLoadTree(treeBytes);
        }
    }

    @Override
    protected void onDestroy() {
        stopRenderLoop();
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
                out.write(buf, 0, n);
            }
            return out.toByteArray();
        } catch (IOException e) {
            return null;
        }
    }

    /** T8: start pumping nativeRenderFrame while the surface lives. */
    private void startRenderLoop() {
        if (renderLoopRunning) {
            return;
        }
        renderLoopRunning = true;
        renderHandler.post(renderTick);
    }

    /** T8: stop the frame loop (surface going away). */
    private void stopRenderLoop() {
        renderLoopRunning = false;
        renderHandler.removeCallbacks(renderTick);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Attach the render surface, then start the continuous loop.
        if (engineHandle != 0 && holder != null && holder.getSurface() != null) {
            try {
                nativeAttachSurface(engineHandle, holder.getSurface());
            } catch (Throwable ignored) {
                // Fail-closed: attach never breaks the surface path.
            }
        }
        startRenderLoop();
        nativeRenderFrame(engineHandle);
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        nativeRenderFrame(engineHandle);
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        // Stop the loop first so no frame is queued after detach; the tree
        // handle itself survives for the next surface.
        stopRenderLoop();
        if (engineHandle != 0) {
            try {
                nativeDetachSurface(engineHandle);
            } catch (Throwable ignored) {
                // Fail-closed: detach never breaks teardown.
            }
        }
    }
}
