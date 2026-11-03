// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View

/**
 * A stand-in FM-1 panel for the first builds: the seven encoders as drag strips, the fourteen buttons and the
 * 27 keys, with their LEDs. It lets the unchanged firmware be played on the phone until the lightpen pages take over.
 */
class PanelView(context: Context) : View(context) {
    private val d = resources.displayMetrics.density

    private val phosphor = 0xFF8DFFA0.toInt()
    private val dim = 0xFF3F8A4F.toInt()
    private val faint = 0xFF16351F.toInt()
    private val ink = 0xFF031006.toInt()
    private val bg = 0xFF050806.toInt()

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val line = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE; strokeWidth = 1.5f * d }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.MONOSPACE; textAlign = Paint.Align.CENTER; textSize = 11f * d
    }

    private class Enc(val id: Int, val label: String) { val r = RectF(); var turns = 0 }
    private class Btn(val id: Int, val label: String) { val r = RectF() }
    private class Key(val bit: Int, val black: Boolean) { val r = RectF() }

    private val encs = listOf(
        Enc(Native.ENC_SELECT, "SELECT"), Enc(Native.ENC_ALGORITHM, "ALGO"), Enc(Native.ENC_PRESETS, "PRESETS"),
        Enc(Native.ENC_KNOB1, "K1"), Enc(Native.ENC_KNOB2, "K2"), Enc(Native.ENC_KNOB3, "K3"), Enc(Native.ENC_KNOB4, "K4"),
    )
    private val btns = listOf(
        Btn(Native.BTN_HOME, "HOME"), Btn(Native.BTN_EDIT, "EDIT"), Btn(Native.BTN_SEL, "SEL"), Btn(Native.BTN_ENV, "ENV"),
        Btn(Native.BTN_LFO, "LFO"), Btn(Native.BTN_FX, "FX"), Btn(Native.BTN_GLO, "GLO"),
        Btn(Native.BTN_OCT_DOWN, "OCT-"), Btn(Native.BTN_OCT_UP, "OCT+"), Btn(Native.BTN_ARP, "ARP"),
        Btn(Native.BTN_SEQ, "SEQ"), Btn(Native.BTN_PLAY, "PLAY"), Btn(Native.BTN_REC, "REC"), Btn(Native.BTN_SAVE, "SAVE"),
    )
    // F3 .. G5: bit 0 .. 26
    private val keys = (0 until 27).map { Key(it, (5 + it) % 12 in setOf(1, 3, 6, 8, 10)) }

    // what each finger holds
    private sealed class Hold {
        class OnEnc(val e: Enc, var lastX: Float, var lastY: Float, var acc: Float = 0f) : Hold()
        class OnBtn(val b: Btn) : Hold()
        class OnKey(var k: Key?) : Hold()
    }
    private val holds = HashMap<Int, Hold>()
    private var keyMask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = 6f * d
        val gap = 5f * d
        val encH = h * 0.2f
        val btnH = h * 0.32f
        val keyTop = encH + btnH + gap
        // encoders
        val ew = (w - 2 * pad - 6 * gap) / 7f
        encs.forEachIndexed { i, e ->
            val x = pad + i * (ew + gap)
            e.r.set(x, pad, x + ew, encH - gap / 2)
        }
        // buttons: two rows of seven
        val bw = (w - 2 * pad - 6 * gap) / 7f
        val bh = (btnH - 2 * gap) / 2f
        btns.forEachIndexed { i, b ->
            val x = pad + (i % 7) * (bw + gap)
            val y = encH + gap / 2 + (i / 7) * (bh + gap)
            b.r.set(x, y, x + bw, y + bh)
        }
        // keys: 16 white, the black ones over their gaps
        val whites = keys.filter { !it.black }
        val kw = (w - 2 * pad) / whites.size
        val kb = h - pad
        var wi = 0
        keys.forEach { k ->
            if (!k.black) {
                val x = pad + wi * kw
                k.r.set(x + 1f * d, keyTop, x + kw - 1f * d, kb)
                wi++
            } else {
                val x = pad + wi * kw
                k.r.set(x - kw * 0.32f, keyTop, x + kw * 0.32f, keyTop + (kb - keyTop) * 0.6f)
            }
        }
    }

    private fun lit(leds: Long, glow: Long, id: Int): Int =
        if ((leds shr id) and 1L != 0L) 2 else if ((glow shr id) and 1L != 0L) 1 else 0

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(bg)
        val leds = Native.leds()
        val glow = Native.glow()
        val corner = 4f * d
        val ty = (text.descent() + text.ascent()) / 2

        encs.forEach { e ->
            line.color = dim
            canvas.drawRoundRect(e.r, corner, corner, line)
            // a tick that turns with it
            val cx = e.r.centerX()
            val cy = e.r.centerY() + 6f * d
            val rad = minOf(e.r.width(), e.r.height()) * 0.24f
            canvas.drawCircle(cx, cy, rad, line)
            val a = Math.toRadians((e.turns * 15.0) % 360.0 - 90.0)
            line.color = phosphor
            canvas.drawLine(cx, cy, cx + (rad * Math.cos(a)).toFloat(), cy + (rad * Math.sin(a)).toFloat(), line)
            text.color = dim
            canvas.drawText(e.label, cx, e.r.top + 12f * d, text)
        }
        btns.forEach { b ->
            val on = lit(leds, glow, b.id)
            val held = holds.values.any { it is Hold.OnBtn && it.b === b }
            fill.color = when { held || on == 2 -> phosphor; on == 1 -> faint; else -> bg }
            canvas.drawRoundRect(b.r, corner, corner, fill)
            line.color = if (on == 2 || held) phosphor else dim
            canvas.drawRoundRect(b.r, corner, corner, line)
            text.color = if (held || on == 2) ink else phosphor
            canvas.drawText(b.label, b.r.centerX(), b.r.centerY() - ty, text)
        }
        // white keys first, then the black ones over them
        for (pass in 0..1) keys.forEach { k ->
            if (k.black != (pass == 1)) return@forEach
            val down = (keyMask shr k.bit) and 1 != 0
            val on = lit(leds, glow, 14 + k.bit)
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

    private fun hit(x: Float, y: Float): Hold? {
        encs.firstOrNull { it.r.contains(x, y) }?.let { return Hold.OnEnc(it, x, y) }
        btns.firstOrNull { it.r.contains(x, y) }?.let { return Hold.OnBtn(it) }
        keyAt(x, y)?.let { return Hold.OnKey(it) }
        return null
    }

    private fun keyAt(x: Float, y: Float): Key? =
        keys.firstOrNull { it.black && it.r.contains(x, y) } ?: keys.firstOrNull { !it.black && it.r.contains(x, y) }

    private fun sendKeys() {
        var m = 0
        holds.values.forEach { h -> if (h is Hold.OnKey) h.k?.let { m = m or (1 shl it.bit) } }
        if (m != keyMask) {
            keyMask = m
            Native.keys(m)
        }
    }

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = ev.actionIndex
                val h = hit(ev.getX(i), ev.getY(i)) ?: return true
                holds[ev.getPointerId(i)] = h
                if (h is Hold.OnBtn) Native.button(h.b.id, true)
                if (h is Hold.OnKey) sendKeys()
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until ev.pointerCount) {
                    val h = holds[ev.getPointerId(i)] ?: continue
                    val x = ev.getX(i)
                    val y = ev.getY(i)
                    when (h) {
                        is Hold.OnEnc -> {
                            // up or right turns clockwise; one detent every 12 dp
                            h.acc += (x - h.lastX) - (y - h.lastY)
                            h.lastX = x; h.lastY = y
                            val step = 12f * d
                            val n = (h.acc / step).toInt()
                            if (n != 0) {
                                h.acc -= n * step
                                h.e.turns += n
                                Native.enc(h.e.id, n)
                            }
                        }
                        is Hold.OnKey -> {
                            val k = keyAt(x, y)
                            if (k !== h.k) { h.k = k; sendKeys() }
                        }
                        is Hold.OnBtn -> {}
                    }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                val ids = if (ev.actionMasked == MotionEvent.ACTION_CANCEL) (0 until ev.pointerCount).map { ev.getPointerId(it) }
                          else listOf(ev.getPointerId(ev.actionIndex))
                ids.forEach { id ->
                    when (val h = holds.remove(id)) {
                        is Hold.OnBtn -> Native.button(h.b.id, false)
                        is Hold.OnKey -> sendKeys()
                        else -> {}
                    }
                }
            }
        }
        return true
    }

}
