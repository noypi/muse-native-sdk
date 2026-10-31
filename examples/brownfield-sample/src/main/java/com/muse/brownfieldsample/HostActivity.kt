package com.muse.brownfieldsample

import android.os.Bundle
import androidx.appcompat.app.AppCompatActivity

/**
 * Pre-existing host activity (stock app structure, unmodified except for
 * hosting [ExistingFragment]). Single lifecycle owner for all EngineView
 * hosts (A3: exactly-once dispatch). Shared-engine mode shows
 * [ExistingFragment] + [SecondFragment] on one engine; multi-engine mode
 * MAY show [MultiEngineFragment] on a second engine instance.
 */
class HostActivity : AppCompatActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_host)
        if (savedInstanceState == null) {
            supportFragmentManager.beginTransaction()
                .replace(R.id.host_container, ExistingFragment())
                .commit()
        }
    }
}
