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
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsAnimation;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;
import java.util.List;
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

    /**
     * Touch-slop mirror for the page-flip fallback gate below. Sole source
     * of truth is {@code runtime::touch_detail::kTouchSlopPx} (plan A2,
     * 8px); this literal must track it — the native slop verdict and this
     * Java gate must agree, otherwise a scroll could page-flip or an
     * outside tap could go silent.
     */
    private static final float TOUCH_SLOP_PX = 8.0f;

    /** DOWN anchor of the active gesture, for the drag-vs-outside gate. */
    private float downX = 0f;
    private float downY = 0f;
    private int downPointerId = -1;

    /**
     * Focusable input view backing the TextInput widget (tier-2 native
     * component, {@code packages/native/textinput.js}). Plain focusable
     * {@link View} host — deliberately not an editor subclass — so the
     * InputMethodManager gains a showable {@code com.muse.host} target.
     * Sized 1x1 and never touch-consuming, so the native tap chain stays
     * first (A3).
     */
    private View inputViewTextInput;

    /**
     * Focusable input view backing the MuseField field surface
     * ({@code packages/intelligence/lib/field.js}). Same host form as the
     * TextInput view above: a plain focusable {@link View}, not an editor
     * subclass. Both views share one 1x1 host parent and are focused
     * together on the L7 probe tap path.
     */
    private View inputViewMuseField;

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

    /** Feed one animated IME frame into the typed inset bus (deps.insets.imeBottom px). */
    private static native void nativeSetImeBottom(long handle, float imeBottomPx);

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
        // outside-tap fallback below (dispatch returned -1 AND the gesture
        // never dragged past slop), so a tap cannot both fire and flip and
        // a drag-release can never turn the page (AC-INPUT-020 drag-no-fire).
        surfaceView.setOnTouchListener((v, event) -> {
            if (engineHandle == 0) {
                return false;
            }
            int action = event.getActionMasked();
            int index = event.getActionIndex();
            if (action == MotionEvent.ACTION_DOWN) {
                downX = event.getX(index);
                downY = event.getY(index);
                downPointerId = event.getPointerId(index);
            }
            if (action == MotionEvent.ACTION_MOVE) {
                // MOVE batching (run decision UNSETTLED per support.md: the
                // evidence does not settle a batching/coalescing policy, so
                // this is the smallest safe choice, not a settled policy).
                // Only the action-index sample is forwarded by default,
                // which under-measures fast drags; forward each batched
                // historical sample first so the native UP slop verdict sees
                // the full gesture displacement. No native contract change:
                // history reuses the same MOVE entry.
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
            if (action == MotionEvent.ACTION_UP) {
                boolean dragged = false;
                if (event.getPointerId(index) == downPointerId) {
                    float dx = event.getX(index) - downX;
                    float dy = event.getY(index) - downY;
                    dragged = Math.hypot(dx, dy) >= TOUCH_SLOP_PX;
                }
                downPointerId = -1;
                if (claimed < 0 && !dragged) {
                    nativeAdvancePage(engineHandle);
                } else if (claimed >= 0) {
                    // L7 probe tap path (T2): the native chain already ran
                    // first and claimed a node (e.g. the Layering IME probe
                    // row) — focus the TextInput + MuseField-backed input
                    // view and hand InputMethodManager a showable target.
                    // Page-flip fallback above is untouched.
                    requestImeForProbeTap();
                }
                nativeRenderFrame(engineHandle);
            } else if (action == MotionEvent.ACTION_CANCEL) {
                downPointerId = -1;
            }
            return true;
        });
        FrameLayout root = new FrameLayout(this);
        root.addView(surfaceView);
        // D1 input-view host (T2): TextInput + MuseField-backed focusable
        // input view. A 1x1 focusable plain-View pair parked at the origin —
        // focusable so dumpsys input_method reports a focusable
        // com.muse.host target, but never clickable/touch-consuming so every
        // touch still enters the native tap chain first (A3) and the
        // outside-tap page-flip fallback above is untouched.
        inputViewTextInput = new View(this);
        inputViewTextInput.setFocusable(true);
        inputViewTextInput.setFocusableInTouchMode(true);
        inputViewTextInput.setClickable(false);
        inputViewTextInput.setContentDescription("TextInput IME probe input view");
        inputViewTextInput.setTag("TextInput");
        inputViewMuseField = new View(this);
        inputViewMuseField.setFocusable(true);
        inputViewMuseField.setFocusableInTouchMode(true);
        inputViewMuseField.setClickable(false);
        inputViewMuseField.setContentDescription("MuseField IME probe input view");
        inputViewMuseField.setTag("MuseField");
        FrameLayout.LayoutParams imeParams = new FrameLayout.LayoutParams(1, 1);
        root.addView(inputViewTextInput, imeParams);
        root.addView(inputViewMuseField, imeParams);
        setContentView(root);
        attachImeInsetListener(root);
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

    /**
     * Focus the TextInput + MuseField-backed input view after a claimed tap
     * (L7 probe row) and ask InputMethodManager to show — the IME summon
     * leg (AC-RUNTIME-009). Runs strictly after nativeDispatchTouch claimed
     * the gesture, so native-first routing is preserved.
     */
    private void requestImeForProbeTap() {
        if (inputViewTextInput != null) {
            inputViewTextInput.requestFocus();
        }
        if (inputViewMuseField != null) {
            inputViewMuseField.requestFocus();
        }
        View anchor = inputViewTextInput != null ? inputViewTextInput : inputViewMuseField;
        if (anchor != null) {
            InputMethodManager imm =
                    (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) {
                imm.showSoftInput(anchor, 0);
            }
        }
    }

    /**
     * Register the animated IME inset listener (T3, FR-LAYOUT-012 typed
     * inset bus). Each animation frame reads the animated ime() height
     * and forwards it through the single feed below into
     * deps.insets.imeBottom px — no parallel channel.
     */
    private void attachImeInsetListener(View root) {
        if (Build.VERSION.SDK_INT >= 30) {
            root.setWindowInsetsAnimationCallback(
                    new WindowInsetsAnimation.Callback(
                            WindowInsetsAnimation.Callback.DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
                        @Override
                        public WindowInsets onProgress(
                                WindowInsets insets,
                                List<WindowInsetsAnimation> runningAnimations) {
                            onInsetsFrame(
                                    (float) insets.getInsets(WindowInsets.Type.ime()).bottom);
                            return insets;
                        }
                    });
        } else {
            root.setOnApplyWindowInsetsListener((v, insets) -> {
                onInsetsFrame(
                        (float) insets.getInsets(WindowInsets.Type.ime()).bottom);
                return insets;
            });
        }
    }

    /**
     * Single inset feed: carry one animated frame height into the JNI
     * setter backing deps.insets.imeBottom (same px field the T1 anchor
     * consumes). Sole entry point — keep exactly one definition.
     */
    void onInsetsFrame(float imeBottomPx) {
        if (engineHandle != 0) {
            nativeSetImeBottom(engineHandle, imeBottomPx);
        }
    }

    /** Reads the bundled tree asset (assets/tree.mui.bin). Returns null on failure. */    private byte[] readTreeAsset() {
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
