// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.view.MotionEvent
import android.view.View

/**
 * The keyboard under the screen: the FM-1's 27 keys (F3 .. G5), with their LEDs (the grid's cells; the top two are
 * strike 1 and 2). Everything else is played on the screen itself; this is the one part of the case that stays.
 * (To come: the A / B / A+B switch for which split the keys play.)
 */
class PanelView(context: Context) : View(context) {
    private val d = resources.displayMetrics.density

    private val phosphor = 0xFF8DFFA0.toInt()
    private val dim = 0xFF3F8A4F.toInt()
    private val bg = 0xFF050806.toInt()

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 1.5f * d }

    private class Key(val bit: Int, val black: Boolean) { val r = RectF() }

    // F3 .. G5: bit 0 .. 26
    private val keys = (0 until 27).map { Key(it, (5 + it) % 12 in setOf(1, 3, 6, 8, 10)) }
    private val held = HashMap<Int, Key?>()     // finger -> the key under it
    private var keyMask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = 6f * d
        val whites = keys.count { !it.black }
        val kw = (w - 2 * pad) / whites
        // the keys no taller than a real keyboard's proportions; the rest of the space above them left dark
        val kh = minOf(h - 2 * pad, kw * 5.5f)
        val top = h - pad - kh
        val bottom = h - pad
        var wi = 0
        keys.forEach { k ->
            val x = pad + wi * kw
            if (!k.black) {
                k.r.set(x + 1f * d, top, x + kw - 1f * d, bottom)
                wi++
            } else k.r.set(x - kw * 0.32f, top, x + kw * 0.32f, top + kh * 0.6f)
        }
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(bg)
        val leds = Native.leds()
        val glow = Native.glow()
        for (pass in 0..1) keys.forEach { k ->
            if (k.black != (pass == 1)) return@forEach
            val down = (keyMask shr k.bit) and 1 != 0
            val id = 14 + k.bit
            val on = if ((leds shr id) and 1L != 0L) 2 else if ((glow shr id) and 1L != 0L) 1 else 0
            fill.color = when {
                down -> phosphor
                k.black -> 0xFF0C1A10.toInt()
                else -> 0xFF13241A.toInt()
            }
            canvas.drawRect(k.r, fill)
            line.color = dim
            canvas.drawRect(k.r, line)
            if (on > 0 && !down) {   // the key's LED
                fill.color = if (on == 2) phosphor else dim
                canvas.drawCircle(k.r.centerX(), k.r.bottom - k.r.width() * 0.45f, k.r.width() * 0.16f, fill)
            }
        }
        postInvalidateOnAnimation()
    }

    private fun keyAt(x: Float, y: Float): Key? =
        keys.firstOrNull { it.black && it.r.contains(x, y) } ?: keys.firstOrNull { !it.black && it.r.contains(x, y) }

    private fun sendKeys() {
        var m = 0
        held.values.forEach { k -> k?.let { m = m or (1 shl it.bit) } }
        if (m != keyMask) {
            keyMask = m
            Native.keys(m)
        }
    }

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = ev.actionIndex
                held[ev.getPointerId(i)] = keyAt(ev.getX(i), ev.getY(i))
                sendKeys()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until ev.pointerCount) {
                    val id = ev.getPointerId(i)
                    if (!held.containsKey(id)) continue
                    val k = keyAt(ev.getX(i), ev.getY(i))
                    if (k !== held[id]) { held[id] = k; sendKeys() }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                held.remove(ev.getPointerId(ev.actionIndex))
                sendKeys()
            }
            MotionEvent.ACTION_CANCEL -> { held.clear(); sendKeys() }
        }
        return true
    }
}
