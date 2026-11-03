// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Rect
import android.view.View

/** The engine's 240 x 240 screen, scaled up with hard pixels, redrawn every frame. */
class ScreenView(context: Context) : View(context) {
    private val pixels = IntArray(240 * 240)
    private val bitmap = Bitmap.createBitmap(240, 240, Bitmap.Config.ARGB_8888)
    private val src = Rect(0, 0, 240, 240)
    private val dst = Rect()
    private val paint = Paint().apply { isFilterBitmap = false; isAntiAlias = false }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(w, w)
    }

    override fun onDraw(canvas: Canvas) {
        Native.screen(pixels)
        bitmap.setPixels(pixels, 0, 240, 0, 0, 240, 240)
        val side = minOf(width, height)
        // whole pixels where the screen allows: every engine pixel the same size
        val scale = maxOf(1, side / 240)
        val s = if (side - scale * 240 < side / 12) scale * 240 else side
        val x = (width - s) / 2
        val y = (height - s) / 2
        dst.set(x, y, x + s, y + s)
        canvas.drawBitmap(bitmap, src, dst, paint)
        postInvalidateOnAnimation()
    }
}
