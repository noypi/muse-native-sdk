package com.muse.host;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.KeyEvent;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputConnectionWrapper;
import android.widget.EditText;
import java.util.concurrent.atomic.AtomicLong;

/**
 * T8 (BUG-M3-FIDELITY-003 / FR-M3-006, AC-M3-011/012/014): observability-only
 * probe field for the single instrumented page ("m3-text-probe").
 *
 * <p>Plain {@link EditText} subclass (the m3-gallery host classpath carries
 * no androidx dependency, so no AppCompat base is available). Behavior is
 * byte-identical to {@link EditText}: every {@link InputConnection} entry is
 * captured and then delegated to {@code super} unconditionally, and the
 * connection itself is the framework one wrapped in an
 * {@link InputConnectionWrapper}. The only addition is observation: each
 * entry emits exactly one {@code muse_text} JSON logcat line through the
 * native logcat-relay sink below ({@code adb logcat -s muse_text}).
 *
 * <p>Closed {@code type} set (9 members, exhaustive per the T8 contract):
 * {@code composing}, {@code commit_text}, {@code finish_composing},
 * {@code delete}, {@code set_selection}, {@code key_event},
 * {@code editor_action}, {@code text_updated}, {@code clip_changed}.
 * {@code clip_changed} is emitted by the host activity's
 * {@code OnPrimaryClipChangedListener}; every other type originates here.
 *
 * <p>Event ids are drawn from one process-wide monotonic sequence
 * ({@link #nextEventId()}); the native sink canonicalizes them into a
 * gap-free stream (see {@code jni_bridge.cpp}). Span bounds for
 * span-carrying events ({@code composing}, and {@code commit_text} when it
 * consumes a composing span) ride inside the {@code text} argument as a
 * trailing envelope ({@link #withSpanEnvelope}); the native sink strips the
 * envelope and re-emits {@code spanStart}/{@code spanEnd} top-level, so the
 * fixed 4-arg JNI signature is never reshaped. Clipboard MIME rides the
 * same envelope ({@link #withClipEnvelope}). Malformed envelopes fail
 * closed (emitted as plain text, never a crash).
 *
 * <p>T10 note (AC-M3-014 remainder): no commitContent-style URI path is
 * added here. A commitContent override would go through
 * androidx.core.view.inputmethod.InputConnectionCompat, which is not on the
 * m3-gallery host classpath (this file deliberately avoids androidx), and
 * the framework InputConnection.commitContent API level + URI-permission
 * grant path is not reachable without new permissions/dependencies. The
 * text/html round trip is therefore observed end-to-end through the
 * existing clip_changed MIME routing (withClipEnvelope, generic mime type
 * 0) plus paste → TextWatcher text_updated, with no behavior change
 * besides the MainActivity "Stage rich clip" affordance.
 *
 * <p>Fail-closed everywhere: every emission is wrapped so no throwable
 * (null/empty/rapid input included) can escape into the IME or the view
 * system; a disabled observation path leaves a working editor behind.
 */
public class IMEInterceptingEditText extends EditText {

    /** Log tag for the observation stream (queried via {@code adb logcat -s muse_text}). */
    static final String MUSE_TEXT_TAG = "muse_text";

    /** Envelope separator between payload text and trailing span/MIME metadata. */
    static final char ENVELOPE_SEP = '\u0001';

    /** Process-wide monotonic event-id source shared by every emission path. */
    private static final AtomicLong EVENT_IDS = new AtomicLong(0);

    /** Last caret reported via {@code onSelectionChanged}; suppresses echo repeats. */
    private int lastReportedCaret = Integer.MIN_VALUE;

    public IMEInterceptingEditText(Context context) {
        super(context);
    }

