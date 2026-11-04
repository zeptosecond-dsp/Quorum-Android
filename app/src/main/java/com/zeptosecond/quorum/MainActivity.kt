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
import android.view.WindowManager
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.LinearLayout

/** Step one: the engine, its own screen, and a stand-in FM-1 panel to play it with. */
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
