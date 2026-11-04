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
 * The keyboard under the screen, dressed as Buchla's Electric Music Box touch keyboard: pale ribbed plates on a navy
 * panel, each raised key ringed in red, a red band along the top. On the band: STRIKE 1 at the far left, the split
 * the keys play (A, A+B, B) in the middle, STRIKE 2 at the far right, all plates like the keys.
 * The 27 keys are the FM-1's (F3 .. G5) with their lights (the grid's cells; the top two are strike 1 and 2).
 */
class PanelView(context: Context) : View(context) {
    private val d = resources.displayMetrics.density

    private val navy = 0xFF2B3478.toInt()
    private val navyDeep = 0xFF1C2252.toInt()
    private val red = 0xFFC23A4C.toInt()
    private val plate = 0xFFD3D9E1.toInt()
    private val plateDim = 0xFFA9B1BD.toInt()
    private val platePress = 0xFF8E97A6.toInt()
    private val rib = 0x14000000

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG)
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
    }

    private class Key(val bit: Int, val black: Boolean) { val r = RectF() }
    private class Btn(val label: String, val act: Int) { val r = RectF() }   // act: 1..3 the split, 10/11 strike 1/2

    // F3 .. G5: bit 0 .. 26
    private val keys = (0 until 27).map { Key(it, (5 + it) % 12 in setOf(1, 3, 6, 8, 10)) }
    private val btns = listOf(Btn("STRIKE 1", 10), Btn("A", 1), Btn("A+B", 2), Btn("B", 3), Btn("STRIKE 2", 11))
    private val band = RectF()
    private val held = HashMap<Int, Key?>()     // finger -> the key under it
    private val pressed = HashMap<Int, Btn>()   // finger -> the button under it
    private var keyMask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = 6f * d
        val whites = keys.count { !it.black }
        val kw = (w - 2 * pad) / whites
        val bandH = minOf(46f * d, h * 0.22f)
        // the keys no taller than a real keyboard's proportions; the rest of the space above them left as panel
        val kh = minOf(h - 2 * pad - bandH - pad, kw * 5.5f)
        val bottom = h - pad
        val top = bottom - kh
        band.set(0f, top - pad - bandH, w.toFloat(), top - pad)
        var wi = 0
        keys.forEach { k ->
            val x = pad + wi * kw
            if (!k.black) {
                k.r.set(x + 2.5f * d, top, x + kw - 2.5f * d, bottom)
                wi++
            } else k.r.set(x - kw * 0.30f, top, x + kw * 0.30f, top + kh * 0.58f)
        }
        // on the band: the strikes at the ends, the split in the middle
        val bh = band.height() - 12f * d
        val by = band.top + 6f * d
        val sw = minOf(96f * d, w * 0.2f)
        val mw = minOf(64f * d, w * 0.12f)
        btns[0].r.set(pad, by, pad + sw, by + bh)
        btns[4].r.set(w - pad - sw, by, w - pad, by + bh)
        val gap = 8f * d
        val x0 = w / 2f - (3 * mw + 2 * gap) / 2f
        for (i in 0..2) btns[1 + i].r.set(x0 + i * (mw + gap), by, x0 + i * (mw + gap) + mw, by + bh)
        text.textSize = minOf(bh * 0.42f, 15f * d)
    }

    private fun ribbed(c: Canvas, r: RectF, color: Int) {   // a touch plate: pale, with fine horizontal ribs
        fill.color = color
        c.drawRect(r, fill)
        fill.color = rib
        var y = r.top + 2f * d
        while (y < r.bottom) { c.drawRect(r.left, y, r.right, y + 0.7f * d, fill); y += 2.6f * d }
    }

    override fun onDraw(canvas: Canvas) {
        canvas.drawColor(navy)
        val leds = Native.leds()
        val glow = Native.glow()
        val route = Native.routeGet()
        val lit = Native.strikeLit()

        // the red band and its plates
        fill.color = red
        canvas.drawRect(band, fill)
        btns.forEach { b ->
            val on = when (b.act) { in 1..3 -> b.act == route; 10 -> lit and 1 != 0; else -> lit and 2 != 0 }
            val down = pressed.containsValue(b)
            fill.color = navyDeep
            canvas.drawRect(b.r.left - 1.5f * d, b.r.top - 1.5f * d, b.r.right + 1.5f * d, b.r.bottom + 1.5f * d, fill)
            ribbed(canvas, b.r, if (down) platePress else if (on) plate else plateDim)
            if (on) {                           // chosen: a red bar along its foot
                fill.color = red
                canvas.drawRect(b.r.left + 4f * d, b.r.bottom - 5f * d, b.r.right - 4f * d, b.r.bottom - 2.5f * d, fill)
            }
            text.color = navyDeep
            canvas.drawText(b.label, b.r.centerX(), b.r.centerY() + text.textSize * 0.36f, text)
        }

        // the keys: the long plates, then the raised ones ringed in red
        for (pass in 0..1) keys.forEach { k ->
            if (k.black != (pass == 1)) return@forEach
            val down = (keyMask shr k.bit) and 1 != 0
            if (k.black) {
                fill.color = navy
                canvas.drawRect(k.r.left - 5f * d, k.r.top, k.r.right + 5f * d, k.r.bottom + 5f * d, fill)
                fill.color = red
                canvas.drawRect(k.r.left - 3f * d, k.r.top, k.r.right + 3f * d, k.r.bottom + 3f * d, fill)
            }
            ribbed(canvas, k.r, if (down) platePress else plate)
            val id = 14 + k.bit
            val on = if ((leds shr id) and 1L != 0L) 2 else if ((glow shr id) and 1L != 0L) 1 else 0
            if (on > 0 && !down) {              // the key's light
                fill.color = if (on == 2) red else 0x99C23A4C.toInt()
                canvas.drawCircle(k.r.centerX(), k.r.bottom - k.r.width() * 0.45f, k.r.width() * 0.16f, fill)
            }
        }
        postInvalidateOnAnimation()
    }

    private fun keyAt(x: Float, y: Float): Key? =
        keys.firstOrNull { it.black && it.r.contains(x, y) } ?: keys.firstOrNull { !it.black && it.r.contains(x, y) }

    private fun btnAt(x: Float, y: Float): Btn? = btns.firstOrNull {
        x >= it.r.left - 4f * d && x <= it.r.right + 4f * d && y >= it.r.top - 4f * d && y <= it.r.bottom + 4f * d
    }

    private fun sendKeys() {
        var m = 0
        held.values.forEach { k -> k?.let { m = m or (1 shl it.bit) } }
        if (m != keyMask) {
            keyMask = m
            Native.keys(m)
        }
    }

    private fun press(b: Btn) {
        when (b.act) {
            in 1..3 -> Native.route(b.act)
            10, 11 -> Native.strike(b.act - 10, true)
        }
    }

    private fun release(b: Btn) {
        if (b.act >= 10) Native.strike(b.act - 10, false)
    }

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = ev.actionIndex
                val id = ev.getPointerId(i)
                val b = btnAt(ev.getX(i), ev.getY(i))
                if (b != null) { pressed[id] = b; press(b) }
                else { held[id] = keyAt(ev.getX(i), ev.getY(i)); sendKeys() }
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
                val id = ev.getPointerId(ev.actionIndex)
                pressed.remove(id)?.let { release(it) }
                held.remove(id)
                sendKeys()
            }
            MotionEvent.ACTION_CANCEL -> {
                pressed.values.forEach { release(it) }
                pressed.clear(); held.clear(); sendKeys()
            }
        }
        return true
    }
}
