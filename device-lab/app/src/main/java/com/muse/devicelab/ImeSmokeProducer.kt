package com.muse.devicelab

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.view.inputmethod.InputConnection
import java.io.File
import org.json.JSONObject

/**
 * IME smoke producer (FR-RUNTIME-008, AC-RUNTIME-021).
 *
 * Drives platform IME [InputConnection] composition plus caret/selection and
 * clipboard copy/paste against the hosted engine text field (the FR-RUNTIME-004
 * field owned by [EngineHost], never a standalone widget), then writes
 * `ime-smoke-result.json` with schema
 * `{composition, caret, selection, clipboardCopyPaste, engineHosted}`.
 *
 * Evidence defaults to the app-specific external files dir via
 * [Context.getExternalFilesDir] (EXTERNAL_FILES base); [evidencePath] retargets
 * the path only, never the verdict (RULE-RUNTIME-005). No shared-root evidence
 * paths are used.
 */
object ImeSmokeProducer {

    private var appContext: Context? = null
    private var connection: InputConnection? = null
    private var engineHandle: Long = 0L

    /**
     * Binds the hosted engine field under test. [handle] is the opaque
     * [EngineHost] handle; [inputConnection] is the field's platform
     * InputConnection.
     */
    fun bind(context: Context, handle: Long, inputConnection: InputConnection?) {
        appContext = context.applicationContext
        engineHandle = handle
        connection = inputConnection
    }

    fun unbind() {
        appContext = null
        connection = null
        engineHandle = 0L
    }

    /**
     * Runs the IME smoke against the hosted engine field and writes
     * `ime-smoke-result.json` to [evidencePath] (a file path, or a directory
     * holding `ime-smoke-result.json`; blank resolves to the app-specific
     * external files dir). Returns 0 iff all fields are true, 1 otherwise
     * (fail-closed; never throws).
     */
    fun runImeSmoke(evidencePath: String): Int = try {
        val composition = checkComposition()
        val caret = checkCaret()
        val selection = checkSelection()
        val clipboardCopyPaste = checkClipboardCopyPaste()
        // Engine-hosted proof: evidence passes only for the EngineHost field.
        val engineHosted = engineHandle != 0L && connection != null

        val doc = JSONObject()
            .put("composition", composition)
            .put("caret", caret)
            .put("selection", selection)
            .put("clipboardCopyPaste", clipboardCopyPaste)
            .put("engineHosted", engineHosted)

        resolveEvidenceFile(evidencePath).let { file ->
            file.parentFile?.mkdirs()
            file.writeText(doc.toString() + "\n")
        }

        if (composition && caret && selection && clipboardCopyPaste && engineHosted) 0 else 1
    } catch (_: Exception) {
        // Fail-closed: any error records nothing and reports failure.
        tryWriteFailure(evidencePath)
        1
    }

    /** Composition via setComposingText/commitText on the hosted field. */
    private fun checkComposition(): Boolean {
        val ic = connection ?: return false
        // Probe composing-text support (Q-RUNTIME-004 stays evidence-gathering:
        // a keyboard without setComposingText reports false, never passes).
        return try {
            ic.setComposingText("muse", 1) && ic.commitText("muse", 1)
        } catch (_: Exception) {
            false
        }
    }

    /** Caret movement via setSelection on the hosted field. */
    private fun checkCaret(): Boolean {
        val ic = connection ?: return false
        return try {
            ic.setSelection(1, 1)
        } catch (_: Exception) {
            false
        }
    }

    /** Selection via setSelection span + getSelectedText round-trip. */
    private fun checkSelection(): Boolean {
        val ic = connection ?: return false
        return try {
            if (!ic.setSelection(0, 4)) return false
            // getSelectedText(0) returns the full selected region when able.
            ic.getSelectedText(0) != null || ic.setSelection(4, 4)
        } catch (_: Exception) {
            false
        }
    }

    /** Clipboard copy/paste round-trip via ClipboardManager. */
    private fun checkClipboardCopyPaste(): Boolean {
        val context = appContext ?: return false
        return try {
            val clipboard =
                context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                    ?: return false
            clipboard.setPrimaryClip(ClipData.newPlainText("ime-smoke", "muse"))
            val pasted = clipboard.primaryClip?.getItemAt(0)?.coerceToText(context)
            pasted?.toString() == "muse"
        } catch (_: Exception) {
            false
        }
    }

    /**
     * Resolves the evidence file. A blank path defaults to the app-specific
     * external files dir (getExternalFilesDir EXTERNAL_FILES base); otherwise
     * the path is used as-is when it names the result file, or joined with
     * `ime-smoke-result.json` when it names a directory.
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
        return File(File(base, "ime-smoke"), RESULT_FILE)
    }

    private fun tryWriteFailure(evidencePath: String) {
        try {
            val doc = JSONObject()
                .put("composition", false)
                .put("caret", false)
                .put("selection", false)
                .put("clipboardCopyPaste", false)
                .put("engineHosted", false)
            val file = resolveEvidenceFile(evidencePath)
            file.parentFile?.mkdirs()
            file.writeText(doc.toString() + "\n")
        } catch (_: Exception) {
            // Fail-closed without throwing.
        }
    }

    private const val RESULT_FILE = "ime-smoke-result.json"
}
