// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.BitmapShader
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Matrix
import android.graphics.Paint
import android.graphics.RadialGradient
import android.graphics.Rect
import android.graphics.RectF
import android.graphics.Shader
import android.graphics.Typeface
import android.view.MotionEvent
import android.view.ViewGroup
import kotlin.math.cos
import kotlin.math.min
import kotlin.math.sin

/**
 * The rack face, the same as Arcturus S6's (after the PPG Waveterm's) but in brushed aluminium
 * (res/drawable-nodpi/rack_bg.jpg): a handle near either end, and from the left the tube, as tall as the screen in a
 * thin black plastic bezel; two 5.25" drives standing on end; and two knobs, BRIGHTNESS (the tube's own, the same
 * control as Global: VDU's) and MASTER LEVEL (the output's VCA). Both drives' lights glow green; the first one's
 * flickers while the disk works.
 *
 * Quorum's own: from the tube's right side to the handle, a row of 32 green lights along the top, grid A's row now,
 * and another along the bottom, grid B's. They are early-'80s gallium phosphide lamps: 3 mm, green diffused epoxy,
 * not very bright, more yellow than a modern green. Touched (or swept along) their cells turn over, as on the Grids
 * page. At boot, while the petri dish grows, two quick sweeps, A and B going opposite ways.
 *
 * Everything else is done on the tube, with a finger as the lightpen: there are no keys or plates on the rack.
 */
@SuppressLint("ViewConstructor")
class RackView(context: Context, private val crt: CrtView) : ViewGroup(context) {
    private val d = resources.displayMetrics.density
    private val bg: Bitmap = BitmapFactory.decodeResource(resources, R.drawable.rack_bg)
    private val drive: Bitmap = BitmapFactory.decodeResource(resources, R.drawable.drive)
    private val cap: Bitmap = BitmapFactory.decodeResource(resources, R.drawable.knob_cap)
    private val grip: Bitmap = BitmapFactory.decodeResource(resources, R.drawable.handle_tex)   // (the handles' covering)
    private val gripShader = BitmapShader(grip, Shader.TileMode.CLAMP, Shader.TileMode.CLAMP)
    private val gripM = Matrix()

