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

    /** Last forwarded inset height; throttles the paced present (F1). */
    private float lastImeBottomPx = -1f;

    /**
     * Touch-slop mirror (sole source of truth is
     * {@code runtime::touch_detail::kTouchSlopPx}, 8px). FR-SAMPLE-014
     * removed the Java page-flip gate that consumed it; the DOWN anchor
     * below is kept so the raw gesture stream still reaches the native tap
     * chain untouched. No Java gate pages anymore.
     */
    private static final float TOUCH_SLOP_PX = 8.0f;

    /** DOWN anchor of the active gesture, for the drag-vs-outside gate. */
    private float downX = 0f;
    private float downY = 0f;
    private int downPointerId = -1;

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

    /** Broadcast action the proof pages through (same entry as the tap path). */
    static final String ACTION_NATIVE_ADVANCE_PAGE = "muse.native.NATIVE_ADVANCE_PAGE";

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
            // FR-SAMPLE-014: neutralized — no broadcast paginates
            // (AC-SAMPLE-039). The receiver entry is kept for the proof
            // harness, but it intentionally never calls nativeAdvancePage.
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

    /** Release the attached surface without destroying the handle. */
    private static native void nativeDetachSurface(long handle);

    /** Feed one animated IME frame into the typed inset bus (deps.insets.imeBottom px). */
    private static native void nativeSetImeBottom(long handle, float imeBottomPx);

    /** Destroy the engine handle. */
    private static native void nativeDestroy(long handle);

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
                downPointerId = -1;
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
            } else if (action == MotionEvent.ACTION_CANCEL) {
                downPointerId = -1;
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
        byte[] treeBytes = readTreeAsset();
        if (treeBytes != null) {
            engineHandle = nativeLoadTree(treeBytes);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        // FR-SAMPLE-014: advanceReceiver above is neutralized (never pages);
        // both receivers stay registered so the harness/theme entries exist.
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
            registerReceiver(themeReceiver, new IntentFilter(ACTION_NATIVE_SET_THEME),
                    Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(advanceReceiver, new IntentFilter(ACTION_NATIVE_ADVANCE_PAGE));
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
            unregisterReceiver(themeReceiver);
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
     */
    void onInsetsFrame(float imeBottomPx) {
        if (engineHandle != 0) {
            if (imeBottomPx == lastImeBottomPx) {
                return;
            }
            lastImeBottomPx = imeBottomPx;
            nativeSetImeBottom(engineHandle, imeBottomPx);
            nativeRenderFrame(engineHandle);
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
