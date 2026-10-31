package com.muse.brownfieldsample

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import androidx.fragment.app.Fragment

/**
 * Pre-existing fragment, now hosting EngineView (FR-PVIEWS-010).
 *
 * Layout (`fragment_existing.xml`) contains
 * `<com.muse.pviews.EngineView android:id="@+id/engine_view" .../>`.
 * On engine init failure the host shows its own fallback view
 * (`R.id.engine_fallback`, T7 contract) instead of crashing.
 *
 * Shared-engine mode: [SecondFragment] attaches to the same engine
 * instance ([SharedEngineHolder]) with different UI state (isolation
 * proof for `uiIsolated: true`).
 */
class ExistingFragment : Fragment() {

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = inflater.inflate(R.layout.fragment_existing, container, false)

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val engineView: View? = view.findViewById(R.id.engine_view)
        val fallback: TextView? = view.findViewById(R.id.engine_fallback)
        val engine = SharedEngineHolder.acquire(requireContext())
        if (engine == null || engineView == null) {
            // Host-owned fallback (T7): engine init failure never crashes.
            engineView?.visibility = View.GONE
            fallback?.visibility = View.VISIBLE
        } else {
            fallback?.visibility = View.GONE
            engineView.visibility = View.VISIBLE
            engineView.contentDescription = "host-a"
        }
    }

    override fun onDestroyView() {
        SharedEngineHolder.release()
        super.onDestroyView()
    }
}
