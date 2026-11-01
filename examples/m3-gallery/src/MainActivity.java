package com.muse.host;

import android.app.Activity;
import android.content.BroadcastReceiver;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.Editable;
import android.text.TextWatcher;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.ViewTreeObserver;
import android.view.WindowInsets;
import android.view.WindowInsetsAnimation;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
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

    /** T1 (FR-STARTUP-002/AC-STARTUP-003): cold-readiness flag set on the UI-thread post-back. */
    private volatile boolean treeReady = false;

    /** T2 (FR-STARTUP-002/AC-STARTUP-003): cold-readiness flag set in surfaceCreated/surfaceChanged. */
    private volatile boolean surfaceReady = false;

    /** T2: latest surface holder for the single first-render gate (RULE-STARTUP-002). */
    private SurfaceHolder pendingHolder;

    /** T1 generation token (feeds T3): late post-backs with a stale generation are dropped. */
    private int treeLoadGeneration = 0;

    /** T1 single-owner background loader (ASSUMPTION A2, RULE-STARTUP-002). */
    private final java.util.concurrent.ExecutorService treeLoader =
            java.util.concurrent.Executors.newSingleThreadExecutor();

    /** TTFD exactly-once guard: reportFullyDrawn() is signalled at most once. */
    private boolean fullyDrawnReported = false;

    /** Last forwarded inset height; throttles the paced present (F1). */
    private float lastImeBottomPx = -1f;

    /**
     * FR-SAMPLE-014 removed the Java page-flip gate that consumed the DOWN
     * anchor: every touch goes to the native tap chain untouched, and no
     * Java gate pages anymore.
     */

    /**
     * Focusable input view backing the TextInput widget (tier-2 native
     * component, {@code packages/native/textinput.js}). Visible 1x1
     * {@link EditText} host (D1 amended, T5-retry, T2 focus fix) —
     * composition, not an editor subclass — so the InputMethodManager gains an
     * onCheckIsTextEditor/onCreateInputConnection path and a showable
     * {@code com.muse.host} target. Sized 1x1 and parked at the container
     * corner, never touch-consuming, so the native tap chain stays first
     * (A3). Stays {@link View#VISIBLE}: INVISIBLE views cannot take focus,
     * so requestFocus() would no-op and the IME would never bind.
     */
    private View inputViewTextInput;

    /**
     * Focusable input view backing the MuseField field surface
     * ({@code packages/intelligence/lib/field.js}). Same host form as the
     * TextInput view above: a visible 1x1 {@link EditText} by composition,
     * not an editor subclass. Both views share one 1x1 host parent and are
     * focused together on the L7 probe tap path.
     */
    private View inputViewMuseField;

    /**
     * T8 (BUG-M3-FIDELITY-003 / FR-M3-006): live probe field for the
     * single instrumented page ("m3-text-probe"). An
     * {@link IMEInterceptingEditText} (observability-only
     * {@link EditText} subclass) so every Gboard composition/caret action
     * on the probe field emits one {@code muse_text} JSON logcat line via
     * the native sink. Null unless the probe page is showing.
     */
    private IMEInterceptingEditText probeField;

    /**
     * T8 echo guard: while true, programmatic edits (see
     * {@link #applyRemoteEdit(String)}) suppress the
     * {@code TextWatcher}→native echo so a remote {@code setText} emits no
     * {@code text_updated} line of its own. User edits always emit.
     */
    private boolean isApplyingRemoteEdit;

    /** T8 clipboard transport for {@code clip_changed} (content + MIME). */
    private ClipboardManager clipboardManager;

    /** T8 clipboard listener; registered only while the probe field exists. */
    private final ClipboardManager.OnPrimaryClipChangedListener clipListener =
            new ClipboardManager.OnPrimaryClipChangedListener() {
                @Override
                public void onPrimaryClipChanged() {
                    reportClipboardChanged();
                }
            };

    /** Neutralized broadcast entry (AC-SAMPLE-039): kept registered, never pages. The proof uses ACTION_PROOF_ADVANCE_PAGE. */
    static final String ACTION_NATIVE_ADVANCE_PAGE = "muse.native.NATIVE_ADVANCE_PAGE";

    /**
     * Proof-only harness paging entry (BUG-RUNTIME-005 T1): harness-only,
     * unreachable from surface gestures. Receives
     * {@code muse.native.PROOF_ADVANCE_PAGE} (extras: none required;
     * forward-only — any direction extra is ignored unless +1/absent;
     * backward unsupported, no page turn) and pages via the same
     * {@code nativeAdvancePage} (navigateStory) path the shell Prev/Next
     * buttons use.
     */
    static final String ACTION_PROOF_ADVANCE_PAGE = "muse.native.PROOF_ADVANCE_PAGE";

    /**
     * Broadcast action driving the same-activity theme switch (T3a): the T3
     * re-attempt sends {@code muse.native.NATIVE_SET_THEME} with an int
     * {@code night_mode} extra (0=light, 1=dark) instead of flipping
     * {@code cmd uimode}. Same entry as the JNI switch above — the activity
     * is never recreated.
     */
    static final String ACTION_NATIVE_SET_THEME = "muse.native.NATIVE_SET_THEME";

    /** Receiver for the proof's advance broadcast; same path as the tap entry. */
    private final BroadcastReceiver advanceReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            // BUG-SAMPLE-007 T4: scoped paging — shell-chrome/deep-link/key
            // origins only (AC-SAMPLE-039/AC-M3-020). Every other
            // action/origin (including missing origin, story-surface,
            // surface tap) is a fail-closed no-op that never pages.
            if (intent == null) {
                return;
            }
            if (!ACTION_NATIVE_ADVANCE_PAGE.equals(intent.getAction())) {
                return;
            }
            String origin = intent.getStringExtra("origin");
            if (!("shell-chrome".equals(origin)
                    || "key".equals(origin)
                    || "deep-link".equals(origin))) {
                return;
            }
            if (engineHandle == 0) {
                return;
            }
            nativeAdvancePage(engineHandle);
            nativeRenderFrame(engineHandle);
        }
    };

    /** Receiver for the T3a same-page theme switch; same activity, no restart. */
    private final BroadcastReceiver themeReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (ACTION_NATIVE_SET_THEME.equals(intent != null ? intent.getAction() : null)
                    && engineHandle != 0) {
                int nightMode = intent.getIntExtra("night_mode", 0);
                nativeSetTheme(engineHandle, nightMode);
                // FR-RUNTIME-011 T4 seam theme (Stage 3 fix, MAJOR 1): apply
                // the same in-place switch through the seam. Fail-closed.
                if (seamSurfaceHandle != 0) {
                    try {
                        nativeSeamSetTheme(seamSurfaceHandle, nightMode);
                    } catch (Throwable ignored) {
                    }
                }
            }
        }
    };

    /**
     * Proof-only harness paging entry (BUG-RUNTIME-005 T1): harness-only,
     * unreachable from surface gestures. Receives
     * {@code muse.native.PROOF_ADVANCE_PAGE} (extras: none required;
     * forward-only — any direction extra is ignored unless +1/absent;
     * backward unsupported, no page turn) and pages via the same
     * {@code nativeAdvancePage} (navigateStory) path the shell Prev/Next
     * buttons use. Returns the new page index. Fail-closed on
     * {@code engineHandle == 0} (no-op, no crash). The
     * {@code NATIVE_ADVANCE_PAGE} receiver above stays scoped per
     * BUG-SAMPLE-007 (shell-chrome/key/deep-link only, fail-closed
     * otherwise); surface-gesture paging stays off (AC-SAMPLE-039).
     */
    private final BroadcastReceiver proofAdvanceReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            if (!ACTION_PROOF_ADVANCE_PAGE.equals(intent != null ? intent.getAction() : null)) {
                return;
            }
            if (engineHandle == 0) {
                return;
            }
            try {
                int direction = intent.getIntExtra("direction", 1);
                if (direction != 1) {
                    Log.w("GalleryPage", "PROOF_ADVANCE_PAGE backward unsupported (direction="
                            + direction + "); forward-only (+1), no page turn");
                    return;
                }
                // The native entry advances one page per call (forward-only);
                // this entry is forward-only (+1): one JNI advance, one page
                // marker through the navigateStory path.
                int newIndex = nativeAdvancePage(engineHandle);
                Log.i("GalleryPage", "PROOF_ADVANCE_PAGE page=" + newIndex);
            } catch (Throwable ignored) {
                // Fail-closed: harness paging never crashes the host.
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
     * Same-activity theme switch (BUG-M3-FIDELITY-002 T3a / AC-M3-010).
     * Flips the native palette override in place on the same
     * {@code MainActivity} instance and re-renders the same page — no new
     * activity, no restart, state/scroll preserved. {@code nightMode} is
     * 0=light, 1=dark; returns measured repaint frames, -1 fail-closed.
     */
    private static native int nativeSetTheme(long handle, int nightMode);

    /**
     * Dispatch a raw touch event to the native tap chain (Option A input
     * entry, BUG-M3-FIDELITY-007). Forwards raw MotionEvent fields only —
     * never a pre-decided node id; the node resolves natively from
     * laid-out bounds. Returns the claimed-and-fired node id, or -1 when
     * the gesture fired nothing (outside, drag, unclaimed).
     */
    private static native int nativeDispatchTouch(
            long handle, int action, float x, float y, int pointerId, long timestampMs);

    /**
     * FR-RUNTIME-011 T1 single Surface host seam (reference-client binding).
     * The gallery host migrates to this seam exclusively: attach/tree→handle,
     * renderFrame, raw dispatchTouch (integer/float fields only), in-place
     * setTheme/setImeBottom, idempotent detach/destroy. No View/MotionEvent
     * object crosses; no gallery concept (pages, stories, Prev/Next) appears
     * in any seam-adjacent signature. Fail-closed per PresenterCode, never
     * throws across the VM boundary.
     * Attach the evaluated tree to the opaque window handle. Returns the
     * PresenterCode ordinal as int (0=ok; non-zero=fail-closed status with a
     * logcat diagnostic, never a throw). The usable SurfaceHandle is retained
     * natively; Java keeps only the opaque slot below.
     */
    private static native int nativeSeamAttachSurface(
            long windowHandle, String rootKind, boolean unknownKind);

    /**
     * Handle getter (FR-RUNTIME-011 Stage 3 fix, adjudication A1): the T2
     * contract keeps {@code nativeSeamAttachSurface -> int32 status}, so the
     * usable SurfaceHandle is published out-of-band here — last attached
     * handle, or 0 when none. Fail-closed, never throws.
     */
    private static native long nativeSeamLastHandle();

    /**
     * Dispatch raw touch fields across the seam (action, pointerId, x/y in
     * px, event time in ms). Float/int fields only — exactly the T1 order.
     * Fail-closed when no live surface exists (diagnostic, no throw).
     */
    private static native void nativeSeamDispatchTouch(
            long surfaceHandle, int action, int pointerId,
            float xPx, float yPx, long eventTimeMs);

    /**
     * Render one frame for the seam surface. Returns the PresenterCode
     * ordinal as int (0=ok; non-zero=fail-closed status, never a throw).
     */
    private static native int nativeSeamRenderFrame(long handle);

    /**
     * Apply the theme in place (nightMode in {0,1}). Returns the
     * PresenterCode ordinal as int, never throws.
     */
    private static native int nativeSeamSetTheme(long handle, int nightMode);

    /**
     * Apply the IME bottom inset in place (px). Returns the PresenterCode
     * ordinal as int, never throws.
     */
    private static native int nativeSeamSetImeBottom(long handle, int bottomPx);

    /** Release the seam surface without destroying the handle (always kOk). */
    private static native int nativeSeamDetach(long handle);

    /** Destroy the seam surface (always kOk, safe to repeat). */
    private static native int nativeSeamDestroy(long handle);

    /**
     * Stats accessor: lifetime presented count for the surface, or -1
     * fail-closed (unknown handle, never a throw).
     */
    private static native long nativeSeamGetFrameStats(long handle);

    /**
     * T1 SurfaceHandle for this host (0 = no live seam surface). Retained
     * opaquely: Java never dereferences it, and the engine owns its lifetime
     * until detach/destroy (RULE-RUNTIME-006: exactly one presenter per
     * window). Reset to 0 on every detach/destroy path, including repeats.
     */
    private long seamSurfaceHandle = 0;

    /** Whether the seam surface above is currently attached (idempotency guard). */
    private boolean seamAttached = false;

    /**
     * FR-RUNTIME-011 T4 seam attach (Stage 3 fix, MAJOR 1 / adjudication A1):
     * attaches the surface through the T1 seam and stores the usable handle
     * via the {@code nativeSeamLastHandle} getter (the attach entry itself
     * returns only the int32 status per the T2 contract). The opaque window
     * token is derived from the Surface identity (stable per Surface object,
     * forced into the plausible-address range); it is never dereferenced as
     * a View — validation lives in the presenter. Fail-closed: any non-ok
     * status leaves {@code seamSurfaceHandle} at 0 and never throws.
     */
    private void attachSeamSurface(android.view.Surface surface) {
        if (seamAttached && seamSurfaceHandle != 0) {
            return;
        }
        try {
            long windowToken = surface != null
                    ? (((long) System.identityHashCode(surface)) & 0xffffffffL) | 0x7e110000L
                    : 0L;
            int status = nativeSeamAttachSurface(windowToken, "gallery", false);
            if (status == 0) {
                seamSurfaceHandle = nativeSeamLastHandle();
                seamAttached = seamSurfaceHandle != 0;
            } else {
                seamSurfaceHandle = 0;
                seamAttached = false;
            }
        } catch (Throwable ignored) {
            // Fail-closed: seam attach never breaks the surface path.
            seamSurfaceHandle = 0;
            seamAttached = false;
        }
    }

    /**
     * Idempotent seam detach (T1 detachSurface semantics: always kOk, safe to
     * repeat). Resets the opaque slot so a second call is a no-op. The native
     * seam detach entry lands with the remaining T1 JNI bindings; until then
     * this keeps the Java-side slot consistent with the legacy detach path
     * (which already releases swapchain-before-window per RULE-RUNTIME-006).
     */
    private void detachSeamSurface() {
        if (seamSurfaceHandle != 0) {
            nativeSeamDetach(seamSurfaceHandle);
            seamSurfaceHandle = 0;
        }
        seamAttached = false;
    }

    /**
     * Idempotent seam destroy (T1 destroySurface semantics: always kOk, safe
     * to repeat). Same slot reset as detach; teardown order stays
     * swapchain-before-window with the tree handle untouched.
     */
    private void destroySeamSurface() {
        if (seamSurfaceHandle != 0) {
            nativeSeamDestroy(seamSurfaceHandle);
            seamSurfaceHandle = 0;
        }
        seamAttached = false;
    }

    /** Release the attached surface without destroying the handle. */
    private static native void nativeDetachSurface(long handle);

    /** Feed one animated IME frame into the typed inset bus (deps.insets.imeBottom px). */
    private static native void nativeSetImeBottom(long handle, float imeBottomPx);

    /** Destroy the engine handle. */
    private static native void nativeDestroy(long handle);

    /**
     * BUG-SAMPLE-007 T6: shell-chrome Next-region predicate (forward-only
     * contract per user ruling 2026-11-01). Pure function of its inputs, no
     * JNI. Returns true iff the (x, y) sample hits the calibrated
     * shell-chrome Next region (GEOMETRY-ONLY — the claimed value is
     * IGNORED for eligibility and kept only as a logged forensic). A Next-region ACTION_UP issues exactly one
     * direct forward step; Prev-region (left-chrome) taps fall through with
     * no page and no emit — there is no step-back entry (nativeAdvancePage
     * takes no direction), so backward paging stays unpaged until a trusted
     * count plus proven wrap exist. Surface taps and outside-region samples
     * return false for any claimed value (claimed measures engine-node
     * acceptance only — the engine Next node never accepts, so Next-region
     * taps report claimed == -1 by construction).
     *
     * Tunable bounds (T7 calibrates on device 720x1600): the chrome strip
     * occupies the top band of the display and the Next control sits in the
     * right-hand band. Raw MotionEvent pixels are density-dependent: on the
     * 720x1600 reference device 1dp ~= 2px at 320dpi (xhdpi), so a 300dp
     * right-hand band maps to ~= 600px and a 100dp top strip maps to ~=
     * 200px. Evidence anchor: device tap request (660,150) dispatched as
     * (660.0,105.0) — inside the band/Strip under these defaults. Adjust
     * NEXT_X_MIN / NEXT_Y_MAX from the per-tap Log.d line below when T7
     * measures the true chrome geometry and the y offset mapping.
     */
    static boolean isShellChromeAdvance(float x, float y, int claimed) {
        // T6 tunable region constants (px on the reference device; see the
        // dp/px mapping in the method doc above — T7 owns calibration).
        final float NEXT_X_MIN = 600f;
        final float NEXT_Y_MAX = 200f;
        // GEOMETRY-ONLY (A6 calibration 2026-11-01): claimed is forensics-only,
        // never an eligibility gate — retained in the signature for logging.
        return x >= NEXT_X_MIN && y <= NEXT_Y_MAX;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // BUG-M3-FIDELITY-003 round-2: `--es page m3-text-probe` routes to the
        // TextProbePage (sharedId="m3-text-probe"). Any other value (or no
        // extra) keeps the gallery behavior below byte-identical.
        String requestedPage = getIntent() != null ? getIntent().getStringExtra("page") : null;
        boolean showTextProbe = "m3-text-probe".equals(requestedPage);
        SurfaceView surfaceView = new SurfaceView(this);
        surfaceView.getHolder().addCallback(this);
        // Tap routing (BUG-M3-FIDELITY-007 disposition, FR-SAMPLE-014):
        // every touch goes to the native tap chain first with raw
        // MotionEvent fields. The old `setOnClickListener ->
        // nativeAdvancePage` page-flip is gone as a tap handler — a tap
        // that claims a button fires the button only and never turns the
        // page. The outside-tap fallback (dispatch returned -1 AND the
        // gesture never dragged past slop) is removed: no surface gesture
        // paginates (AC-SAMPLE-039). Gallery paging lives only in the
        // shell chrome (Prev/Next buttons, ArrowLeft/ArrowRight, and
        // gallery://story/<id> deep links). A drag-release never turns
        // the page (AC-INPUT-020 drag-no-fire).
        // FR-RUNTIME-011 T4 seam touch shape: every sample below forwards raw
        // MotionEvent fields only (action, pointerId, x/y in px, event time in
        // ms) in the T1 dispatchTouch order — no View object crosses, and the
        // px values cross raw with no Java-side conversion (plan A2: the
        // boundary converts dp→px exactly once; MotionEvent already reports
        // px, so the tap path performs zero conversions). The seam entry is
        // nativeSeamDispatchTouch (declared above); the legacy claim-returning
        // entry below stays only until the seam tap-chain routing (arena fire
        // + layering scrim dismiss) lands natively — see the ASSUMPTION in
        // the T4 report. Shell-chrome paging stays geometry-only.
        surfaceView.setOnTouchListener((v, event) -> {
            if (engineHandle == 0) {
                return false;
            }
            int action = event.getActionMasked();
            int index = event.getActionIndex();
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
            // FR-RUNTIME-011 T4 seam touch (Stage 3 fix, MAJOR 1): forward the
            // same raw fields through the T1 seam in its (action, pointerId,
            // x/y px, eventTimeMs) order. Fail-closed when no seam surface is
            // live; never throws across the VM.
            if (seamSurfaceHandle != 0) {
                try {
                    nativeSeamDispatchTouch(
                            seamSurfaceHandle,
                            action,
                            event.getPointerId(index),
                            event.getX(index),
                            event.getY(index),
                            event.getEventTime());
                } catch (Throwable ignored) {
                    // Fail-closed: seam touch never breaks the tap path.
                }
            }
            // T6 calibration line (T7): raw tap geometry plus the
            // claimed node id per tap; left-chrome and surface samples are
            // expected here with no page following.
            Log.d("MainActivity",
                    "touch x=" + event.getX(index)
                            + " y=" + event.getY(index)
                            + " claimed=" + claimed);
            if (action == MotionEvent.ACTION_UP
                    && engineHandle != 0
                    && isShellChromeAdvance(event.getX(index), event.getY(index), claimed)) {
                // T6 Next-only direct driver: exactly one forward step plus
                // one frame, never via broadcast. Region-miss samples
                // (left-chrome, surface, outside, unclaimed) fall through to
                // the existing dispatch/render path below with no page.
                nativeAdvancePage(engineHandle);
                nativeRenderFrame(engineHandle);
                // FR-RUNTIME-011 T4 seam present (Stage 3 fix, MAJOR 1):
                // present the seam frame alongside the legacy frame.
                if (seamSurfaceHandle != 0) {
                    try {
                        nativeSeamRenderFrame(seamSurfaceHandle);
                    } catch (Throwable ignored) {
                    }
                }
            } else if (action == MotionEvent.ACTION_UP) {
                // FR-SAMPLE-014: no outside-tap fallback — an unclaimed
                // tap renders the frame but never advances the page.
                if (claimed >= 0) {
                    // L7 probe tap path (T2): the native chain already ran
                    // first and claimed a node (e.g. the Layering IME probe
                    // row) — focus the TextInput + MuseField-backed input
                    // view and hand InputMethodManager a showable target.
                    requestImeForProbeTap();
                }
                nativeRenderFrame(engineHandle);
                // FR-RUNTIME-011 T4 seam present (Stage 3 fix, MAJOR 1).
                if (seamSurfaceHandle != 0) {
                    try {
                        nativeSeamRenderFrame(seamSurfaceHandle);
                    } catch (Throwable ignored) {
                    }
                }
            }
            return true;
        });
        FrameLayout root = new FrameLayout(this);
        root.addView(surfaceView);
        // D1 input-view host (T2, D1 amended / T5-retry / T2 focus fix):
        // TextInput + MuseField-backed focusable input view. A 1x1 VISIBLE
        // EditText pair parked at the origin — EditText by composition (never a
        // SurfaceView/EditText subclass) so the IME gets an
        // onCheckIsTextEditor/onCreateInputConnection path; VISIBLE (never
        // INVISIBLE — invisible views cannot take focus) and
        // never clickable/touch-consuming so every touch still enters the
        // native tap chain first (A3) and the outside-tap page-flip
        // fallback above is untouched.
        inputViewTextInput = new EditText(this);
        inputViewTextInput.setFocusable(true);
        inputViewTextInput.setFocusableInTouchMode(true);
        inputViewTextInput.setClickable(false);
        inputViewTextInput.setVisibility(View.VISIBLE);
        inputViewTextInput.setContentDescription("TextInput IME probe input view");
        inputViewTextInput.setTag("TextInput");
        inputViewMuseField = new EditText(this);
        inputViewMuseField.setFocusable(true);
        inputViewMuseField.setFocusableInTouchMode(true);
        inputViewMuseField.setClickable(false);
        inputViewMuseField.setVisibility(View.VISIBLE);
        inputViewMuseField.setContentDescription("MuseField IME probe input view");
        inputViewMuseField.setTag("MuseField");
        FrameLayout.LayoutParams imeParams = new FrameLayout.LayoutParams(1, 1);
        root.addView(inputViewTextInput, imeParams);
        root.addView(inputViewMuseField, imeParams);
        setContentView(root);
        if (showTextProbe) {
            showTextProbePage(root);
        }
        attachImeInsetListener(root);
        startAsyncTreeLoad();
    }

    /**
     * T1 (FR-STARTUP-002/AC-STARTUP-003): start the single-owner background
     * tree load and return immediately. Asset read + nativeLoadTree run on
     * the background loader; only the resulting handle is posted to the UI
     * thread. Never blocks, never throws.
     */
    void startAsyncTreeLoad() {
        final int generation = treeLoadGeneration;
        // A1 single-process lifetime (configChanges, no recreate): the loader is
        // shut down in onDestroy and never reused — guard the re-entry seam.
        if (treeLoader.isShutdown()) {
            Log.e("MainActivity", "tree-load: loader shut down, no reload");
            return;
        }
        try {
            treeLoader.execute(() -> {
                final byte[] bytes;
                try {
                    bytes = readTreeAsset();
                } catch (Throwable t) {
                    Log.e("MainActivity", "tree-load: asset read threw");
                    return;
                }
                if (bytes == null) {
                    Log.e("MainActivity", "tree-load failed-closed: null");
                    return;
                }
                if (bytes.length == 0) {
                    Log.e("MainActivity", "tree-load failed-closed: empty");
                    return;
                }
                final long handle;
                try {
                    handle = nativeLoadTree(bytes);
                } catch (Throwable t) {
                    Log.e("MainActivity", "tree-load: parse threw");
                    return;
                }
                if (handle == 0) {
                    Log.e("MainActivity", "tree-load failed-closed: native-0");
                    return;
                }
                final long readyHandle = handle;
                final byte[] readyBytes = bytes;
                new Handler(Looper.getMainLooper()).post(() -> {
                    if (generation != treeLoadGeneration) {
                        // Security LOW (destroy-during-parse race): the bg parse
                        // produced readyHandle after onDestroy bumped the
                        // generation. Drop without install/render and destroy the
                        // orphan so one native tree does not leak per race hit.
                        // UI-thread-safe: same UI-thread nativeDestroy path as
                        // onDestroy for a never-attached (window == null) handle
                        // (registry-claimed exactly-once, fail-closed, never throws).
                        if (readyHandle != 0) {
                            nativeDestroy(readyHandle);
                        }
                        return;
                    }
                    try {
                        handleTreeBytes(readyBytes, readyHandle);
                    } catch (Throwable t) {
                        Log.e("MainActivity", "tree-load: completion threw");
                    }
                });
            });
        } catch (Throwable t) {
            Log.e("MainActivity", "tree-load: submit failed");
        }
    }

    /**
     * T1 completion hook (runs on the UI thread): fail-closed install of the
     * pre-parsed tree handle. The costly parse (null/empty check +
     * nativeLoadTree + == 0 branch) already ran on the background loader;
     * this hook only re-validates, installs the handle, sets treeReady, and
     * calls the T2 gate. Null/empty bytes or handle == 0 logs
     * {@code tree-load failed-closed: <null|empty|native-0>}, leaves
     * {@code engineHandle} at 0, and never throws.
     */
    void handleTreeBytes(byte[] treeBytes, long handle) {
        try {
            if (treeBytes == null) {
                Log.e("MainActivity", "tree-load failed-closed: null");
                return;
            }
            if (treeBytes.length == 0) {
                Log.e("MainActivity", "tree-load failed-closed: empty");
                return;
            }
            if (handle == 0) {
                Log.e("MainActivity", "tree-load failed-closed: native-0");
                return;
            }
            engineHandle = handle;
            treeReady = true;
            maybeRenderFirstFrame();
        } catch (Throwable t) {
            Log.e("MainActivity", "tree-load: handle install failed");
        }
    }

    /**
     * First-render gate (T2, FR-STARTUP-002/AC-STARTUP-003): posts
     * {@code nativeAttachSurface + nativeRenderFrame} only when
     * {@code treeReady && surfaceReady && engineHandle != 0}; else silent no-op.
     * Single first-render entry (RULE-STARTUP-002): both the T1 post-back and
     * the surface callbacks call this method.
     */
    private void maybeRenderFirstFrame() {
        if (!(treeReady && surfaceReady && engineHandle != 0)) {
            return;
        }
        SurfaceHolder holder = pendingHolder;
        if (holder == null) {
            return;
        }
        nativeAttachSurface(engineHandle, holder.getSurface());
        nativeRenderFrame(engineHandle);
        // FR-RUNTIME-011 T4 seam first frame (Stage 3 fix, MAJOR 1).
        if (seamSurfaceHandle != 0) {
            try {
                nativeSeamRenderFrame(seamSurfaceHandle);
            } catch (Throwable ignored) {
            }
        }
        maybeReportFullyDrawn();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // BUG-RUNTIME-005 post-rebase: advanceReceiver above is scoped per
        // BUG-SAMPLE-007 (shell-chrome/key/deep-link only, fail-closed
        // otherwise); proofAdvanceReceiver is the harness-only entry;
        // surface-gesture paging stays off (AC-SAMPLE-039).
        if (probeField != null) {
            try {
                clipboardManager =
                        (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
                if (clipboardManager != null) {
                    clipboardManager.addPrimaryClipChangedListener(clipListener);
                }
            } catch (Throwable ignored) {
                // Fail-closed: no clipboard observation without a manager.
            }
        }
        if (Build.VERSION.SDK_INT >= 33) {
            // Trigger is `adb shell am broadcast` from outside the app, so the
            // receivers must be exported to receive it.
            registerReceiver(advanceReceiver, new IntentFilter(ACTION_NATIVE_ADVANCE_PAGE),
                    Context.RECEIVER_EXPORTED);
            registerReceiver(proofAdvanceReceiver, new IntentFilter(ACTION_PROOF_ADVANCE_PAGE),
                    Context.RECEIVER_EXPORTED);
            registerReceiver(themeReceiver, new IntentFilter(ACTION_NATIVE_SET_THEME),
                    Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(advanceReceiver, new IntentFilter(ACTION_NATIVE_ADVANCE_PAGE));
            registerReceiver(proofAdvanceReceiver, new IntentFilter(ACTION_PROOF_ADVANCE_PAGE));
            registerReceiver(themeReceiver, new IntentFilter(ACTION_NATIVE_SET_THEME));
        }
    }

    @Override
    protected void onPause() {
        try {
            if (clipboardManager != null) {
                clipboardManager.removePrimaryClipChangedListener(clipListener);
            }
        } catch (Throwable ignored) {
            // Listener was not registered.
        } finally {
            clipboardManager = null;
        }
        try {
            unregisterReceiver(advanceReceiver);
        } catch (IllegalArgumentException ignored) {
            // Receiver was not registered (e.g. paused without resume).
        }
        try {
            unregisterReceiver(proofAdvanceReceiver);
        } catch (IllegalArgumentException ignored) {
            // Receiver was not registered (e.g. paused without resume).
        }
        try {
            unregisterReceiver(themeReceiver);
        } catch (IllegalArgumentException ignored) {
            // Receiver was not registered (e.g. paused without resume).
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        treeLoadGeneration++;
        treeLoader.shutdownNow();
        if (engineHandle != 0) {
            nativeDetachSurface(engineHandle);
            nativeDestroy(engineHandle);
            engineHandle = 0;
        }
        // T1 idempotent teardown: safe to repeat (kOk on repeats); the slot
        // reset makes a second destroy a no-op with no native call.
        destroySeamSurface();
        super.onDestroy();
    }

    /**
     * BUG-M3-FIDELITY-003 round-2: Java-side host for the TextProbePage
     * (Showcase TextProbePage, sharedId="m3-text-probe"). Overlays the probe
     * title plus the "Probe live field" input on top of the gallery surface
     * so the `--es page m3-text-probe` launch shows the probe page. The
     * engine tree underneath is untouched; without the extra this method
     * never runs and gallery behavior is unchanged.
     */
    private void showTextProbePage(FrameLayout root) {
        LinearLayout probe = new LinearLayout(this);
        probe.setOrientation(LinearLayout.VERTICAL);
        probe.setTag("m3-text-probe");
        probe.setContentDescription("m3-text-probe");
        TextView title = new TextView(this);
        title.setText("Text probe page");
        title.setContentDescription("m3-text-probe");
        probe.addView(title);
        EditText field = new IMEInterceptingEditText(this);
        field.setHint("Probe live field");
        field.setContentDescription("Probe live field");
        field.setTag("m3-text-probe");
        field.setFocusable(true);
        field.setFocusableInTouchMode(true);
        // T8 observation wiring: TextWatcher content/bound path with the
        // isApplyingRemoteEdit echo guard (programmatic setText emits no
        // text_updated of its own; user edits always emit). The touch-caret
        // (onSelectionChanged) and IME (InputConnection wrapper) paths live
        // in IMEInterceptingEditText itself; clipboard (content + MIME)
        // arrives via the OnPrimaryClipChangedListener below.
        probeField = (IMEInterceptingEditText) field;
        probeField.addTextChangedListener(new TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
            }

            @Override
            public void afterTextChanged(Editable s) {
                if (isApplyingRemoteEdit) {
                    return;
                }
                try {
                    if (probeField != null) {
                        probeField.nativeOnTextUpdated(s != null ? s.toString() : "");
                    }
                } catch (Throwable ignored) {
                    // Fail-closed: observation never breaks the probe field.
                }
            }
        });
        // AC-M3-013 tap summon: when a tap lands the field gains focus first
        // (attached + laid out on screen, EditText accepts input), then hand
        // the already-focused view to IMM so the keyboard reliably appears.
        field.setOnFocusChangeListener((v, hasFocus) -> {
            if (hasFocus) {
                InputMethodManager tapImm =
                        (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                if (tapImm != null) {
                    tapImm.showSoftInput(field, InputMethodManager.SHOW_IMPLICIT);
                }
            }
        });
        probe.addView(field);
        // T10 (AC-M3-014 remainder, U2): probe-page debug affordance that
        // stages the pinned rich clip. The staged clip is system clipboard
        // content, so the existing OnPrimaryClipChangedListener WILL fire —
        // that clip_changed (mime=text/html) IS the asserted event, not echo
        // (echo guard covers programmatic field edits only, not clipboard).
        Button stageRichClip = new Button(this);
        stageRichClip.setText("Stage rich clip");
        stageRichClip.setContentDescription("Stage rich clip");
        stageRichClip.setOnClickListener(v -> stageRichClip());
        probe.addView(stageRichClip);
        FrameLayout.LayoutParams params = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT);
        root.addView(probe, params);
        // Verification probe: hardened keyboard summon for the overlay field
        // (requestFocus gate + SHOW_IMPLICIT after attach). If the gate still
        // reports mInputShown=false, the emulator config — not the app — is
        // the blocker.
        field.post(() -> {
            boolean focused = field.requestFocus();
            if (focused) {
                InputMethodManager imm =
                        (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
                if (imm != null) {
                    imm.showSoftInput(field, InputMethodManager.SHOW_IMPLICIT);
                }
            }
        });
    }

    /**
     * T8 clipboard path (AC-M3-014): reports the new primary clip as one
     * {@code clip_changed} observation (content + MIME) through the probe
     * field's native sink. Null/empty clips fail closed (no line, no
     * crash); failures warn under the shared {@code muse_text} tag only.
     */
    private void reportClipboardChanged() {
        if (probeField == null) {
            return;
        }
        try {
            ClipboardManager manager = clipboardManager != null ? clipboardManager
                    : (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (manager == null || !manager.hasPrimaryClip()) {
                return;
            }
            ClipData clip = manager.getPrimaryClip();
            if (clip == null || clip.getItemCount() == 0) {
                return;
            }
            ClipData.Item item = clip.getItemAt(0);
            CharSequence content = item != null ? item.coerceToText(this) : null;
            String mime = null;
            try {
                if (clip.getDescription() != null && clip.getDescription().getMimeTypeCount() > 0) {
                    mime = clip.getDescription().getMimeType(0);
                }
            } catch (Throwable ignored) {
                mime = null;
            }
            int cursor = 0;
            try {
                cursor = Math.max(0, probeField.getSelectionStart());
            } catch (Throwable ignored) {
                cursor = 0;
            }
            probeField.nativeOnIMEEvent(
                    "clip_changed",
                    IMEInterceptingEditText.withClipEnvelope(
                            content != null ? content.toString() : "", mime),
                    cursor,
                    IMEInterceptingEditText.nextEventId());
        } catch (Throwable t) {
            Log.w(IMEInterceptingEditText.MUSE_TEXT_TAG, "clip_changed observation failed", t);
        }
    }

    /**
     * T10 staging affordance (AC-M3-014 remainder): stages the pinned rich
     * payload — HTML {@code <b>rich</b>} + plain fallback {@code rich} — via
     * {@code ClipData.newHtmlText} so the existing clip_changed listener
     * reports it with {@code mime=text/html}. The listener's MIME field is
     * generic (reads description mime type 0 verbatim), so text/html routes
     * with no transport change. Fail-closed: null manager / throwable never
     * crashes the probe page. No new permissions required.
     */
    private void stageRichClip() {
        try {
            ClipboardManager manager = clipboardManager != null ? clipboardManager
                    : (ClipboardManager) getSystemService(Context.CLIPBOARD_SERVICE);
            if (manager == null) {
                return;
            }
            manager.setPrimaryClip(
                    ClipData.newHtmlText("muse-rich-probe", "rich", "<b>rich</b>"));
        } catch (Throwable ignored) {
            // Fail-closed: staging never breaks the probe page.
        }
    }

    /**
     * T8 echo-guard path: applies a programmatic (remote) edit to the
     * probe field without emitting a {@code text_updated} echo of its own.
     * The {@link TextWatcher} suppresses its native echo while
     * {@code isApplyingRemoteEdit} is set; user edits are unaffected.
     */
    void applyRemoteEdit(String text) {
        if (probeField == null) {
            return;
        }
        isApplyingRemoteEdit = true;
        try {
            probeField.setText(text != null ? text : "");
        } finally {
            isApplyingRemoteEdit = false;
        }
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
            // Decor-level registration (bug-runtime-004 F2 scope): the
            // content-root FrameLayout never receives animation dispatches
            // under adjustUnspecified (decor consumes first), so register
            // the same callback on the decor view where IME transitions
            // dispatch. Single feed preserved — forwards to onInsetsFrame.
            getWindow().getDecorView().setWindowInsetsAnimationCallback(
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
            // F2 sampler fallback (bug-runtime-004 t5r6): on API 30+ the
            // onProgress path above never delivers nonzero heights on
            // 127.0.0.1:6562, so register the same visible-display-frame
            // sampler shape as the pre-30 fallback alongside it and feed
            // the SAME onInsetsFrame entry (no parallel feed).
            root.getViewTreeObserver().addOnGlobalLayoutListener(
                    new ViewTreeObserver.OnGlobalLayoutListener() {
                        @Override
                        public void onGlobalLayout() {
                            Rect visible = new Rect();
                            root.getWindowVisibleDisplayFrame(visible);
                            int keyboardHeight = Math.max(
                                    0, root.getRootView().getHeight() - visible.bottom);
                            onInsetsFrame((float) keyboardHeight);
                        }
                    });
        } else {
            root.setOnApplyWindowInsetsListener((v, insets) -> {
                onInsetsFrame((float) insets.getSystemWindowInsetBottom());
                return insets;
            });
            // Poll-based visible-display-frame sampler (t5r3, API 29): under
            // ADJUST_PAN getSystemWindowInsetBottom() above reads 0, so sample
            // the keyboard height per layout pass from the visible display
            // frame and feed the SAME onInsetsFrame entry (no parallel feed,
            // no window-behavior change).
            // Time-based per-frame sampler (F3, t5r5 root cause on API 29):
            // the app-window resize is a discrete jump — zero onGlobalLayout
            // passes fire mid-animation — so the layout-pass sampler above
            // can never observe transition frames. Poll the visible display
            // frame per frame during the transition window via a
            // handler-paced Runnable (started on the show/hide layout
            // trigger, stopped at steady state) and forward through the SAME
            // single onInsetsFrame entry. The layout-pass sampler stays for
            // steady states; this complements, never replaces, it.
            final Handler imeTransitionHandler = new Handler(Looper.getMainLooper());
            final int[] imeTransitionLastHeight = new int[] { -1 };
            final int[] imeTransitionStableFrames = new int[] { 0 };
            final boolean[] imeTransitionPolling = new boolean[] { false };
            final Runnable[] imeTransitionPoll = new Runnable[1];
            imeTransitionPoll[0] = new Runnable() {
                @Override
                public void run() {
                    Rect frame = new Rect();
                    root.getWindowVisibleDisplayFrame(frame);
                    int keyboardHeight = Math.max(
                            0, root.getRootView().getHeight() - frame.bottom);
                    onInsetsFrame((float) keyboardHeight);
                    if (keyboardHeight != imeTransitionLastHeight[0]) {
                        imeTransitionLastHeight[0] = keyboardHeight;
                        imeTransitionStableFrames[0] = 0;
                        imeTransitionHandler.postDelayed(imeTransitionPoll[0], 16);
                    } else if (imeTransitionStableFrames[0] < 3) {
                        imeTransitionStableFrames[0]++;
                        imeTransitionHandler.postDelayed(imeTransitionPoll[0], 16);
                    } else {
                        imeTransitionPolling[0] = false;
                    }
                }
            };
            root.getViewTreeObserver().addOnGlobalLayoutListener(
                    new ViewTreeObserver.OnGlobalLayoutListener() {
                        @Override
                        public void onGlobalLayout() {
                            Rect visible = new Rect();
                            root.getWindowVisibleDisplayFrame(visible);
                            int keyboardHeight = Math.max(
                                    0, root.getRootView().getHeight() - visible.bottom);
                            onInsetsFrame((float) keyboardHeight);
                            if (!imeTransitionPolling[0]) {
                                imeTransitionPolling[0] = true;
                                imeTransitionStableFrames[0] = 0;
                                imeTransitionHandler.post(imeTransitionPoll[0]);
                            }
                        }
                    });
        }
    }

    /**
     * Single inset feed: carry one animated frame height into the JNI
     * setter backing deps.insets.imeBottom (same px field the T1 anchor
     * consumes). Sole entry point — keep exactly one definition.
     * Paces one layering present per changed frame (F1): forwards the
     * value then renders, throttled by a last-value guard so an
     * unchanged frame is a no-op.
     * T1 conformance: setImeBottom applies in place (no re-attach) and is
     * applied within one frame; the theme switch above is likewise in place
     * (same activity, same page, no restart). Both ride the seam in-place
     * entries once the remaining T1 JNI bindings land.
     */
    void onInsetsFrame(float imeBottomPx) {
        if (engineHandle != 0) {
            if (imeBottomPx == lastImeBottomPx) {
                return;
            }
            lastImeBottomPx = imeBottomPx;
            nativeSetImeBottom(engineHandle, imeBottomPx);
            nativeRenderFrame(engineHandle);
            // FR-RUNTIME-011 T4 seam IME/render (Stage 3 fix, MAJOR 1): feed
            // the same inset in place through the seam and present the seam
            // frame. Fail-closed; never throws.
            if (seamSurfaceHandle != 0) {
                try {
                    nativeSeamSetImeBottom(seamSurfaceHandle, (int) imeBottomPx);
                    nativeSeamRenderFrame(seamSurfaceHandle);
                } catch (Throwable ignored) {
                }
            }
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

    /**
     * Signals first content present (TTFD) exactly once. Guarded so a
     * reporting failure never throws and never breaks the frame path.
     */
    private void maybeReportFullyDrawn() {
        if (fullyDrawnReported) {
            return;
        }
        fullyDrawnReported = true;
        try {
            reportFullyDrawn();
        } catch (Throwable ignored) {
            // Fail-closed: reporting must never break the frame path.
        }
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        pendingHolder = holder;
        surfaceReady = true;
        // FR-RUNTIME-011 T4 seam lifecycle (Stage 3 fix, MAJOR 1): attach the
        // surface through the seam and store the handle via the getter.
        attachSeamSurface(holder != null ? holder.getSurface() : null);
        maybeRenderFirstFrame();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        pendingHolder = holder;
        surfaceReady = true;
        // Re-attach guard: a no-op when the seam is already attached.
        attachSeamSurface(holder != null ? holder.getSurface() : null);
        maybeRenderFirstFrame();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceReady = false;
        // Release the native window before the framework frees it (the
        // presenter never outlives the host's invalidation call); the tree
        // handle itself survives for the next surface. T1 detach is
        // idempotent: a repeat detach (e.g. destroy-after-destroy) is kOk.
        if (engineHandle != 0) {
            nativeDetachSurface(engineHandle);
        }
        detachSeamSurface();
    }
}