    public IMEInterceptingEditText(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public IMEInterceptingEditText(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    /**
     * T8 JNI observation entry (exact contract signature). Implemented in
     * {@code engine/runtime/src/jni_bridge.cpp}: fail-closed logcat-relay
     * sink (exception-check + early return, null/empty safe) that emits one
     * {@code muse_text} JSON line per call and bumps the atomic
     * receive-counter. Never throws into the VM.
     */
    public native void nativeOnIMEEvent(
            String eventType, String text, int cursorPosition, long eventId);

    /**
     * T8 JNI observation entry for the {@code TextWatcher} content path
     * (exact contract signature). Same native sink as above; the sink owns
     * the canonical event id for this entry. Never throws into the VM.
     */
    public native void nativeOnTextUpdated(String currentText);

    /** Next process-wide monotonic event id (shared by the wrapper, caret, and clip paths). */
    public static long nextEventId() {
        return EVENT_IDS.incrementAndGet();
    }

    /**
     * Packs span bounds onto a payload for span-carrying events. The native
     * sink strips the envelope and re-emits {@code spanStart}/{@code spanEnd}
     * top-level; callers that never carry a span pass the payload through
     * untouched. Bounds are clamped to {@code 0 <= spanStart <= spanEnd}
     * here so a malformed range fails closed before it crosses JNI.
     */
    public static String withSpanEnvelope(String payload, int spanStart, int spanEnd) {
        String base = payload != null ? payload : "";
        int start = Math.max(0, spanStart);
        int end = Math.max(0, spanEnd);
        if (end < start) {
            end = start;
        }
        return base + ENVELOPE_SEP + "spanStart=" + start + ";spanEnd=" + end;
    }

    /**
     * Packs clipboard content + MIME onto the {@code text} argument for
     * {@code clip_changed} events. The native sink strips the envelope and
     * re-emits {@code mime} top-level with the plain content as
     * {@code text}. Null content/MIME fail closed to empty strings.
     */
    public static String withClipEnvelope(String content, String mime) {
        String base = content != null ? content : "";
        String type = mime != null ? mime : "";
        return base + ENVELOPE_SEP + "mime=" + type;
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        InputConnection base = super.onCreateInputConnection(outAttrs);
        if (base == null) {
            return null;
        }
        // Observability wrapper only: target stays the framework connection,
        // mutable flag matches the wrapped connection's batch-edit state.
        return new ObservingConnection(base, false);
    }

    @Override
    protected void onSelectionChanged(int selStart, int selEnd) {
        super.onSelectionChanged(selStart, selEnd);
        // Touch-caret path (AC-M3-012): a collapsed caret move reports
        // set_selection with the updated cursor. Range selections report the
        // anchor; unchanged carets are suppressed so layout passes stay
        // silent. Never throws into the view system.
        try {
            if (selStart == selEnd && selStart >= 0 && selStart != lastReportedCaret) {
                lastReportedCaret = selStart;
                nativeOnIMEEvent("set_selection", "", selStart, nextEventId());
            }
        } catch (Throwable ignored) {
            // Fail-closed: caret observation must never break selection.
        }
    }

    /** Fail-closed emit helper: a throwing sink still leaves a working editor. */
    private void emitIme(String type, String text, int cursor) {
        try {
            nativeOnIMEEvent(type, text != null ? text : "", cursor, nextEventId());
        } catch (Throwable ignored) {
            // Fail-closed: observation never breaks input.
        }
    }

    /** Current caret, clamped fail-closed when no cursor is available. */
    private int currentCursor() {
        try {
            int pos = getSelectionStart();
            return Math.max(0, pos);
        } catch (Throwable ignored) {
            return 0;
        }
    }

    /**
     * Observability wrapper over the framework connection. Each override
     * captures cursor + composing-span state, delegates to {@code super}
     * unconditionally, then emits exactly one observation event. The return
     * value is always the delegate's own — observation never changes it.
     */
    private final class ObservingConnection extends InputConnectionWrapper {

        ObservingConnection(InputConnection target, boolean mutable) {
            super(target, mutable);
        }

        @Override
        public boolean commitText(CharSequence text, int newCursorPosition) {
            // Capture the composing span BEFORE super: commit consumes it.
            int spanStart = -1;
            int spanEnd = -1;
            try {
                android.text.Editable editable = getEditable();
                if (editable != null) {
                    spanStart = BaseInputConnection.getComposingSpanStart(editable);
                    spanEnd = BaseInputConnection.getComposingSpanEnd(editable);
                }
            } catch (Throwable ignored) {
                spanStart = -1;
                spanEnd = -1;
            }
            boolean result = super.commitText(text, newCursorPosition);
            try {
                String payload = text != null ? text.toString() : "";
                if (spanStart >= 0 && spanEnd >= 0) {
                    payload = withSpanEnvelope(payload, spanStart, spanEnd);
                }
                emitIme("commit_text", payload, currentCursor());
            } catch (Throwable ignored) {
                // Fail-closed.
            }
            return result;
        }

        @Override
        public boolean setComposingText(CharSequence text, int newCursorPosition) {
            boolean result = super.setComposingText(text, newCursorPosition);
            try {
                String payload = text != null ? text.toString() : "";
                int spanStart = -1;
                int spanEnd = -1;
                try {
                    android.text.Editable editable = getEditable();
                    if (editable != null) {
                        spanStart = BaseInputConnection.getComposingSpanStart(editable);
                        spanEnd = BaseInputConnection.getComposingSpanEnd(editable);
                    }
                } catch (Throwable ignored) {
                    spanStart = -1;
                    spanEnd = -1;
                }
                if (spanStart >= 0 && spanEnd >= 0) {
                    payload = withSpanEnvelope(payload, spanStart, spanEnd);
                }
                emitIme("composing", payload, currentCursor());
            } catch (Throwable ignored) {
                // Fail-closed.
            }
            return result;
        }

        @Override
        public boolean finishComposingText() {
            boolean result = super.finishComposingText();
            try {
                emitIme("finish_composing", "", currentCursor());
            } catch (Throwable ignored) {
                // Fail-closed.
            }
            return result;
        }

        @Override
        public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            boolean result = super.deleteSurroundingText(beforeLength, afterLength);
            try {
                emitIme("delete", "", currentCursor());
            } catch (Throwable ignored) {
                // Fail-closed.
            }
            return result;
        }

        @Override
        public boolean setSelection(int start, int end) {
            boolean result = super.setSelection(start, end);
            try {
                if (start >= 0) {
                    lastReportedCaret = start;
                }
                emitIme("set_selection", "", Math.max(0, start));
            } catch (Throwable ignored) {
                // Fail-closed.
            }
            return result;
        }

        @Override
        public boolean sendKeyEvent(KeyEvent event) {
            boolean result = super.sendKeyEvent(event);
            try {
                String payload = "";
                try {
                    payload = event != null ? String.valueOf(event.getKeyCode()) : "";
                } catch (Throwable ignored) {
                    payload = "";
                }
                emitIme("key_event", payload, currentCursor());
            } catch (Throwable ignored) {
                // Fail-closed.
            }
            return result;
        }

        @Override
        public boolean performEditorAction(int editorAction) {
            boolean result = super.performEditorAction(editorAction);
            try {
                emitIme("editor_action", "", currentCursor());
            } catch (Throwable ignored) {
                // Fail-closed.
            }
            return result;
        }

        /** Editable behind this connection for span capture; null when detached. */
        private android.text.Editable getEditable() {
            try {
                return IMEInterceptingEditText.this.getText();
            } catch (Throwable ignored) {
                return null;
            }
        }
    }
}
