package com.muse.brownfieldsample

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment

/**
 * Second host sharing the SAME engine instance as [ExistingFragment]
 * (shared mode, T6 `createHost(FRAGMENT, SHARED)` / T7 refcount teardown).
 * Renders different UI state (`contentDescription = "host-b"`) as the
 * isolation proof (`uiIsolated: true` in cold-start evidence).
 */
class SecondFragment : Fragment() {

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = inflater.inflate(R.layout.fragment_second, container, false)

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        val engineView: View? = view.findViewById(R.id.engine_view_b)
        SharedEngineHolder.acquire(requireContext())
        engineView?.contentDescription = "host-b"
    }

    override fun onDestroyView() {
        SharedEngineHolder.release()
        super.onDestroyView()
    }
}
