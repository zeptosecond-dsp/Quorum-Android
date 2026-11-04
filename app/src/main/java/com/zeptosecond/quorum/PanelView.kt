// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.View

/**
 * The keyboard under the screen, dressed as Buchla's Electric Music Box touch keyboard: pale ribbed plates on a navy
 * panel, each raised key ringed in red, a red band along the top. On the band: STRIKE 1 at the far left, the split
 * the keys play (A, A+B, B) in the middle, STRIKE 2 at the far right, all plates like the keys. Over the rule, above
 * STRIKE 2, VINTAGE: the disk and the tube's faults all on or all off at once (off: a clean screen to set them from).
 * Over the rule at the left, the arrows and SELECT: a box moved between what can be touched on the screen, SELECT a tap
 * on it (for when the tube is too far gone to aim at); a value opened, a slider over them sets it.
 * The 27 keys are the FM-1's (F3 .. G5), plain plates: the grids show in the lights above.
 * Above it all, on black under a thin navy rule: each grid's row now as square red lights, A on the left, B on the
 * right; touched (or swept) they turn over, as the cells on the Grids page. While an FG is chosen (its page, or its
 * row on the mod matrix) all 64 show its level instead, as the FM-1 S6's keys: a dot, the way up glowing. At boot,
 * while the petri dish grows, two quick sweeps, A and B going opposite ways.
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
    private val btns = listOf(Btn("STRIKE 1", 10), Btn("A", 1), Btn("A+B", 2), Btn("B", 3), Btn("STRIKE 2", 11),
        Btn("VINTAGE", 20),                    // (over the rule, above STRIKE 2: the disk and the tube's faults, all at once)
        Btn("", 30), Btn("", 31), Btn("SELECT", 34), Btn("", 32), Btn("", 33))   // (over the rule, at the left: left, up,
                                                // SELECT, down, right: the screen without touching it; 30.. as Native.nav)
    private val band = RectF()
    private val slider = RectF()                // (the open value's slider, over the arrows)
    private val sliding = HashSet<Int>()        // fingers on it
    private val tri = Path()
    private var navHeld: Btn? = null            // an arrow held: it repeats
    private var navNext = 0L
    private val strip = RectF()                 // the lights' row
    private val rule = RectF()                  // the thin navy line over it
    private val ledX = FloatArray(2)            // where each grid's 32 lights start
    private var ledPitch = 0f
    private val swept = HashMap<Int, Int>()     // finger -> the light it last turned (g * 32 + cell)
    private val held = HashMap<Int, Key?>()     // finger -> the key under it
    private val pressed = HashMap<Int, Btn>()   // finger -> the button under it
    private var keyMask = 0

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        val pad = 6f * d
        val whites = keys.count { !it.black }
        val kw = (w - 2 * pad) / whites
        val bandH = minOf(46f * d, h * 0.22f)
        // the keys no taller than a real keyboard's proportions; room above for the band, the lights and their rule
        val vbH = minOf(34f * d, bandH - 12f * d)   // the VINTAGE plate, as tall as the band's
        val slH = 22f * d                       // the slider's row, over the arrows (shown while a value is open)
        val kh = maxOf(20f * d, minOf(h - 8 * pad - bandH - 26f * d - vbH - slH, kw * 5.5f))
        val bottom = h - pad
        val top = bottom - kh
        band.set(0f, top - pad - bandH, w.toFloat(), top - pad)
        val stripH = 26f * d
        strip.set(0f, band.top - stripH, w.toFloat(), band.top)
        rule.set(0f, strip.top - pad, w.toFloat(), strip.top)
        val half = (w - 3 * pad) / 2f           // A: the left half, B: the right
        ledPitch = half / 32f
        ledX[0] = pad
        ledX[1] = 2 * pad + half
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
        btns[5].r.set(w - pad - sw, rule.top - pad - vbH, w - pad, rule.top - pad)
        // the arrows and SELECT: left, up, SELECT, down, right, along the left
        val aw = minOf(44f * d, w * 0.085f)
        val selW = minOf(84f * d, w * 0.17f)
        val ag = 6f * d
        var x = pad
        for (i in 6..10) {
            val bw = if (i == 8) selW else aw
            btns[i].r.set(x, rule.top - pad - vbH, x + bw, rule.top - pad)
            x += bw + ag
        }
        slider.set(pad, btns[6].r.top - pad - slH, x - ag, btns[6].r.top - pad)
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
        canvas.drawColor(0xFF000000.toInt())
        fill.color = navy
        canvas.drawRect(rule, fill)
        canvas.drawRect(0f, band.top, width.toFloat(), height.toFloat(), fill)
        drawLights(canvas)
        val route = Native.routeGet()
        val lit = Native.strikeLit()
        val vint = Native.vintageGet()

        // the red band and its plates
        fill.color = red
        canvas.drawRect(band, fill)
        btns.forEach { b ->
            val nav = b.act >= 30                   // (the arrows, SELECT: plain plates, no state)
            val on = when (b.act) { in 1..3 -> b.act == route; 10 -> lit and 1 != 0; 20 -> vint; 11 -> lit and 2 != 0; else -> false }
            val down = pressed.containsValue(b)
            fill.color = navyDeep
            canvas.drawRect(b.r.left - 1.5f * d, b.r.top - 1.5f * d, b.r.right + 1.5f * d, b.r.bottom + 1.5f * d, fill)
            ribbed(canvas, b.r, if (down) platePress else if (on || nav) plate else plateDim)
            if (on) {                           // chosen: a red bar along its foot
                fill.color = red
                canvas.drawRect(b.r.left + 4f * d, b.r.bottom - 5f * d, b.r.right - 4f * d, b.r.bottom - 2.5f * d, fill)
            }
            text.color = navyDeep
            if (b.act in 30..33) arrow(canvas, b)
            else canvas.drawText(b.label, b.r.centerX(), b.r.centerY() + text.textSize * 0.36f, text)
        }
        drawSlider(canvas)
        navHeld?.let { b ->                     // an arrow held: again, every so often
            val now = android.os.SystemClock.uptimeMillis()
            if (now >= navNext) { Native.nav(NAV_DIR[b.act - 30]); navNext = now + 110L }
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
        }
        postInvalidateOnAnimation()
    }

    // the lights: 0 dark .. 1 lit, by place (0..63, A's 32 then B's)
    private val lv = FloatArray(64)
    private var sweepT0 = -1L                   // the boot sweep's start (-1: not yet; -2: done or missed)
    private var lightsTouch = true              // the cells can be turned over (not while an FG or the sweep shows)

    private fun lerp(a: Int, b: Int, t: Float): Int {
        fun ch(sh: Int) = (((a shr sh) and 255) + (((b shr sh) and 255) - ((a shr sh) and 255)) * t).toInt() shl sh
        return (0xFF shl 24) or ch(16) or ch(8) or ch(0)
    }

    private fun drawLights(canvas: Canvas) {
        val now = android.os.SystemClock.uptimeMillis()
        if (sweepT0 == -1L) sweepT0 = if (Native.booting() != 0) now else -2L
        val fg = Native.fgLevel()
        val sweepMs = 2400L                     // two there-and-backs: A left to right, B mirrored
        if (sweepT0 >= 0 && now - sweepT0 < sweepMs) {
            val ph = ((now - sweepT0) % 1200L) / 1200f * 2f            // 0..2: out and back
            val head = (if (ph < 1f) ph else 2f - ph) * 31f
            val dir = if (ph < 1f) 1f else -1f
            for (c in 0 until 32) {
                val behind = (head - c) * dir                          // how far the tail trails
                val v = if (behind < -0.5f) 0f else if (behind < 0.5f) 1f else maxOf(0f, 1f - behind / 4.5f) * 0.7f
                lv[c] = v; lv[63 - c] = v
            }
            lightsTouch = false
        } else if (fg >= 0) {                   // an FG: a dot at its level, the way up glowing (one block of 64)
            val pos = fg * 63 / 32767
            for (i in 0 until 64) lv[i] = if (i == pos) 1f else if (i < pos) 0.38f else 0f
            lightsTouch = false
        } else {
            for (g in 0..1) {
                val row = Native.row(g)
                for (c in 0 until 32) lv[g * 32 + c] = if ((row ushr c) and 1 != 0) 1f else 0f
            }
            lightsTouch = true
        }
        val s = ledPitch * 0.66f
        for (i in 0 until 64) {
            val g = i / 32
            val c = i % 32
            val x = ledX[g] + c * ledPitch + (ledPitch - s) / 2f
            val y = strip.centerY() - s / 2f
            val v = lv[i]
            if (v > 0.6f) {
                fill.color = (((0x55 * v).toInt()) shl 24) or 0xFF2A2A
                canvas.drawRect(x - 1.5f * d, y - 1.5f * d, x + s + 1.5f * d, y + s + 1.5f * d, fill)
            }
            fill.color = lerp(0xFF3A0C10.toInt(), 0xFFFF3B30.toInt(), v)
            canvas.drawRect(x, y, x + s, y + s, fill)
        }
    }

    private val NAV_DIR = intArrayOf(2, 0, 1, 3) // the plates (left, up, down, right) to Native.nav's (up, down, left, right)

    private fun arrow(c: Canvas, b: Btn) {      // the arrow on its plate (30 left, 31 up, 32 down, 33 right)
        val cx = b.r.centerX(); val cy = b.r.centerY() - 1.5f * d; val r = minOf(b.r.width(), b.r.height()) * 0.2f
        tri.reset()
        when (b.act) {
            30 -> { tri.moveTo(cx - r, cy); tri.lineTo(cx + r * 0.8f, cy - r); tri.lineTo(cx + r * 0.8f, cy + r) }
            33 -> { tri.moveTo(cx + r, cy); tri.lineTo(cx - r * 0.8f, cy - r); tri.lineTo(cx - r * 0.8f, cy + r) }
            31 -> { tri.moveTo(cx, cy - r); tri.lineTo(cx - r, cy + r * 0.8f); tri.lineTo(cx + r, cy + r * 0.8f) }
            else -> { tri.moveTo(cx, cy + r); tri.lineTo(cx - r, cy - r * 0.8f); tri.lineTo(cx + r, cy - r * 0.8f) }
        }
        tri.close()
        fill.color = navyDeep
        c.drawPath(tri, fill)
    }

    private fun drawSlider(c: Canvas) {         // the open value: a groove, a ribbed plate on it, a red line at its middle
        val v = Native.barGet()
        if (v < 0) { sliding.clear(); return }
        val tr = slider.height() * 0.32f
        fill.color = navyDeep
        c.drawRect(slider.left - 1.5f * d, slider.centerY() - tr - 1.5f * d, slider.right + 1.5f * d, slider.centerY() + tr + 1.5f * d, fill)
        fill.color = navy
        c.drawRect(slider.left, slider.centerY() - tr, slider.right, slider.centerY() + tr, fill)
        val kw = 26f * d
        val x = slider.left + (slider.width() - kw) * v / 10000f
        val k = RectF(x, slider.top, x + kw, slider.bottom)
        fill.color = navyDeep
        c.drawRect(k.left - 1.5f * d, k.top - 1.5f * d, k.right + 1.5f * d, k.bottom + 1.5f * d, fill)
        ribbed(c, k, if (sliding.isEmpty()) plate else platePress)
        fill.color = red
        c.drawRect(k.centerX() - 1.5f * d, k.top + 3f * d, k.centerX() + 1.5f * d, k.bottom - 3f * d, fill)
    }

    private fun slideTo(x: Float) {
        val kw = 26f * d
        Native.barSet((((x - slider.left - kw / 2f) / (slider.width() - kw)).coerceIn(0f, 1f) * 10000f).toInt())
    }

    private fun keyAt(x: Float, y: Float): Key? =
        keys.firstOrNull { it.black && it.r.contains(x, y) } ?: keys.firstOrNull { !it.black && it.r.contains(x, y) }

    private fun btnAt(x: Float, y: Float): Btn? = btns.firstOrNull {
        x >= it.r.left - 4f * d && x <= it.r.right + 4f * d && y >= it.r.top - 4f * d && y <= it.r.bottom + 4f * d
    }

    private fun ledAt(x: Float, y: Float): Int? {   // g * 32 + cell, or null
        if (y < strip.top - 6f * d || y > strip.bottom) return null
        for (g in 0..1) {
            val c = ((x - ledX[g]) / ledPitch).toInt()
            if (x >= ledX[g] && c in 0 until 32) return g * 32 + c
        }
        return null
    }

    private fun sweep(id: Int, x: Float, y: Float) {
        if (!lightsTouch) return
        val l = ledAt(x, y) ?: return
        if (swept[id] == l) return
        swept[id] = l
        Native.cell(l / 32, l % 32)
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
            in 30..33 -> { Native.nav(NAV_DIR[b.act - 30]); navHeld = b; navNext = android.os.SystemClock.uptimeMillis() + 380L }
            34 -> Native.navSelect()
            20 -> Native.vintage(!Native.vintageGet())
            10, 11 -> Native.strike(b.act - 10, true)
        }
    }

    private fun release(b: Btn) {
        if (navHeld === b) navHeld = null
        if (b.act == 10 || b.act == 11) Native.strike(b.act - 10, false)
    }

    override fun onTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = ev.actionIndex
                val id = ev.getPointerId(i)
                val b = btnAt(ev.getX(i), ev.getY(i))
                val onSlider = Native.barGet() >= 0 && ev.getY(i) >= slider.top - 8f * d && ev.getY(i) <= slider.bottom + 4f * d &&
                    ev.getX(i) >= slider.left - 8f * d && ev.getX(i) <= slider.right + 8f * d
                if (onSlider && b == null) { sliding.add(id); slideTo(ev.getX(i)) }
                else if (b != null) { pressed[id] = b; press(b) }
                else if (ev.getY(i) < band.top) { swept[id] = -1; sweep(id, ev.getX(i), ev.getY(i)) }
                else { held[id] = keyAt(ev.getX(i), ev.getY(i)); sendKeys() }
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until ev.pointerCount) {
                    val id = ev.getPointerId(i)
                    if (sliding.contains(id)) { slideTo(ev.getX(i)); continue }
                    if (swept.containsKey(id)) { sweep(id, ev.getX(i), ev.getY(i)); continue }
                    if (!held.containsKey(id)) continue
                    val k = keyAt(ev.getX(i), ev.getY(i))
                    if (k !== held[id]) { held[id] = k; sendKeys() }
                }
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                val id = ev.getPointerId(ev.actionIndex)
                pressed.remove(id)?.let { release(it) }
                sliding.remove(id)
                swept.remove(id)
                held.remove(id)
                sendKeys()
            }
            MotionEvent.ACTION_CANCEL -> {
                pressed.values.forEach { release(it) }
                pressed.clear(); held.clear(); swept.clear(); sliding.clear(); navHeld = null; sendKeys()
            }
        }
        return true
    }
}
