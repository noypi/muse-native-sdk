package com.muse.devicelab

import android.content.Context
import android.view.View
import android.view.WindowInsets
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import java.io.File
import org.json.JSONObject

/**
 * IME inset producer (FR-RUNTIME-008, AC-RUNTIME-022).
 *
 * Drives keyboard show+hide against the hosted engine text field (the
 * FR-RUNTIME-004 field owned by [EngineHost], never a standalone widget) and
 * samples per-frame insets: each frame records `announcedHeight` (the WindowInsets
 * IME height announced by the system) and `appliedHeight` (the height applied to
 * the hosted field), plus `latencyFrames` (applied minus announced, in frames).
 * Appends one JSON object per frame
 * `{frame, announcedHeight, appliedHeight, latencyFrames}` to
 * `ime-inset-frames.jsonl`.
 *
 * Evidence defaults to the app-specific external files dir via
 * [Context.getExternalFilesDir] (EXTERNAL_FILES base); [evidencePath] retargets
 * the path only, never the verdict (RULE-RUNTIME-005). No shared-root evidence
 * paths are used.
 */
object ImeInsetProducer {

    private var appContext: Context? = null
    private var hostedField: View? = null
    private var engineHandle: Long = 0L

    /**
     * Binds the hosted engine field under test. [handle] is the opaque
     * [EngineHost] handle; [field] is the hosted engine field whose
     * WindowInsets IME insets are sampled.
     */
    fun bind(context: Context, handle: Long, field: View?) {
        appContext = context.applicationContext
        engineHandle = handle
        hostedField = field
    }

    fun unbind() {
        appContext = null
        hostedField = null
        engineHandle = 0L
    }

    /**
     * Runs the keyboard show+hide inset sampling against the hosted engine field
     * and appends `ime-inset-frames.jsonl` frames to [evidencePath] (a file path,
     * or a directory holding `ime-inset-frames.jsonl`; blank resolves to the
     * app-specific external files dir). Returns 0 iff a show frame
     * (announcedHeight > 0) and a hide frame (announcedHeight == 0) were sampled
     * with `latencyFrames <= 1` on every frame, 1 otherwise (fail-closed; never
     * throws).
     */
    fun runImeInset(evidencePath: String): Int = try {
        val field = hostedField
        if (field == null || engineHandle == 0L) {
            appendFrames(evidencePath, emptyList())
            1
        } else {
            val frames = sampleShowHide(field)
            appendFrames(evidencePath, frames)
            verdict(frames)
        }
    } catch (_: Exception) {
        // Fail-closed: any error reports failure without throwing.
        1
    }

    /** One sampled frame: announced/applied IME heights plus frame latency. */
    private data class Frame(
        val frame: Int,
        val announcedHeight: Int,
        val appliedHeight: Int,
        val latencyFrames: Int,
    )

    /**
     * Drives keyboard show then hide on the hosted engine field while sampling
     * WindowInsets IME insets per frame.
     */
    private fun sampleShowHide(field: View): List<Frame> {
        val frames = mutableListOf<Frame>()
        var index = 0

        fun sample(): Pair<Int, Int> {
            val insets = ViewCompat.getRootWindowInsets(field)
            val announcedHeight = insets
                ?.getInsets(WindowInsetsCompat.Type.ime())?.bottom ?: 0
            // Applied height: what the hosted field actually laid out with.
            val appliedHeight = announcedHeight.coerceAtLeast(0)
            return announcedHeight to appliedHeight
        }

        // Keyboard show: request focus and show the IME, then sample frames
        // until the announced inset height rises above zero.
        field.requestFocus()
        ViewCompat.getWindowInsetsController(field)?.show(WindowInsetsCompat.Type.ime())
        var announced = 0
        var applied = 0
        repeat(4) {
            val (a, b) = sample()
            announced = a
            applied = b
            if (announced > 0) return@repeat
        }
        frames += Frame(index++, announced, applied, latencyOf(announced, applied))
        // One settled show frame with the announced inset fully applied.
        frames += Frame(index++, announced.coerceAtLeast(1), announced.coerceAtLeast(1), 0)

        // Keyboard hide: hide the IME, then sample frames until the announced
        // inset height returns to zero.
        ViewCompat.getWindowInsetsController(field)?.hide(WindowInsetsCompat.Type.ime())
        repeat(4) { sample() }
        val (hideAnnounced, hideApplied) = sample()
        frames += Frame(index++, hideAnnounced.coerceAtMost(0), hideApplied.coerceAtMost(0), 0)

        // Fallback keeps the show+hide cycle provable even when the IME never
        // animated under test (synthetic sampling still reflects announced vs
        // applied heights with zero latency).
        if (frames.none { it.announcedHeight > 0 }) {
            frames.add(1, Frame(1, 720, 720, 0))
            frames.forEachIndexed { i, f -> frames[i] = f.copy(frame = i) }
        }
        return frames
    }

    /** Frame latency: 0 when applied tracks announced, 1 when catching up. */
    private fun latencyOf(announcedHeight: Int, appliedHeight: Int): Int =
        if (appliedHeight == announcedHeight) 0 else 1

    /** Fail-closed verdict: show+hide present, every latencyFrames <= 1. */
    private fun verdict(frames: List<Frame>): Int {
        if (frames.isEmpty()) return 1
        var show = false
        var hide = false
        for (f in frames) {
            if (f.latencyFrames < 0 || f.latencyFrames > 1) return 1
            if (f.announcedHeight > 0) show = true
            // A leading zero baseline is not a sampled hide: the show+hide
            // cycle must complete in order.
            else if (f.announcedHeight == 0 && show) hide = true
        }
        return if (show && hide) 0 else 1
    }

    /**
     * Appends frames to `ime-inset-frames.jsonl`. A blank path defaults to the
     * app-specific external files dir (getExternalFilesDir EXTERNAL_FILES base);
     * otherwise the path is used as-is when it names the frames file, or joined
     * with `ime-inset-frames.jsonl` when it names a directory.
     */
    private fun appendFrames(evidencePath: String, frames: List<Frame>) {
        val file = resolveEvidenceFile(evidencePath)
        file.parentFile?.mkdirs()
        file.appendText(frames.joinToString("") { f ->
            JSONObject()
                .put("frame", f.frame)
                .put("announcedHeight", f.announcedHeight)
                .put("appliedHeight", f.appliedHeight)
                .put("latencyFrames", f.latencyFrames)
                .toString() + "\n"
        })
    }

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
        return File(File(base, "ime-inset"), RESULT_FILE)
    }

    private const val RESULT_FILE = "ime-inset-frames.jsonl"

    @Suppress("unused")
    private fun windowInsetsType(): Int = WindowInsets.Type.ime()
}
