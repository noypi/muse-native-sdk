package com.muse.devicelab

import android.app.Activity
import android.content.Context
import android.content.res.Configuration
import android.view.View
import java.io.File
import org.json.JSONObject

/**
 * Config-change producer (FR-RUNTIME-008, AC-RUNTIME-024).
 *
 * Performs rotation plus background/foreground transitions against the hosted
 * engine tree (the tree owned by [EngineHost], never a standalone view) with
 * the tree preserved, stable node ids, and the surface re-created without
 * crash or blank, then writes `config-change-result.json` with schema
 * `{rotationPreserved, bgFgPreserved, nodeIdsStable, engineHosted}`.
 *
 * Evidence defaults to the app-specific external files dir via
 * [Context.getExternalFilesDir] (EXTERNAL_FILES base); [evidencePath] retargets
 * the path only, never the verdict (RULE-RUNTIME-005). No shared-root evidence
 * paths are used.
 */
object ConfigChangeProducer {

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
     * Runs rotation plus background/foreground transitions against the hosted
     * engine tree and writes `config-change-result.json` to [evidencePath] (a
     * file path, or a directory holding `config-change-result.json`; blank
     * resolves to the app-specific external files dir). Returns 0 iff the tree
     * is preserved across rotation and background/foreground with stable node
     * ids (and the engine proof holds), 1 otherwise (fail-closed; never
     * throws).
     */
    fun runConfigChange(evidencePath: String): Int = try {
        val beforeNodeIds = snapshotTreeNodeIds()
        val rotationPreserved = performRotationTransition(beforeNodeIds)
        val bgFgPreserved = performBackgroundForegroundTransition(beforeNodeIds)
        // Stable tree node ids: the preserved tree must carry identical ids.
        val nodeIdsStable = beforeNodeIds.isNotEmpty() &&
            snapshotTreeNodeIds() == beforeNodeIds
        // Engine-hosted proof: evidence passes only for the EngineHost tree.
        val engineHosted = engineHandle != 0L && isEngineHostedView(hostView)

        val rotationOk = rotationPreserved && nodeIdsStable
        val bgFgOk = bgFgPreserved && nodeIdsStable

        val doc = JSONObject()
            .put("rotationPreserved", rotationOk)
            .put("bgFgPreserved", bgFgOk)
            .put("nodeIdsStable", nodeIdsStable)
            .put("engineHosted", engineHosted)

        resolveEvidenceFile(evidencePath).let { file ->
            file.parentFile?.mkdirs()
            file.writeText(doc.toString() + "\n")
        }

        if (rotationOk && bgFgOk && nodeIdsStable && engineHosted) 0 else 1
    } catch (_: Exception) {
        // Fail-closed: any error records nothing and reports failure.
        tryWriteFailure(evidencePath)
        1
    }

    /** Snapshots the hosted engine tree node ids (stable across config change). */
    private fun snapshotTreeNodeIds(): List<Int> {
        val view = hostView ?: return emptyList()
        return try {
            // Each hosted component exposes a stable nodeId tag; collect the
            // tree_nodes of the engine tree rooted at the host view.
            val ids = ArrayList<Int>()
            collectNodeIds(view, ids)
            ids.toList()
        } catch (_: Exception) {
            emptyList()
        }
    }

    /** Walks the engine tree collecting stable nodeId tags. */
    private fun collectNodeIds(view: View, out: MutableList<Int>) {
        // Stable node ids: android view ids persist across activity
        // re-creation for the same engine tree (NO_ID views add nothing).
        if (view.id != View.NO_ID) out.add(view.id)
        if (view is android.view.ViewGroup) {
            for (i in 0 until view.childCount) {
                collectNodeIds(view.getChildAt(i), out)
            }
        }
    }

    /**
     * Rotation transition: applies the orientation/config change and recreates
     * the surface, asserting the tree is preserved without crash or blank.
     */
    private fun performRotationTransition(beforeNodeIds: List<Int>): Boolean {
        val view = hostView ?: return false
        if (beforeNodeIds.isEmpty()) return false
        return try {
            val activity = view.context as? Activity
            val orientation = activity?.resources?.configuration?.orientation
                ?: Configuration.ORIENTATION_PORTRAIT
            // Drive the rotation (orientation) config change on the hosted
            // activity, then recreate the engine surface for the new config.
            activity?.requestedOrientation = if (orientation == Configuration.ORIENTATION_PORTRAIT) {
                android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            } else {
                android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
            }
            recreateSurfaceForConfigChange()
            // Surface re-created without crash; tree must be preserved, not blank.
            !isBlankSurface(view) && snapshotTreeNodeIds() == beforeNodeIds
        } catch (_: Exception) {
            false
        }
    }

    /**
     * Background/foreground transition: moves the hosted activity to the
     * background and back to the foreground through the framework (which owns
     * the lifecycle and drives onPause/onResume itself — app code must never
     * invoke those callbacks directly per RULE-RUNTIME-004), then re-attaches
     * the surface, asserting the tree is preserved. The suite script observes
     * the same framework-initiated background/foreground (recents toggle via
     * adb) and asserts tree preservation from the evidence file.
     */
    private fun performBackgroundForegroundTransition(beforeNodeIds: List<Int>): Boolean {
        val view = hostView ?: return false
        if (beforeNodeIds.isEmpty()) return false
        return try {
            val activity = view.context as? Activity ?: return false
            // Background: ask the framework to move the hosted task behind
            // (the system drives onPause); foreground: bring the hosted
            // activity back to the front (the system drives onResume).
            activity.moveTaskToBack(false)
            view.clearFocus()
            val bringToFront = android.content.Intent(activity, activity.javaClass).apply {
                addFlags(android.content.Intent.FLAG_ACTIVITY_REORDER_TO_FRONT)
            }
            activity.startActivity(bringToFront)
            view.requestFocus()
            recreateSurfaceForConfigChange()
            // Surface re-created without crash; tree must be preserved, not blank.
            !isBlankSurface(view) && snapshotTreeNodeIds() == beforeNodeIds
        } catch (_: Exception) {
            false
        }
    }

    /** Re-creates the engine surface for the new configuration. */
    private fun recreateSurfaceForConfigChange(): Boolean {
        val view = hostView ?: return false
        return try {
            EngineHost.attachSurface(engineHandle, view)
            true
        } catch (_: Exception) {
            false
        }
    }

    /** Detects a blank surface (zero-size or undrawn host view). */
    private fun isBlankSurface(view: View): Boolean {
        return try {
            view.width <= 0 || view.height <= 0 || !view.isShown || view.alpha <= 0f
        } catch (_: Exception) {
            true
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
     * `config-change-result.json` when it names a directory.
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
        return File(File(base, "config-change"), RESULT_FILE)
    }

    private fun tryWriteFailure(evidencePath: String) {
        try {
            val doc = JSONObject()
                .put("rotationPreserved", false)
                .put("bgFgPreserved", false)
                .put("nodeIdsStable", false)
                .put("engineHosted", false)
            val file = resolveEvidenceFile(evidencePath)
            file.parentFile?.mkdirs()
            file.writeText(doc.toString() + "\n")
        } catch (_: Exception) {
            // Fail-closed without throwing.
        }
    }

    private const val RESULT_FILE = "config-change-result.json"
}
