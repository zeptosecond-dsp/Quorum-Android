// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

import android.annotation.SuppressLint
import android.content.Context
import android.opengl.GLES20
import android.opengl.GLSurfaceView
import android.view.MotionEvent
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.nio.FloatBuffer
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10
import kotlin.math.max
import kotlin.math.roundToInt

/**
 * The tube, on the GPU. The firmware draws each page as phosphor light (240 x 240 intensities, with its halo);
 * this does what the firmware's tube does to it on the FM-1 (the bulge, the dimming toward the corners, the phosphor's
 * colour ramp) at the phone's own resolution, with fine scanlines instead of one every other engine line. It is also
 * where the finger lands: touches go to the firmware, which knows what is drawn where.
 */
class CrtView(context: Context) : GLSurfaceView(context) {
    private val renderer = Renderer()

    init {
        setEGLContextClientVersion(2)
        setRenderer(renderer)
        renderMode = RENDERMODE_CONTINUOUSLY
    }

    /** The finger as a lightpen: where it lands, in the firmware's pixels; the firmware does the rest. */
    @SuppressLint("ClickableViewAccessibility")
    override fun onTouchEvent(ev: MotionEvent): Boolean {
        val act = when (ev.actionMasked) {
            MotionEvent.ACTION_DOWN -> 0
            MotionEvent.ACTION_MOVE -> 1
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> 2
            else -> return true                 // (a second finger: ignored; the lightpen is one)
        }
        val i = ev.findPointerIndex(ev.getPointerId(0))
        if (i < 0) return true
        Native.touch(act, ev.getX(i) * 240f / width, ev.getY(i) * 240f / height)
        return true
    }

    override fun onMeasure(widthMeasureSpec: Int, heightMeasureSpec: Int) {
        val w = MeasureSpec.getSize(widthMeasureSpec)
        setMeasuredDimension(w, w)
    }

