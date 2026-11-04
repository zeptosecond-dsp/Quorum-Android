// SPDX-License-Identifier: GPL-3.0-only
package com.zeptosecond.quorum

/** The engine (libquorum): Quorum's firmware on its own thread, Oboe for the sound. */
object Native {
    init {
        System.loadLibrary("quorum")
    }

    /** Starts the engine once; patches and settings live in [dir]. */
    @JvmStatic external fun start(dir: String)
    @JvmStatic external fun audioStart(): Boolean
    @JvmStatic external fun audioStop()
    @JvmStatic external fun setLatency(frames: Int)
    @JvmStatic external fun underruns(): Int

    /** The FM-1's panel: buttons by id (0..13), encoders by id (0..6), the 27 keys as a bit mask (bit 0 = F3). */
    @JvmStatic external fun button(id: Int, down: Boolean)
    @JvmStatic external fun enc(id: Int, detents: Int)
    @JvmStatic external fun keys(mask: Int)
    @JvmStatic external fun midi(status: Int, d1: Int, d2: Int)

    /** LED bits: 0..13 the buttons, 14.. the keys. */
    @JvmStatic external fun leds(): Long
    @JvmStatic external fun glow(): Long

    /** The 240 x 240 screen as ARGB (the firmware's own tube, 16-bit colour). */
    @JvmStatic external fun screen(out: IntArray)

    /** The tube's inputs, for [CrtView]: [intensity] 240 x 240 bytes and [ramp] 256 RGBA (direct buffers); [gain]
     *  4: the tube's gain in the middle and the corners, scanlines a screen height, their darkness in per cent. */
    @JvmStatic external fun crt(intensity: java.nio.ByteBuffer, ramp: java.nio.ByteBuffer, gain: IntArray)

    /** A finger on the screen: [act] 0 down, 1 move, 2 up; [x], [y] on the glass in the firmware's pixels (0..240). */
    @JvmStatic external fun touch(act: Int, x: Float, y: Float)

    /** The Disk wants a patch named: the name as it is now (8 letters), once; null when not. */
    @JvmStatic external fun nameAsk(): String?
    /** The name typed ([ok]), or the dialog closed without one. */
    @JvmStatic external fun nameSet(name: String?, ok: Boolean)

    /** The keyboard's split: [mode] 1 A, 2 A+B, 3 B; [routeGet] as it is now (0: neither path on the keys). */
    @JvmStatic external fun route(mode: Int)
    @JvmStatic external fun routeGet(): Int
    /** Strike [s] (0, 1) by hand: down and up (Bow and Breath sound while it is held). */
    @JvmStatic external fun strike(s: Int, down: Boolean)
    /** The strikes' gates now: bit 0 strike 1, bit 1 strike 2. */
    @JvmStatic external fun strikeLit(): Int

    // the FM-1's ids (firmware/src/platform.c)
    const val BTN_OCT_DOWN = 0
    const val BTN_OCT_UP = 1
    const val BTN_FX = 2
    const val BTN_SEL = 3
    const val BTN_ENV = 4
    const val BTN_LFO = 5
    const val BTN_EDIT = 6
    const val BTN_GLO = 7
    const val BTN_HOME = 8
    const val BTN_SAVE = 9
    const val BTN_ARP = 10
    const val BTN_SEQ = 11
    const val BTN_PLAY = 12
    const val BTN_REC = 13

    const val ENC_SELECT = 0
    const val ENC_ALGORITHM = 1
    const val ENC_KNOB1 = 2
    const val ENC_KNOB2 = 3
    const val ENC_KNOB3 = 4
    const val ENC_KNOB4 = 5
    const val ENC_PRESETS = 6
}
