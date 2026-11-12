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
        val s = minOf(MeasureSpec.getSize(widthMeasureSpec), MeasureSpec.getSize(heightMeasureSpec))   // (square: the rack says how big)
        setMeasuredDimension(s, s)
    }

    private inner class Renderer : GLSurfaceView.Renderer {
        private val intensity = ByteBuffer.allocateDirect(240 * 240).order(ByteOrder.nativeOrder())
        private val ramp = ByteBuffer.allocateDirect(256 * 4).order(ByteOrder.nativeOrder())
        private val gain = IntArray(7)
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
        private var uCap = 0
        private var uFly = 0
        private var uT = 0
        private val t0 = android.os.SystemClock.uptimeMillis()
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
            uCap = GLES20.glGetUniformLocation(prog, "uCap")
            uFly = GLES20.glGetUniformLocation(prog, "uFly")
            uT = GLES20.glGetUniformLocation(prog, "uT")
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
            GLES20.glUniform1f(uCap, gain[5].coerceIn(0, 10) / 10f)   // (VINTAGE) the capacitors, the flyback: 0..1
            GLES20.glUniform1f(uFly, gain[6].coerceIn(0, 10) / 10f)
            GLES20.glUniform1f(uT, ((android.os.SystemClock.uptimeMillis() - t0) % 600000L) / 1000f)
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
        // (Global: VINTAGE) the tube's faults, each 0..1:
        //   capacitor  dried-out filter caps: mains ripple in the deflection, the sides of the picture wavering in a
        //              slow roll up the screen, the top corners pulled in and stained purple, a faint hum bar
        //   flyback    a failing flyback: the top of the scan folding back over itself in a bright band, the picture
        //              squashed toward the top and sat low and drifting, lines pairing into a fine hatch, bright
        //              edges smearing to the right
        private const val FS = """
precision highp float;
uniform sampler2D uInt;
uniform sampler2D uRamp;
uniform float uGc;
uniform float uGe;
uniform float uLinePx;
uniform float uLineDim;
uniform float uDg;
uniform float uCap;
uniform float uFly;
uniform float uT;
varying vec2 vEng;
float tube(vec2 q, vec2 g) {                // the picture at q (where the beam is told to be), dimmed as the glass at g
    vec2 d = q - 120.0;
    float k2 = 30.0 * dot(d, d);
    vec2 s = q + d * k2 / 16777216.0;
    if (s.x < 0.0 || s.y < 0.0 || s.x >= 239.0 || s.y >= 239.0) return 0.0;
    float v = texture2D(uInt, (s + 0.5) / 240.0).r * 255.0;
    vec2 e = 2.0 * g + 1.0 - 240.0;
    float d2 = (e.x * e.x + e.y * e.y) * 128.0 / 57121.0;
    float tf = uGe + (uGc - uGe) * clamp(256.0 - d2, 0.0, 256.0) / 256.0;
    return v * tf / 256.0;
}
void main() {
    vec2 q = vEng - 0.5;
    vec2 g = q;                             // (the glass: where this fragment is)
    if (uDg >= 0.0) {                       // the degauss: the picture wobbles, the wobble dying away
        float a = 7.0 * exp(-2.5 * uDg);
        q += vec2(sin(q.y * 0.09 + uDg * 38.0), cos(q.x * 0.07 + uDg * 29.0)) * a;
    }
    float hum = 1.0;
    if (uCap > 0.0) {                       // the capacitors: the width breathing with the ripple, line by line
        float a = pow(uCap, 1.5);
        float ph = q.y / 240.0 * 6.2831853 * 1.3 + uT * 1.7;   // (a cycle and a bit down the screen, rolling)
        float edge = (q.x - 120.0) / 120.0;
        float wob = sin(ph) + 0.35 * sin(ph * 2.7 + 1.3) + 0.15 * sin(q.y * 0.21 - uT * 9.0);
        q.x -= edge * abs(edge) * wob * a * 16.0;
        q.x += edge * a * 22.0 * exp(-q.y / 22.0);             // (the top corners drawn in)
        q.x += sin(q.y * 0.045 + uT * 1.1) * a * 2.5;           // (the whole picture swaying a little)
        hum = 1.0 - a * 0.16 * (0.5 + 0.5 * sin(ph - 0.8));
    }
    float v;
    float fold = 0.0;
    if (uFly > 0.0) {                       // the flyback: the scan squashed, folded at the top, sat low
        float a = uFly;
        float drop = a * 10.0 + sin(uT * 0.6) * a * 1.5 + sin(uT * 7.3) * a * a * 0.8;
        float y0 = a * 34.0;                // (the picture's top rows, folded back)
        float h = 3.0 + a * 13.0;           // (into this band)
        float py = q.y - drop;
        q.x += a * 3.0 + sin(py * 0.9 + uT * 31.0) * a * a * a * 0.9;   // (a little off to the side, the lines unsteady)
        if (py < 0.0) v = 0.0;
        else {
            float u = py / (240.0 - drop);
            float sy = y0 + (240.0 - y0) * pow(u, 1.0 - 0.3 * a);
            v = tube(vec2(q.x, sy), g);
            v += 0.55 * a * tube(vec2(q.x - 1.6 - 2.5 * a, sy), g);   // (bright edges smearing right)
            if (py < h) {                   // the fold: the top rows again, reversed, piled into the band
                float fy = y0 * (1.0 - py / h);
                v += (1.0 + a) * tube(vec2(q.x + 1.0, fy), g);
                fold = a * (1.6 * exp(-py / 2.0) + 0.45 * (1.0 - py / h));   // (the beam piled up: a bright line)
            }
            v *= 1.0 - a * 0.32 * (0.5 + 0.5 * sin(q.x * 1.25 - q.y * 3.3));   // (lines pairing: a fine hatch)
        }
    } else v = tube(q, g);
    v = min(255.0, v * hum + fold * 60.0);
    vec3 c = texture2D(uRamp, vec2((floor(v) + 0.5) / 256.0, 0.5)).rgb;
    if (uCap > 0.0) {                       // ... and the purity gone at the top corners
        float side = min(g.x, 240.0 - g.x);
        float w = pow(uCap, 1.3) * exp(-side / 26.0) * exp(-g.y / 34.0);
        float l = max(c.r, max(c.g, c.b));
        c = mix(c, l * vec3(0.95, 0.25, 1.0), clamp(w * 1.4, 0.0, 0.9)) + vec3(0.30, 0.02, 0.36) * w * 0.6;
    }
    if (uDg >= 0.0) {                       // ... and its colours smear across the glass, strongest at the edges
        float m = exp(-2.0 * uDg);
        vec2 dd = (q - 120.0) / 120.0;
        vec3 tint = 0.5 + 0.5 * cos(6.2831853 * (dd.x * 0.7 + dd.y * 0.5 + uDg * 1.5) + vec3(0.0, 2.0, 4.0));
        c = mix(c, max(c.r, max(c.g, c.b)) * tint * 1.4, m * 0.7 * clamp(length(dd) * 1.2, 0.2, 1.0));
        c += vec3(0.20, 0.08, 0.25) * exp(-uDg * 14.0);
    }
    if (mod(floor(gl_FragCoord.y), uLinePx) >= uLinePx - 1.0) c *= uLineDim;
    {                                       // the glass sits back in its bezel: darker to its edges, darker still in
                                            // its corners, and the bezel's own shadow along the top and the left
                                            // (the room's light comes from there)
        vec2 hp = (g - 120.0) / 120.0;
        vec2 ae = abs(hp);
        float sh = 1.0 - 0.62 * smoothstep(0.84, 1.0, max(ae.x, ae.y));
        sh *= 1.0 - 0.50 * smoothstep(0.92, 1.38, length(ae));
        sh *= 1.0 - 0.38 * max(1.0 - smoothstep(-1.0, -0.87, hp.y), 1.0 - smoothstep(-1.0, -0.89, hp.x));
        c *= sh;
    }
    {                                       // the room's light on the glass: a soft highlight, top left
        vec2 hp = (g - 120.0) / 120.0;
        float hx = (hp.x + 0.55) * (hp.x + 0.55), hy = (hp.y + 0.62) * (hp.y + 0.62);
        c += vec3(0.90, 0.95, 1.0) * (exp(-(hx / 0.10 + hy / 0.03)) * 0.058 + exp(-(hx / 0.022 + hy / 0.007)) * 0.058);   // (a wide soft
                                            // glow and a brighter heart: plain to see on a dark page, never over the text)
    }
    gl_FragColor = vec4(c, 1.0);
}
"""
    }
}