    private inner class Renderer : GLSurfaceView.Renderer {
        private val intensity = ByteBuffer.allocateDirect(240 * 240).order(ByteOrder.nativeOrder())
        private val ramp = ByteBuffer.allocateDirect(256 * 4).order(ByteOrder.nativeOrder())
        private val gain = IntArray(5)
        private val quad: FloatBuffer = ByteBuffer.allocateDirect(4 * 4 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().apply {
            // clip x, y; engine x, y (y down)
            put(floatArrayOf(-1f, -1f, 0f, 240f, 1f, -1f, 240f, 240f, -1f, 1f, 0f, 0f, 1f, 1f, 240f, 0f)); position(0)
        }
        private var prog = 0
        private var texInt = 0
        private var texRamp = 0
        private var uGc = 0
        private var uGe = 0
        private var uLinePx = 0
        private var uLineDim = 0
        private var uDg = 0
        private var viewH = 1

        override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
            prog = program(VS, FS)
            val t = IntArray(2)
            GLES20.glGenTextures(2, t, 0)
            texInt = t[0]; texRamp = t[1]
            GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1)
            setup(texInt, GLES20.GL_LINEAR)
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_LUMINANCE, 240, 240, 0,
                GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, intensity)
            setup(texRamp, GLES20.GL_NEAREST)
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, 256, 1, 0,
                GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, ramp)
            GLES20.glUseProgram(prog)
            GLES20.glUniform1i(GLES20.glGetUniformLocation(prog, "uInt"), 0)
            GLES20.glUniform1i(GLES20.glGetUniformLocation(prog, "uRamp"), 1)
            uGc = GLES20.glGetUniformLocation(prog, "uGc")
            uGe = GLES20.glGetUniformLocation(prog, "uGe")
            uLinePx = GLES20.glGetUniformLocation(prog, "uLinePx")
            uLineDim = GLES20.glGetUniformLocation(prog, "uLineDim")
            uDg = GLES20.glGetUniformLocation(prog, "uDg")
            val aPos = GLES20.glGetAttribLocation(prog, "aPos")
            val aEng = GLES20.glGetAttribLocation(prog, "aEng")
            quad.position(0)
            GLES20.glVertexAttribPointer(aPos, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aPos)
            quad.position(2)
            GLES20.glVertexAttribPointer(aEng, 2, GLES20.GL_FLOAT, false, 16, quad)
            GLES20.glEnableVertexAttribArray(aEng)
            GLES20.glClearColor(0f, 0f, 0f, 1f)
        }

        override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
            GLES20.glViewport(0, 0, width, height)
            viewH = height
        }

        override fun onDrawFrame(gl: GL10?) {
            Native.crt(intensity, ramp, gain)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texInt)
            GLES20.glTexSubImage2D(GLES20.GL_TEXTURE_2D, 0, 0, 0, 240, 240, GLES20.GL_LUMINANCE, GLES20.GL_UNSIGNED_BYTE, intensity)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE1)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texRamp)
            GLES20.glTexSubImage2D(GLES20.GL_TEXTURE_2D, 0, 0, 0, 256, 1, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, ramp)
            GLES20.glUniform1f(uGc, gain[0].toFloat())
            GLES20.glUniform1f(uGe, gain[1].toFloat())
            // the scanlines (Global: VDU): so many a screen height, whole device pixels apart (no moire)
            val lp = max(2, (viewH / maxOf(1, gain[2]).toFloat()).roundToInt())
            GLES20.glUniform1f(uLinePx, lp.toFloat())
            GLES20.glUniform1f(uLineDim, 1f - gain[3].coerceIn(0, 100) / 100f)
            GLES20.glUniform1f(uDg, if (gain[4] >= 0) gain[4] / 1000f else -1f)   // the degauss: seconds since, or -1
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        }

        private fun setup(tex: Int, filter: Int) {
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, filter)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, filter)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
            GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        }

        private fun shader(type: Int, src: String): Int {
            val s = GLES20.glCreateShader(type)
            GLES20.glShaderSource(s, src)
            GLES20.glCompileShader(s)
            val ok = IntArray(1)
            GLES20.glGetShaderiv(s, GLES20.GL_COMPILE_STATUS, ok, 0)
            if (ok[0] == 0) throw RuntimeException("shader: " + GLES20.glGetShaderInfoLog(s))
            return s
        }

        private fun program(vs: String, fs: String): Int {
            val p = GLES20.glCreateProgram()
            GLES20.glAttachShader(p, shader(GLES20.GL_VERTEX_SHADER, vs))
            GLES20.glAttachShader(p, shader(GLES20.GL_FRAGMENT_SHADER, fs))
            GLES20.glLinkProgram(p)
            val ok = IntArray(1)
            GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, ok, 0)
            if (ok[0] == 0) throw RuntimeException("program: " + GLES20.glGetProgramInfoLog(p))
            return p
        }
    }

    companion object {
        private const val VS = """
attribute vec2 aPos;
attribute vec2 aEng;
varying vec2 vEng;
void main() {
    vEng = aEng;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
"""
        // The firmware's tube (dinghy.c, cv_out step 3), in continuous coordinates. q is the engine pixel this
        // fragment falls in (0..240, the firmware's x, y); the bulge reads a little further out the further from
        // the middle; the corners dim by the VDU brightness's gain (crt_build's gc, ge); the ramp gives the colour.
        private const val FS = """
precision highp float;
uniform sampler2D uInt;
uniform sampler2D uRamp;
uniform float uGc;
uniform float uGe;
uniform float uLinePx;
uniform float uLineDim;
uniform float uDg;
varying vec2 vEng;
void main() {
    vec2 q = vEng - 0.5;
    if (uDg >= 0.0) {                       // the degauss: the picture wobbles, the wobble dying away
        float a = 7.0 * exp(-2.5 * uDg);
        q += vec2(sin(q.y * 0.09 + uDg * 38.0), cos(q.x * 0.07 + uDg * 29.0)) * a;
    }
    vec2 d = q - 120.0;
    float k2 = 30.0 * dot(d, d);
    vec2 s = q + d * k2 / 16777216.0;
    float v = 0.0;
    if (s.x >= 0.0 && s.y >= 0.0 && s.x < 239.0 && s.y < 239.0) {
        v = texture2D(uInt, (s + 0.5) / 240.0).r * 255.0;
        vec2 e = 2.0 * q + 1.0 - 240.0;
        float d2 = (e.x * e.x + e.y * e.y) * 128.0 / 57121.0;
        float tf = uGe + (uGc - uGe) * clamp(256.0 - d2, 0.0, 256.0) / 256.0;
        v = min(255.0, v * tf / 256.0);
    }
    vec3 c = texture2D(uRamp, vec2((floor(v) + 0.5) / 256.0, 0.5)).rgb;
    if (uDg >= 0.0) {                       // ... and its colours smear across the glass, strongest at the edges
        float m = exp(-2.0 * uDg);
        vec2 dd = (q - 120.0) / 120.0;
        vec3 tint = 0.5 + 0.5 * cos(6.2831853 * (dd.x * 0.7 + dd.y * 0.5 + uDg * 1.5) + vec3(0.0, 2.0, 4.0));
        c = mix(c, max(c.r, max(c.g, c.b)) * tint * 1.4, m * 0.7 * clamp(length(dd) * 1.2, 0.2, 1.0));
        c += vec3(0.20, 0.08, 0.25) * exp(-uDg * 14.0);
    }
    if (mod(floor(gl_FragCoord.y), uLinePx) >= uLinePx - 1.0) c *= uLineDim;
    gl_FragColor = vec4(c, 1.0);
}
"""
    }
}