    private val ink = 0xFF17181B.toInt()        // the plate's legends: black, screened onto the aluminium
    private val p = Paint(Paint.ANTI_ALIAS_FLAG)
    private val bmp = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val lamp = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
    private val label = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        textAlign = Paint.Align.CENTER
        color = ink
    }
    private val legend = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        typeface = Typeface.create(Typeface.SANS_SERIF, Typeface.BOLD)
        textAlign = Paint.Align.LEFT
        color = ink
    }
    private val src = Rect()
    private val r = RectF()

    // where things are (pixels), worked out in onMeasure
    private var u = 1f                          // the unit: the rack's height
    private val bezel = RectF()
    private val tube = Rect()
    private val drives = arrayOf(RectF(), RectF())
    private val handles = arrayOf(RectF(), RectF())
    private val earX = FloatArray(2)
    private val knobX = FloatArray(2)
    private val knobY = FloatArray(2)
    private var knobR = 1f
    private var top = 0f
    private var bottom = 1f

    // the lights: two rows of 32 (A along the top, B along the bottom), from the tube to the handle
    private var ledX0 = 0f                      // the first light's left edge
    private var ledPitch = 1f
    private val ledY = FloatArray(2)            // each row's middle
    private var ledR = 1f                       // a lens's radius
    private var lampOff: Bitmap? = null         // one lamp dark, one lit (with its glow): drawn once, stamped 64 times
    private var lampOn: Bitmap? = null
    private var lampHalf = 1f                   // half the stamps' side
    private val lv = FloatArray(64)             // each light, 0 dark .. 1 lit (A's 32, then B's)
    private var sweepT0 = -1L                   // the boot sweep's start (-1: not yet; -2: done or missed)
    private var lightsTouch = true              // the cells can be turned over (not during the sweep)
    private val swept = HashMap<Int, Int>()     // finger -> the light it last turned (g * 32 + cell; -1 none yet)

    private var flick = true                    // the first drive's light, this moment
    private val held = IntArray(2) { -1 }       // the finger on each knob
    private val lastX = FloatArray(2)
    private val lastY = FloatArray(2)
    private val turn = FloatArray(2)            // each knob's turn while held, 0..1
    private val names = arrayOf("BRIGHTNESS", "MASTER LEVEL")

    companion object {
        const val DRIVE_ASPECT = 1408f / 404f   // drive.png, long side / short side
        const val LED_X = 0.182f                // its light (the picture is mirrored: top left): the middle of its lens,
                                                // as a fraction of its width and height
        const val LED_Y = 0.0822f
        const val LED_R = 0.05f                 // ... and the lens's size, of its width
        const val ROW = 0.085f                  // the rows of lights: this far in from the rack's top and bottom
    }

    init {
        setWillNotDraw(false)
        addView(crt)
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        val h = MeasureSpec.getSize(heightMeasureSpec)
        setMeasuredDimension(w, h)
        val left = paddingLeft.toFloat()
        val availW = (w - paddingLeft - paddingRight).toFloat()
        val availH = (h - paddingTop - paddingBottom).toFloat()
        // in units of the rack's height: the ears, the handles, the gaps, the tube's bezel (1), the drives (shorter than
        // Arcturus's: the lights' rows run over and under them), the knobs
        val ear = 0.035f; val hw = 0.075f; val g = 0.03f; val dh = 0.64f; val dw = dh / DRIVE_ASPECT; val dg = 0.022f
        val strip = 0.24f
        val need = 2 * (ear + hw) + 4 * g + 1f + 2 * dw + dg + strip
        u = min(availH, availW / need)          // (a squarer screen: the whole rack a little smaller, still whole)
        top = paddingTop + (availH - u) / 2f
        bottom = top + u
        var x = left                            // (a wider screen: what is over goes to the knobs' strip)
        earX[0] = x + ear * u / 2f
        x += ear * u
        handles[0].set(x, top + 0.07f * u, x + hw * u, bottom - 0.07f * u)
        x += (hw + g) * u
        bezel.set(x, top, x + u, bottom)
        val inset = 0.03f * u
        tube.set((bezel.left + inset).toInt(), (bezel.top + inset).toInt(), (bezel.right - inset).toInt(), (bezel.bottom - inset).toInt())
        tube.right = tube.left + min(tube.width(), tube.height())
        tube.bottom = tube.top + tube.width()
        x += (1f + g) * u
        val panelL = x                          // what is right of the tube: from here ...
        val dy = top + (u - dh * u) / 2f
        for (i in 0..1) { drives[i].set(x, dy, x + dw * u, dy + dh * u); x += (dw + if (i == 0) dg else g) * u }
        val right = left + availW
        earX[1] = right - ear * u / 2f
        handles[1].set(right - (ear + hw) * u, top + 0.07f * u, right - ear * u, bottom - 0.07f * u)
        val panelR = handles[1].left - g * u    // ... to here
        val sx0 = x; val sx1 = panelR           // the knobs' strip: what is left
        knobR = min((sx1 - sx0) * 0.30f, 0.078f * u)
        for (i in 0..1) { knobX[i] = (sx0 + sx1) / 2f; knobY[i] = top + u * (if (i == 0) 0.375f else 0.685f) }
        label.textSize = min(0.034f * u, (sx1 - sx0) / 7.6f)
        legend.textSize = 0.026f * u
        // the lights: 32 across all of it, a row at the top and a row at the bottom
        ledPitch = (panelR - panelL) / 32f
        ledX0 = panelL
        ledY[0] = top + ROW * u
        ledY[1] = bottom - ROW * u
        val nr = min(ledPitch * 0.37f, 0.021f * u)
        if (nr != ledR || lampOff == null) { ledR = nr; makeLamps() }
        crt.measure(MeasureSpec.makeMeasureSpec(tube.width(), MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(tube.height(), MeasureSpec.EXACTLY))
    }

    override fun onLayout(changed: Boolean, l: Int, t: Int, rr: Int, b: Int) {
        crt.layout(tube.left, tube.top, tube.right, tube.bottom)
    }

    /**
     * One lamp, dark and lit. A 3 mm round LED of about 1980 seen end on, through its hole in the plate: a dome of
     * green-tinted, milky (diffused) epoxy. Dark, it is a deep bottle green with the room's light on its shoulder.
     * Lit, gallium phosphide gives a yellowish green (about 565 nm) and not much of it: the diffuser spreads it over
     * the whole dome, hotter over the chip in the middle, falling off to the rim, with only a small soft glow on the
     * plate around it: nothing like a modern high-brightness green.
     */
    private fun makeLamps() {
        val rr = ledR
        val half = rr * 3.0f                     // (room for the glow)
        lampHalf = half
        val side = maxOf(4, (half * 2f + 0.5f).toInt())
        val c = side / 2f
        val q = Paint(Paint.ANTI_ALIAS_FLAG)
        fun body(cv: Canvas) {                  // the hole, and the dome dark
            q.shader = null
            q.color = 0x50FFFFFF; cv.drawCircle(c + rr * 0.07f, c + rr * 0.10f, rr * 1.20f, q)    // (the hole's lower lip, lit)
            q.color = 0x90000000.toInt(); cv.drawCircle(c - rr * 0.05f, c - rr * 0.07f, rr * 1.20f, q)   // (its upper lip, in shadow)
            q.color = 0xFF050806.toInt(); cv.drawCircle(c, c, rr * 1.13f, q)                      // (the gap round the lens)
            q.shader = RadialGradient(c - rr * 0.22f, c - rr * 0.28f, rr * 1.25f,
                intArrayOf(0xFF2F8A5C.toInt(), 0xFF1E6442.toInt(), 0xFF113B27.toInt(), 0xFF091F15.toInt()),
                floatArrayOf(0f, 0.38f, 0.78f, 1f), Shader.TileMode.CLAMP)
            cv.drawCircle(c, c, rr, q)
            q.shader = null
        }
        fun glint(cv: Canvas, a: Int) {         // the room's light on the dome: a soft patch and a small bright point
            q.shader = RadialGradient(c - rr * 0.36f, c - rr * 0.42f, rr * 0.50f,
                intArrayOf((a shl 24) or 0xF2FFF6, 0x00F2FFF6), null, Shader.TileMode.CLAMP)
            cv.drawCircle(c - rr * 0.36f, c - rr * 0.42f, rr * 0.50f, q)
            q.shader = null
            q.color = ((a * 3 / 2).coerceAtMost(255) shl 24) or 0xFFFFFF
            cv.drawCircle(c - rr * 0.40f, c - rr * 0.46f, rr * 0.11f, q)
        }
        val off = Bitmap.createBitmap(side, side, Bitmap.Config.ARGB_8888)
        Canvas(off).let { cv -> body(cv); glint(cv, 0x58) }
        val on = Bitmap.createBitmap(side, side, Bitmap.Config.ARGB_8888)
        Canvas(on).let { cv ->
            q.shader = RadialGradient(c, c, half,   // the glow on the plate: small, soft
                intArrayOf(0x70A6F03C, 0x309CE838, 0x0E8FE030, 0x008FE030), floatArrayOf(0f, 0.46f, 0.74f, 1f), Shader.TileMode.CLAMP)
            cv.drawCircle(c, c, half, q)
            q.shader = null
            q.color = 0xFF050806.toInt(); cv.drawCircle(c, c, rr * 1.13f, q)
            q.shader = RadialGradient(c, c, rr,     // the dome lit: the chip's hot spot, the diffused body, the rim
                intArrayOf(0xFFEFFFB0.toInt(), 0xFFC4FA62.toInt(), 0xFF96EC38.toInt(), 0xFF62C420.toInt(), 0xFF2F8418.toInt()),
                floatArrayOf(0f, 0.20f, 0.48f, 0.80f, 1f), Shader.TileMode.CLAMP)
            cv.drawCircle(c, c, rr, q)
            q.shader = null
            glint(cv, 0x30)
        }
        lampOff?.recycle(); lampOn?.recycle()
        lampOff = off; lampOn = on
    }

    private fun knobNorm(i: Int): Float =
        if (held[i] >= 0) turn[i] else if (i == 0) Native.bright() / 10f else Native.master() / 255f

    override fun onDraw(canvas: Canvas) {
        val w = width.toFloat(); val h = height.toFloat()
        // the plate: the picture, covering the screen (cut to its shape, never stretched)
        val sa = bg.width.toFloat() / bg.height; val va = w / h
        if (sa > va) { val sw = (bg.height * va).toInt(); src.set((bg.width - sw) / 2, 0, (bg.width + sw) / 2, bg.height) }
        else { val sh = (bg.width / va).toInt(); src.set(0, (bg.height - sh) / 2, bg.width, (bg.height + sh) / 2) }
        r.set(0f, 0f, w, h)
        canvas.drawBitmap(bg, src, r, bmp)

        // the rack ears: four slots each
        p.shader = null; p.style = Paint.Style.FILL
        for (e in 0..1) for (k in 0..3) {
            val cy = top + u * (0.12f + 0.2533f * k); val sw = 0.011f * u; val sh = 0.024f * u
            p.color = 0x60FFFFFF
            r.set(earX[e] - sw, cy - sh + 1.2f * d, earX[e] + sw, cy + sh + 1.2f * d); canvas.drawRoundRect(r, sw, sw, p)
            p.color = 0xFF07080B.toInt()
            r.set(earX[e] - sw, cy - sh, earX[e] + sw, cy + sh); canvas.drawRoundRect(r, sw, sw, p)
        }

        // the handles: a long black bar standing off the plate on two feet
        for (hd in handles) {
            val rad = hd.width() / 2f
            p.shader = null; p.color = 0x66000000
            r.set(hd.left + 3f * d, hd.top + 4f * d, hd.right + 5f * d, hd.bottom + 5f * d); canvas.drawRoundRect(r, rad, rad, p)
            p.color = 0xFF121315.toInt()
            for (f in 0..1) {
                val fy = if (f == 0) hd.top + 0.02f * u else hd.bottom - 0.02f * u - 0.07f * u
                r.set(hd.left - 0.008f * u, fy, hd.right + 0.008f * u, fy + 0.07f * u); canvas.drawRoundRect(r, 3f * d, 3f * d, p)
            }
            // its covering: the picture (handle_tex.jpg), its long side along the bar, cut to cover it; then the bar's
            // roundness over that, as shading only
            val gs = maxOf(hd.width() / grip.width, hd.height() / grip.height)
            gripM.setScale(gs, gs)
            gripM.postTranslate(hd.centerX() - grip.width * gs / 2f, hd.centerY() - grip.height * gs / 2f)
            gripShader.setLocalMatrix(gripM)
            p.color = 0xFF000000.toInt()
            p.shader = gripShader
            canvas.drawRoundRect(hd, rad, rad, p)
            p.shader = LinearGradient(hd.left, 0f, hd.right, 0f,
                intArrayOf(0xC0000000.toInt(), 0x38000000, 0x1EFFFFFF, 0x48000000, 0xC8000000.toInt()),
                floatArrayOf(0f, 0.22f, 0.42f, 0.74f, 1f), Shader.TileMode.CLAMP)
            canvas.drawRoundRect(hd, rad, rad, p)
            p.shader = null
        }

        // the tube's bezel: black plastic, a lip catching the light, a step down to the glass
        val br = 0.035f * u
        p.color = 0x70000000
        r.set(bezel.left + 2f * d, bezel.top + 3f * d, bezel.right + 4f * d, bezel.bottom + 2f * d); canvas.drawRoundRect(r, br, br, p)
        p.shader = LinearGradient(0f, bezel.top, 0f, bezel.bottom, 0xFF232326.toInt(), 0xFF0B0B0C.toInt(), Shader.TileMode.CLAMP)
        canvas.drawRoundRect(bezel, br, br, p)
        p.shader = null
        p.style = Paint.Style.STROKE; p.strokeWidth = 1.5f * d; p.color = 0xFF3C3C40.toInt()
        r.set(bezel.left + d, bezel.top + d, bezel.right - d, bezel.bottom - d); canvas.drawRoundRect(r, br, br, p)
        p.style = Paint.Style.FILL; p.color = 0xFF030303.toInt()
        val st = 0.017f * u
        r.set(bezel.left + st, bezel.top + st, bezel.right - st, bezel.bottom - st); canvas.drawRoundRect(r, br * 0.7f, br * 0.7f, p)
        p.style = Paint.Style.STROKE; p.strokeWidth = d; p.color = 0xFF1C1C1E.toInt()
        canvas.drawRoundRect(r, br * 0.7f, br * 0.7f, p)
        p.style = Paint.Style.FILL

        // the drives, and their lights (green, as the rows': the photograph's lens is green too)
        val busy = Native.disk()
        flick = if (busy) Math.random() > 0.35 else true
        for (i in 0..1) {
            val dr = drives[i]
            p.color = 0x70000000
            r.set(dr.left + 2f * d, dr.top + 3f * d, dr.right + 4f * d, dr.bottom + 4f * d); canvas.drawRoundRect(r, 3f * d, 3f * d, p)
            canvas.drawBitmap(drive, null, dr, bmp)
            val lx = dr.left + dr.width() * LED_X; val ly = dr.top + dr.height() * LED_Y; val lr = dr.width() * LED_R
            val work = i == 0 && busy
            if (work && !flick) {                // (between flickers: the lens goes dark; the photograph's is lit)
                p.color = 0xD0021005.toInt(); canvas.drawCircle(lx, ly, lr * 1.12f, p)
                continue
            }
            // the photograph's own lens is the light; over it only the glow: soft, and wider and hotter while it works
            val gr = lr * (if (work) 4.4f else 3.0f)
            p.shader = RadialGradient(lx, ly, gr,
                intArrayOf(if (work) 0xC0B4FF50.toInt() else 0x569CF03C, if (work) 0x4490E830 else 0x2090E830, 0x0090E830),
                floatArrayOf(0f, 0.40f, 1f), Shader.TileMode.CLAMP)
            canvas.drawCircle(lx, ly, gr, p)
            p.shader = null
        }

        drawLights(canvas)
        for (i in 0..1) drawKnob(canvas, i)
        postInvalidateDelayed(if (busy) 30L else 33L)   // (the grids' rows as they step; the drive's flicker; the knobs
                                                        // follow the screen's own settings)
    }

    /** The two rows: grid A's row now along the top, grid B's along the bottom (cell 1 at the left). */
    private fun drawLights(canvas: Canvas) {
        val off = lampOff ?: return
        val on = lampOn ?: return
        val now = android.os.SystemClock.uptimeMillis()
        if (sweepT0 == -1L) sweepT0 = if (Native.booting() != 0) now else -2L
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
        } else {
            for (g in 0..1) {
                val row = Native.row(g)
                for (c in 0 until 32) lv[g * 32 + c] = if ((row ushr c) and 1 != 0) 1f else 0f
            }
            lightsTouch = true
        }
        for (g in 0..1) {
            // the row's name, screened on the plate beside its first lamp's side of the drives
            val ty = if (g == 0) ledY[0] + ledR * 1.2f + legend.textSize * 1.25f else ledY[1] - ledR * 1.2f - legend.textSize * 0.55f
            canvas.drawText(if (g == 0) "GRID A" else "GRID B", ledX0 + ledPitch * 0.5f - ledR, ty, legend)
            for (c in 0 until 32) {
                val cx = ledX0 + (c + 0.5f) * ledPitch
                val cy = ledY[g]
                r.set(cx - lampHalf, cy - lampHalf, cx + lampHalf, cy + lampHalf)
                lamp.alpha = 255
                canvas.drawBitmap(off, null, r, lamp)
                val v = lv[g * 32 + c]
                if (v > 0.01f) { lamp.alpha = (v * 255f).toInt().coerceIn(0, 255); canvas.drawBitmap(on, null, r, lamp) }
            }
        }
    }

    private fun drawKnob(canvas: Canvas, i: Int) {
        val cx = knobX[i]; val cy = knobY[i]; val rr = knobR
        val norm = knobNorm(i).coerceIn(0f, 1f)
        canvas.drawText(names[i], cx, cy - rr * 1.42f, label)
        // its travel: a dot at either end and at the middle
        p.shader = null; p.style = Paint.Style.FILL; p.color = ink
        for (t in 0..2) {
            val a = Math.toRadians(-225.0 + 135.0 * t)
            canvas.drawCircle(cx + rr * 1.2f * cos(a).toFloat(), cy + rr * 1.2f * sin(a).toFloat(), rr * 0.035f, p)
        }
        p.color = 0x70000000; canvas.drawCircle(cx + 2f * d, cy + 3.5f * d, rr * 1.02f, p)
        p.color = Color.rgb(22, 22, 22); canvas.drawCircle(cx, cy, rr, p)     // the knurled skirt
        p.style = Paint.Style.STROKE; p.strokeWidth = 1.4f * d
        for (k in 0 until 60) {
            val a = 2 * Math.PI * k / 60
            p.color = if (k % 2 == 0) Color.rgb(58, 58, 58) else Color.rgb(8, 8, 8)
            canvas.drawLine(cx + rr * 0.83f * cos(a).toFloat(), cy + rr * 0.83f * sin(a).toFloat(),
                cx + rr * 0.98f * cos(a).toFloat(), cy + rr * 0.98f * sin(a).toFloat(), p)
        }
        val cr = rr * 0.80f                     // the brushed cap: it stays (its highlights are the room's), the pointer turns
        r.set(cx - cr, cy - cr, cx + cr, cy + cr)
        canvas.drawBitmap(cap, null, r, bmp)
        val a = Math.toRadians(-225.0 + 270.0 * norm)
        val x1 = cx + cr * 0.30f * cos(a).toFloat(); val y1 = cy + cr * 0.30f * sin(a).toFloat()
        val x2 = cx + cr * 0.92f * cos(a).toFloat(); val y2 = cy + cr * 0.92f * sin(a).toFloat()
        p.strokeCap = Paint.Cap.ROUND
        p.strokeWidth = 3.2f * d; p.color = Color.argb(200, 30, 30, 30); canvas.drawLine(x1, y1, x2, y2, p)
        p.strokeWidth = 1.0f * d; p.color = Color.argb(140, 255, 255, 255)
        canvas.drawLine(x1 + 1.2f * d, y1 + 1.2f * d, x2 + 1.2f * d, y2 + 1.2f * d, p)
        p.strokeCap = Paint.Cap.BUTT
        p.style = Paint.Style.FILL
    }

    private fun knobAt(x: Float, y: Float): Int {
        for (i in 0..1) {
            val dx = x - knobX[i]; val dy = y - knobY[i]
            if (dx * dx + dy * dy <= knobR * knobR * 2.6f) return i
        }
        return -1
    }

    /** The light under a finger (g * 32 + cell), or null: a row's band is taller than its lamps, out to the rack's edge. */
    private fun ledAt(x: Float, y: Float): Int? {
        val c = ((x - ledX0) / ledPitch).toInt()
        if (x < ledX0 || c !in 0 until 32) return null
        val reach = ROW * u * 0.80f
        if (y <= ledY[0] + reach) return c
        if (y >= ledY[1] - reach) return 32 + c
        return null
    }

    private fun sweep(id: Int, x: Float, y: Float) {
        if (!lightsTouch) return
        val l = ledAt(x, y) ?: return
        val last = swept[id] ?: return
        if (last == l) return
        if (last >= 0 && last / 32 != l / 32) return   // (a sweep stays in the row it began in)
        swept[id] = l
        Native.cell(l / 32, l % 32)
        invalidate()
    }

    /** A knob under a finger: drag up or to the right to turn it up. A row of lights: a touch turns a cell over, a
     *  swipe along the row each cell it passes. */
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(ev: MotionEvent): Boolean {
        when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = ev.actionIndex
                val id = ev.getPointerId(i)
                val k = knobAt(ev.getX(i), ev.getY(i))
                if (k >= 0 && held[k] < 0) {
                    turn[k] = knobNorm(k)
                    held[k] = id; lastX[k] = ev.getX(i); lastY[k] = ev.getY(i)
                } else if (k < 0 && ledAt(ev.getX(i), ev.getY(i)) != null) {
                    swept[id] = -1
                    sweep(id, ev.getX(i), ev.getY(i))
                }
            }
            MotionEvent.ACTION_MOVE -> {
                for (k in 0..1) {
                    val i = if (held[k] >= 0) ev.findPointerIndex(held[k]) else -1
                    if (i < 0) continue
                    val x = ev.getX(i); val y = ev.getY(i)
                    turn[k] = (turn[k] + ((x - lastX[k]) - (y - lastY[k])) / (170f * d)).coerceIn(0f, 1f)
                    lastX[k] = x; lastY[k] = y
                    if (k == 0) Native.brightSet((turn[k] * 10f + 0.5f).toInt()) else Native.masterSet((turn[k] * 255f + 0.5f).toInt())
                }
                for (i in 0 until ev.pointerCount) {
                    val id = ev.getPointerId(i)
                    if (swept.containsKey(id)) sweep(id, ev.getX(i), ev.getY(i))
                }
                invalidate()
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_CANCEL -> {
                val id = ev.getPointerId(ev.actionIndex)
                val all = ev.actionMasked != MotionEvent.ACTION_POINTER_UP   // (the last finger up, or the touch taken away)
                for (k in 0..1) if (held[k] == id || all) held[k] = -1
                if (all) swept.clear() else swept.remove(id)
                invalidate()
            }
        }
        return true
    }
}
