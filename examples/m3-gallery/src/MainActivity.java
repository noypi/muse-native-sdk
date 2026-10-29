package com.muse.host;

import android.app.Activity;
import android.os.Bundle;
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

    /** Instantiate the engine tree from raw tree bytes. Returns opaque handle or 0. */
    private static native long nativeLoadTree(byte[] treeBytes);

    /** Attach the ANativeWindow surface for the given engine handle. */
    private static native void nativeAttachSurface(long handle, android.view.Surface surface);

    /** Render one frame for the given engine handle; no-op when handle is 0. */
    private static native void nativeRenderFrame(long handle);

    /** Advance one gallery page and render it. Returns the new page index. */
    private static native int nativeAdvancePage(long handle);

    /** Release the attached surface without destroying the handle. */
    private static native void nativeDetachSurface(long handle);

    /** Destroy the engine handle. */
    private static native void nativeDestroy(long handle);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        // Tap anywhere to page through the gallery (host input entry; all
        // pixels are still drawn natively, never a View).
        surfaceView.setOnClickListener(v -> {
            if (engineHandle != 0) {
                nativeAdvancePage(engineHandle);
            }
        });
        setContentView(surfaceView);
        byte[] treeBytes = readTreeAsset();
        if (treeBytes != null) {
            engineHandle = nativeLoadTree(treeBytes);
        }
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
