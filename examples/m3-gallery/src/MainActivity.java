package com.muse.host;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Bundle;
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

    /** Broadcast action the proof pages through (same entry as the tap path). */
    static final String ACTION_NATIVE_ADVANCE_PAGE = "muse.native.NATIVE_ADVANCE_PAGE";

    /** Receiver for the proof's advance broadcast; same path as the tap entry. */
    private final BroadcastReceiver advanceReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (ACTION_NATIVE_ADVANCE_PAGE.equals(intent != null ? intent.getAction() : null)
                    && engineHandle != 0) {
                nativeAdvancePage(engineHandle);
                nativeRenderFrame(engineHandle);
            }
        }
    };

    /** Instantiate the engine tree from raw tree bytes. Returns opaque handle or 0. */
    private static native long nativeLoadTree(byte[] treeBytes);

    /** Attach the ANativeWindow surface for the given engine handle. */
    private static native void nativeAttachSurface(long handle, android.view.Surface surface);

    /** Render one frame for the given engine handle; no-op when handle is 0. */
    private static native void nativeRenderFrame(long handle);

    /** Advance one gallery page and render it. Returns the new page index. */
    private static native int nativeAdvancePage(long handle);

    /**
     * Dispatch a raw touch event to the native tap chain (Option A input
     * entry, BUG-M3-FIDELITY-007). Forwards raw MotionEvent fields only —
     * never a pre-decided node id; the node resolves natively from
     * laid-out bounds. Returns the claimed-and-fired node id, or -1 when
     * the gesture fired nothing (outside, drag, unclaimed).
     */
    private static native int nativeDispatchTouch(
            long handle, int action, float x, float y, int pointerId, long timestampMs);

    /** Release the attached surface without destroying the handle. */
    private static native void nativeDetachSurface(long handle);

    /** Destroy the engine handle. */
    private static native void nativeDestroy(long handle);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        // Tap routing (BUG-M3-FIDELITY-007 disposition): every touch goes to
        // the native tap chain first with raw MotionEvent fields. The old
        // `setOnClickListener -> nativeAdvancePage` page-flip is gone as a
        // tap handler — a tap that claims a button fires the button only and
        // never turns the page. Page advance survives solely as the
        // no-claim fallback below (dispatch returned -1: outside every
        // button, or a drag the arena did not claim as a tap), so a tap
        // cannot both fire and flip.
        surfaceView.setOnTouchListener((v, event) -> {
            if (engineHandle == 0) {
                return false;
            }
            int action = event.getActionMasked();
            int index = event.getActionIndex();
            int claimed = nativeDispatchTouch(
                    engineHandle,
                    action,
                    event.getX(index),
                    event.getY(index),
                    event.getPointerId(index),
                    event.getEventTime());
            if (action == MotionEvent.ACTION_UP) {
                if (claimed < 0) {
                    nativeAdvancePage(engineHandle);
                }
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
    protected void onResume() {
        super.onResume();
        IntentFilter filter = new IntentFilter(ACTION_NATIVE_ADVANCE_PAGE);
        if (Build.VERSION.SDK_INT >= 33) {
            // Trigger is `adb shell am broadcast` from outside the app, so the
            // receiver must be exported to receive it.
            registerReceiver(advanceReceiver, filter, Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(advanceReceiver, filter);
        }
    }

    @Override
    protected void onPause() {
        try {
            unregisterReceiver(advanceReceiver);
        } catch (IllegalArgumentException ignored) {
            // Receiver was not registered (e.g. paused without resume).
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (engineHandle != 0) {
            nativeDetachSurface(engineHandle);
            nativeDestroy(engineHandle);
            engineHandle = 0;
        }
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

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        // Attach the native window, then drive one frame through the bridge.
        if (engineHandle != 0) {
            nativeAttachSurface(engineHandle, holder.getSurface());
            nativeRenderFrame(engineHandle);
        }
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (engineHandle != 0) {
            nativeAttachSurface(engineHandle, holder.getSurface());
            nativeRenderFrame(engineHandle);
        }
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        // Release the native window before the framework frees it (the
        // presenter never outlives the host's invalidation call); the tree
        // handle itself survives for the next surface.
        if (engineHandle != 0) {
            nativeDetachSurface(engineHandle);
        }
    }
}
