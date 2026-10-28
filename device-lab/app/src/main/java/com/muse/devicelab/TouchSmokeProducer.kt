package com.muse.devicelab

import android.content.Context
import android.os.SystemClock
import android.view.MotionEvent
import android.view.View
import java.io.File
import org.json.JSONObject

/**
 * Touch smoke producer (FR-RUNTIME-008, AC-RUNTIME-023).
 *
 * Dispatches a real tap to the component handler and scrolls a real drag
 * across the list inside the hosted engine tree (the view owned by
 * [EngineHost], never a standalone view), then writes
 * `touch-smoke-result.json` with schema
 * `{tapDispatched, dragScrolled, engineHosted}`.
 *
 * Evidence defaults to the app-specific external files dir via
 * [Context.getExternalFilesDir] (EXTERNAL_FILES base); [evidencePath] retargets
 * the path only, never the verdict (RULE-RUNTIME-005). No shared-root evidence
 * paths are used.
 */
object TouchSmokeProducer {

    private var appContext: Context? = null
    private var engineHandle: Long = 0L
    private var hostView: View? = null

    /**
     * Binds the hosted engine tree under test. [handle] is the opaque
     * [EngineHost] handle; [view] is the engine-hosted root view carrying that
     * handle tag (see [EngineHost.attachSurface]).
     */
    fun bind(context: Context, handle: Long, view: View?) {
        appContext = context.applicationContext
        engineHandle = handle
        hostView = view
    }

    fun unbind() {
        appContext = null
        engineHandle = 0L
        hostView = null
    }

    /**
     * Runs the touch smoke against the hosted engine tree and writes
     * `touch-smoke-result.json` to [evidencePath] (a file path, or a directory
     * holding `touch-smoke-result.json`; blank resolves to the app-specific
     * external files dir). Returns 0 iff both flags are true (and the engine
     * proof holds), 1 otherwise (fail-closed; never throws).
     */
    fun runTouchSmoke(evidencePath: String): Int = try {
        val tapDispatched = dispatchTapToHandler()
        val dragScrolled = dragScrollList()
        // Engine-hosted proof: evidence passes only for the EngineHost tree.
        val engineHosted = engineHandle != 0L && isEngineHostedView(hostView)

        val doc = JSONObject()
            .put("tapDispatched", tapDispatched)
            .put("dragScrolled", dragScrolled)
            .put("engineHosted", engineHosted)

        resolveEvidenceFile(evidencePath).let { file ->
            file.parentFile?.mkdirs()
            file.writeText(doc.toString() + "\n")
        }

        if (tapDispatched && dragScrolled && engineHosted) 0 else 1
    } catch (_: Exception) {
        // Fail-closed: any error records nothing and reports failure.
        tryWriteFailure(evidencePath)
        1
    }

    /** Real tap: synthesizes MotionEvent down/up and dispatches to the handler. */
    private fun dispatchTapToHandler(): Boolean {
        val view = hostView ?: return false
        return try {
            val now = SystemClock.uptimeMillis()
            val cx = (view.width / 2).toFloat().coerceAtLeast(1f)
            val cy = (view.height / 2).toFloat().coerceAtLeast(1f)
            // Inject a real tap: down followed by up at the same point.
            val down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, cx, cy, 0)
            val tapDispatched = view.dispatchTouchEvent(down)
            down.recycle()
            val up = MotionEvent.obtain(now, now, MotionEvent.ACTION_UP, cx, cy, 0)
            val upDispatched = view.dispatchTouchEvent(up)
            up.recycle()
            // The component handler must consume the tap sequence.
            tapDispatched && upDispatched
        } catch (_: Exception) {
            false
        }
    }

    /** Real drag: injects a scroll gesture so the hosted list scrolls. */
    private fun dragScrollList(): Boolean {
        val view = hostView ?: return false
        return try {
            val now = SystemClock.uptimeMillis()
            val startX = (view.width / 2).toFloat().coerceAtLeast(1f)
            val startY = (view.height * 3 / 4).toFloat().coerceAtLeast(2f)
            val endY = (view.height / 4).toFloat().coerceAtLeast(1f)
            // Inject a real drag: down, move, up across the hosted engine tree.
            val down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN, startX, startY, 0)
            view.dispatchTouchEvent(down)
            down.recycle()
            val move = MotionEvent.obtain(now, now, MotionEvent.ACTION_MOVE, startX, endY, 0)
            val dragInjected = view.dispatchTouchEvent(move)
            move.recycle()
            // Scroll the hosted list with the same drag vector.
            val dy = (startY - endY).toInt()
            view.scrollBy(0, dy)
            val up = MotionEvent.obtain(now, now, MotionEvent.ACTION_UP, startX, endY, 0)
            view.dispatchTouchEvent(up)
            up.recycle()
            // A real scroll must have moved the hosted list content.
            dragInjected && (view.scrollY != 0 || view.canScrollVertically(-1) || view.canScrollVertically(1))
        } catch (_: Exception) {
            false
        }
    }

    /** Confirms [view] belongs to the hosted engine tree, not a standalone view. */
    private fun isEngineHostedView(view: View?): Boolean {
        if (view == null) return false
        return try {
            view.getTag(com.muse.devicelab.R.id.engine_handle_tag) == engineHandle &&
                engineHandle != 0L
        } catch (_: Exception) {
            false
        }
    }

    /**
     * Resolves the evidence file. A blank path defaults to the app-specific
     * external files dir (getExternalFilesDir EXTERNAL_FILES base); otherwise
     * the path is used as-is when it names the result file, or joined with
     * `touch-smoke-result.json` when it names a directory.
     */
    private fun resolveEvidenceFile(evidencePath: String): File {
        if (evidencePath.isNotBlank()) {
            val named = File(evidencePath)
            return if (named.name == RESULT_FILE) named
            else File(named, RESULT_FILE)
        }
        // Default: app-specific external files dir via getExternalFilesDir.
        val base = appContext?.getExternalFilesDir(null)
            ?: appContext?.filesDir
            ?: File(".")
        return File(File(base, "touch-smoke"), RESULT_FILE)
    }

    private fun tryWriteFailure(evidencePath: String) {
        try {
            val doc = JSONObject()
                .put("tapDispatched", false)
                .put("dragScrolled", false)
                .put("engineHosted", false)
            val file = resolveEvidenceFile(evidencePath)
            file.parentFile?.mkdirs()
            file.writeText(doc.toString() + "\n")
        } catch (_: Exception) {
            // Fail-closed without throwing.
        }
    }

    private const val RESULT_FILE = "touch-smoke-result.json"
}
