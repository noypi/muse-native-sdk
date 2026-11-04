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

    /**
     * T6 follow-up (FR-INPUT-012 AC-INPUT-024/024a): bridge-tap dispatch.
     * Delivers a bridge-minted tapId integer to the owning handle's
     * per-button pending entry via dispatchTapToOwner (consume-before-notify,
     * exactly-once per tapId). Returns 1 on delivery, 0 on fail-closed denial.
     * Never throws across the VM.
     */
    private static native int nativeDispatchBridgeTap(
            long handle, int nodeId, int tapId);

    /**
     * T6 follow-up (AC-INPUT-024a): tapId-integer identity getter. Returns
     * the tapId minted by the most recent bridge-delivered tap (0 when none
     * has fired yet); the JS handle admits it verbatim via
     * adoptBridgeTap(tapId). Never throws.
     */
    private static native int nativeLastBridgeTapId(long handle);

    /**
     * T9 (fr-nav-014-thin-host / AC-NAV-042): generic engine shell-state
     * carriage. Stores the opaque shellRoute string plus the optional
     * shellNotice string into the native handle slot; validity is decided
     * by the route table in JS, never natively. Fail-closed: returns 1
     * applied, 0 denied (null/empty/over-2048 route, bad handle); never
     * throws into the VM. Declared {@code int} to pair with the JNI
     * {@code Java_com_muse_host_MainActivity_nativeSetShellState} export;
     * callers treat the result as opaque (plan {@code : void}). Instance
     * (non-static) to match the export's {@code jobject} receiver.
     */
    private native int nativeSetShellState(long handle, String shellRoute, String shellNotice);

    /**
     * BUG-NAV-005 (FR-NAV-011/014, AC-NAV-038/041/042): generic live router
     * invoke. Forwards a method plus JSON args to the handle's live
     * `RouterBridge::invoke` commit gate and returns the JSON verdict
     * (`{"ok":true,...}` allow or `{"ok":false,"code":"..."}` named denial —
     * same vocabulary the JS facade funnels through). Generic strings only:
     * no route parsing, no route literals, no demo knowledge here (thin-host
     * AC-NAV-044/045). Declared {@code String} to pair with the JNI
     * {@code Java_com_muse_host_MainActivity_nativeRouterInvoke} export;
     * callers treat the result as opaque (a null return is a transport-level
     * denial, fail-closed). Instance (non-static) to match the export's
     * {@code jobject} receiver.
     */
    private native String nativeRouterInvoke(long handle, String method, String jsonArgs);

    /**
     * BUG-NAV-005 route-table provisioning (generic, opaque): replaces the
     * handle bridge's route table from a JSON string array of route ids.
     * The ids ride through uninterpreted manifest-owned data; validity stays
     * with the engine gate. Declared {@code int} to pair with the JNI
     * {@code Java_com_muse_host_MainActivity_nativeRouterSetRoutes} export;
     * callers treat the result as opaque (1 applied, 0 denied). Instance
     * (non-static) to match the export's {@code jobject} receiver.
     */
    private native int nativeRouterSetRoutes(long handle, String routesJson);

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

    /** Last opaque string accepted by the sink; opaque only, never parsed. */
    static volatile String lastForwardedOpaqueLink = null;

    /**
     * Default live sink installed at boot (first-wins: a pre-installed
     * engine/test sink keeps priority). Retains the taken opaque string
     * unparsed for introspection and marks delivery in logcat; never parses.
     */
    private static final OpaqueLinkSink DEFAULT_OPAQUE_LINK_SINK = opaqueLink -> {
        lastForwardedOpaqueLink = opaqueLink;
        Log.i(TAG, "opaque-sink-accept engineHosted=true");
    };

    /** Installs the default live sink unless one is already present. */
    static void installDefaultOpaqueLinkSink() {
        if (opaqueLinkSink == null) {
            opaqueLinkSink = DEFAULT_OPAQUE_LINK_SINK;
        }
    }

    /**
     * Handoff into the JS routing layer. Host never parses; the stashed
     * opaque string stays available for the JS drain above. Same channel
     * serves cold-start (onCreate) and warm (onNewIntent) arrivals.
     *
     * BUG-NAV-005: the taken string is additionally offered to the live
     * router commit gate via {@link #invokeLiveRouter} — the Java drain
     * calls that JNI entry directly, and any future platform binding of
     * the {@code RouterBridge.invoke} transport keeps first-wins — so
     * the opaque-stash drain rides the C++ gate instead of ending at the
     * log-only sink. Never parses; a named gate denial only logs.
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
        invokeLiveRouter(opaqueLink);
        Log.i(TAG, "opaque-intent-ready engineHosted=true");
        // Consume-once: caller passes the taken value; slot already cleared.
    }

    /**
     * BUG-NAV-005 link→id boundary normalization (single funnel helper):
     * converts one opaque link string to the bare table id the NavGraph
     * exact-matches, applied ONCE in {@link #invokeLiveRouter} before
     * {@code nativeRouterInvoke}. Rule (generic, no route literals): trim;
     * strip fragment/query; if a {@code ://} scheme is present drop scheme +
     * host (path after the first {@code /}); take the last non-empty
     * {@code /} segment; accept only {@code [A-Za-z0-9_-]+}. Returns null
     * when nothing parseable remains (fail-closed: caller logs
     * {@code unknown-route}, never throws, never invokes).
     */
    private static String normalizeOpaqueLinkToRouteId(String opaqueLink) {
        if (opaqueLink == null) {
            return null;
        }
        try {
            String s = opaqueLink.trim();
            if (s.isEmpty()) {
                return null;
            }
            int hash = s.indexOf('#');
            if (hash >= 0) {
                s = s.substring(0, hash).trim();
            }
            int query = s.indexOf('?');
            if (query >= 0) {
                s = s.substring(0, query).trim();
            }
            if (s.isEmpty()) {
                return null;
            }
            int scheme = s.indexOf("://");
            if (scheme >= 0) {
                s = s.substring(scheme + 3);
                int slash = s.indexOf('/');
                if (slash < 0) {
                    // No path: the host remainder itself is the candidate id
                    // (e.g. `scheme://field`); validated below, fail-closed.
                } else {
                    s = s.substring(slash + 1);
                }
            }
            String[] parts = s.split("/");
            String last = null;
            for (String part : parts) {
                String t = part.trim();
                if (!t.isEmpty()) {
                    last = t;
                }
            }
            if (last == null || !last.matches("[A-Za-z0-9_-]+")) {
                return null;
            }
            return last;
        } catch (Throwable ignored) {
            return null;
        }
    }

    /**
     * BUG-NAV-005 same-entry commit (AC-NAV-038/042): delivers the taken
     * opaque string as the bare-id JSON {@code route} arg of a generic
     * {@code navigate} invoke against the live handle bridge. The single
     * {@link #normalizeOpaqueLinkToRouteId} call above is the ONLY link
     * handling: scheme/host/prefix stripped to the table id the NavGraph
     * exact-matches (exact-match preserved everywhere else); validity is
     * decided ONLY by the engine gate, which returns the named allow/deny
     * verdict. Fail-closed: zero handle, unparseable id ({@code unknown-route}
     * log, no invoke), JNI absence, or any throwable only logs; never
     * crashes, never mutates host state, never carries demo literals
     * (AC-NAV-044/045).
     */
    private void invokeLiveRouter(String opaqueLink) {
        if (opaqueLink == null || opaqueLink.isEmpty() || engineHandle == 0) {
            return;
        }
        try {
            String routeId = normalizeOpaqueLinkToRouteId(opaqueLink);
            if (routeId == null) {
                Log.i(TAG, "opaque-router-verdict {\"ok\":false,\"code\":\"unknown-route\"}");
                return;
            }
            String jsonArgs = "{\"route\":\"" + quoteJsonString(routeId) + "\"}";
            String verdict = nativeRouterInvoke(engineHandle, "navigate", jsonArgs);
            String shown = verdict == null ? "transport-error" : verdict;
            if (shown.length() > 256) {
                shown = shown.substring(0, 256);
            }
            Log.i(TAG, "opaque-router-verdict " + shown);
        } catch (Throwable ignored) {
            // Fail-closed: gate unreachable never crashes the host.
            Log.i(TAG, "opaque-router-verdict transport-error");
        }
    }

    /**
     * Minimal JSON string quoter for {@link #invokeLiveRouter}: quotes,
     * backslashes, and controls become escapes; everything else rides
     * through verbatim (no parsing, no interpretation). Never throws.
     */
    private static String quoteJsonString(String raw) {
        if (raw == null) {
            return "";
        }
        StringBuilder out = new StringBuilder(raw.length() + 2);
        for (int i = 0; i < raw.length(); i++) {
            char c = raw.charAt(i);
            switch (c) {
                case '"':
                    out.append("\\\"");
                    break;
                case '\\':
                    out.append("\\\\");
                    break;
                case '\n':
                    out.append("\\n");
                    break;
                case '\r':
                    out.append("\\r");
                    break;
                case '\t':
                    out.append("\\t");
                    break;
                default:
                    if (c < 0x20) {
                        out.append(String.format("\\u%04x", (int) c));
                    } else {
                        out.append(c);
                    }
                    break;
            }
        }
        return out.toString();
    }

    /** Drain the cold-start stash into the router once ready (unparsed). */
    private void drainPendingOpaqueLink() {
        if (pendingOpaqueLink == null || engineHandle == 0) {
            return;
        }
        String link = consumePendingOpaqueLink();
        forwardOpaqueToRouter(link);
    }

    /**
     * BUG-NAV-005 test-only route-table provision (FR-NAV-011/014,
     * AC-NAV-038/041/042 tester gate; shipped tap path untouched).
     *
     * <p>Zero production callers provision the bridge: without this entry
     * the handle's route table stays empty on device, so every
     * {@code nativeRouterInvoke("navigate", …)} denies {@code unknown-route}
     * and the tap→graph→kill-restore proof (ok=1 + apply + restore-lands-on-tab)
     * has no on-device path. This drain closes that gap for the tester leg
     * only: when the launching intent carries the string extra
     * {@code muse_test_routes_json} (a JSON string array of route ids, e.g.
     * {@code ["home","lens","field","list"]}), it is forwarded verbatim to
     * the generic {@code nativeRouterSetRoutes} entry and the
     * {@code router-test-provision ok=1/0} verdict is logged. Production
     * launches carry no such extra, so this is a silent no-op on the shipped
     * path: no tap, stash, sink, or invoke behavior changes. Generic opaque
     * strings only — no route parsing, no route literals, no demo knowledge
     * (thin-host AC-NAV-044/045); fail-closed on zero handle, missing JNI,
     * oversize payload, or any throwable. Never throws.
     *
     * <p>NOTE for planner (AC-NAV-045 tension): AC-NAV-045 says probe/test
     * hooks live in JS/engine, never in Java. This entry is deliberately
     * minimal and generic (opaque forward + log, same shape as the stash
     * drain) because only Java holds {@code engineHandle} for the JNI
     * provision call — JS cannot reach it. If the planner prefers a stricter
     * reading, the alternative is an instrumentation-only static armer; the
     * shipped-path semantics are identical either way.
     */
    private void maybeTestProvisionRoutes() {
        // Security MEDIUM-1: intent-exported activity must not provision the
        // live table in release; DEBUG-only gate, silent no-op otherwise.
        if (!BuildConfig.DEBUG) {
            return;
        }
        if (engineHandle == 0) {
            return;
        }
        String routesJson = null;
        try {
            Intent intent = getIntent();
            if (intent == null || !intent.hasExtra("muse_test_routes_json")) {
                return;
            }
            routesJson = intent.getStringExtra("muse_test_routes_json");
        } catch (Throwable ignored) {
            return;
        }
        if (routesJson == null || routesJson.isEmpty() || routesJson.length() > 8192) {
            Log.i(TAG, "router-test-provision ok=0");
            return;
        }
        try {
            int applied = nativeRouterSetRoutes(engineHandle, routesJson);
            Log.i(TAG, "router-test-provision ok=" + applied);
        } catch (Throwable ignored) {
            // Fail-closed: provision unreachable never crashes the host.
            Log.i(TAG, "router-test-provision ok=0");
        }
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
                int claimed = nativeDispatchTouch(
                        engineHandle,
                        event.getActionMasked(),
                        x,
                        y,
                        event.getPointerId(index),
                        event.getEventTime());
                if (event.getActionMasked() == MotionEvent.ACTION_UP && claimed >= 0) {
                    // T6 follow-up (AC-INPUT-024a): the fired tap's
                    // bridge-minted tapId integer crosses JNI here for the
                    // component-side JS handle (adoptBridgeTap(tapId)
                    // verbatim, then press() exactly once). Fail-closed:
                    // never breaks the tap path.
                    try {
                        int bridgeTapId = nativeLastBridgeTapId(engineHandle);
                        Log.d("MainActivity", "bridge tapId=" + bridgeTapId);
                    } catch (Throwable ignored) {
                        // Fail-closed: bridge identity never breaks taps.
                    }
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
        // Cold-start VIEW: install the live sink first so the drain below
        // has an endpoint, then stash opaque and drain unparsed now that
        // the handle exists (warm arrivals drain via onNewIntent below).
        installDefaultOpaqueLinkSink();
        // BUG-NAV-005 tester gate: provision the route table (when the
        // test-only launch extra is present; silent no-op otherwise) BEFORE
        // the stash drain so a drained link hits a populated table.
        maybeTestProvisionRoutes();
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
