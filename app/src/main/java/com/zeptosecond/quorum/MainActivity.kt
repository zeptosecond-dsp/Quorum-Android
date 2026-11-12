// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.app.Activity
import android.app.AlertDialog
import android.graphics.Color
import android.graphics.Typeface
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.text.InputFilter
import android.text.InputType
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.EditText
import android.widget.FrameLayout

/** The engine and its rack face: the tube (touched as with a lightpen), two drives, two knobs, and each grid's row
 *  as a line of green lights. Landscape. */
class MainActivity : Activity() {
    private lateinit var crt: CrtView
    private val ui = Handler(Looper.getMainLooper())
    private var naming: AlertDialog? = null

    /** The Disk asks for a name: the phone's own keyboard, in a plain dialog. */
    private val namePoll = object : Runnable {
        override fun run() {
            if (naming == null) Native.nameAsk()?.let { askName(it.trimEnd()) }
            ui.postDelayed(this, 120)
        }
    }

    private fun askName(current: String) {
        val allowed = InputFilter { src, start, end, _, _, _ ->   // the disk's letters: A-Z 0-9 - _ and space
            val out = StringBuilder()
            for (i in start until end) {
                val c = src[i].uppercaseChar()
                if (c in 'A'..'Z' || c in '0'..'9' || c == '-' || c == '_' || c == ' ') out.append(c)
            }
            if (out.length == end - start && out.toString() == src.subSequence(start, end).toString()) null else out
        }
        val edit = EditText(this).apply {
            setText(current)
            setSelection(current.length)
            filters = arrayOf(allowed, InputFilter.LengthFilter(8))
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_FLAG_CAP_CHARACTERS or
                InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS or InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD
            typeface = Typeface.MONOSPACE
            isSingleLine = true
            selectAll()
        }
        val box = FrameLayout(this).apply {
            val pad = (20 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad / 2, pad, 0)
            addView(edit)
        }
        var answered = false
        val d = AlertDialog.Builder(this, android.R.style.Theme_DeviceDefault_Dialog_Alert)
            .setTitle("NAME THE PATCH")
            .setView(box)
            .setPositiveButton("OK") { _, _ -> answered = true; Native.nameSet(edit.text.toString(), true) }
            .setNegativeButton("CANCEL", null)
            .create()
        d.setOnDismissListener {
            if (!answered) Native.nameSet(null, false)
            naming = null
        }
        edit.setOnEditorActionListener { _, _, _ -> d.getButton(AlertDialog.BUTTON_POSITIVE).performClick(); true }
        d.window?.setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_VISIBLE)
        naming = d
        d.show()
        edit.requestFocus()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        Native.start(filesDir.absolutePath)

        crt = CrtView(this)
        val root = RackView(this, crt)
        root.setBackgroundColor(Color.rgb(176, 177, 179))
        // what is on the rack keeps clear of the cutout and any bars left showing; the plate itself fills the screen
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
        fullScreen()
    }

    /** The whole screen: the rack's plate runs under the camera's cutout, the bars are hidden (a swipe brings them). */
    private fun fullScreen() {
        if (Build.VERSION.SDK_INT >= 28)
            window.attributes = window.attributes.apply { layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES }
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior = WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility = (android.view.View.SYSTEM_UI_FLAG_FULLSCREEN or
                android.view.View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or android.view.View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                android.view.View.SYSTEM_UI_FLAG_LAYOUT_STABLE or android.view.View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                android.view.View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION)
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) fullScreen()
    }

    override fun onResume() {
        super.onResume()
        crt.onResume()
        ui.post(namePoll)
    }

    override fun onPause() {
        ui.removeCallbacks(namePoll)
        crt.onPause()
        super.onPause()
    }

    override fun onDestroy() {
        naming?.dismiss()                       // (turned round: the engine hears "cancelled", it asks again on NAME)
        super.onDestroy()
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
