// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.app.Activity
import android.graphics.Color
import android.os.Build
import android.os.Bundle
import android.view.WindowInsets
import android.view.WindowManager
import android.widget.LinearLayout

/** Step one: the engine, its own screen, and a stand-in FM-1 panel to play it with. */
class MainActivity : Activity() {
    private lateinit var crt: CrtView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        Native.start(filesDir.absolutePath)

        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Color.rgb(5, 8, 6))
        }
        crt = CrtView(this)
        root.addView(crt, LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT))
        root.addView(PanelView(this), LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f))
        // keep clear of the status bar, notches and the gesture bar (Android 15 draws apps edge to edge)
        root.setOnApplyWindowInsetsListener { v, insets ->
            if (Build.VERSION.SDK_INT >= 30) {
                val bars = insets.getInsets(WindowInsets.Type.systemBars() or WindowInsets.Type.displayCutout())
                v.setPadding(bars.left, bars.top, bars.right, bars.bottom)
            } else {
                @Suppress("DEPRECATION")
                v.setPadding(insets.systemWindowInsetLeft, insets.systemWindowInsetTop,
                    insets.systemWindowInsetRight, insets.systemWindowInsetBottom)
            }
            insets
        }
        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        crt.onResume()
    }

    override fun onPause() {
        crt.onPause()
        super.onPause()
    }

    override fun onStart() {
        super.onStart()
        Native.audioStart()
    }

    override fun onStop() {
        Native.audioStop()
        super.onStop()
    }
}
