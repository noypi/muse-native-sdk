package com.muse.brownfieldsample

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.fragment.app.Fragment

/**
 * Optional multi-engine screen (MAY exist per sample contract): hosts a
 * SECOND engine instance, independent of [SharedEngineHolder]. Shared mode
 * ([ExistingFragment] + [SecondFragment]) MUST be shown; this screen is
 * additive only.
 */
class MultiEngineFragment : Fragment() {

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View = inflater.inflate(R.layout.fragment_multi_engine, container, false)
}
