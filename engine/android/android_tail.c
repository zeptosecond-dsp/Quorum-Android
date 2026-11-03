/* SPDX-License-Identifier: GPL-3.0-only */
/* Read after the whole firmware (engine/android/quorum_engine.c): what the phone needs from inside it, unchanged.
 *
 * The screen: the firmware draws every page as phosphor light into crt_cmb (the picture's intensity with its halo,
 * before the tube), then puts it through the tube at 240 lines (the bulge, the dimming toward the corners, a scanline
 * every other line, the phosphor's colour ramp, 16-bit colour). The phone takes crt_cmb instead and does the tube
 * itself on the GPU (CrtView.kt), the same maths at the phone's own resolution: finer scanlines and no colour banding.
 * The ramp and the brightness come from the same VDU settings (Global page). */

/* intensity: 240 x 240 bytes; rgba: the phosphor ramp, 256 x RGBA (full 8-bit, from the same table crt_build uses);
 * gain: the tube's gain in the middle and in the corners (crt_build's gc, ge), then the scanlines (Global: VDU): lines a
 * screen height, darkness in per cent */
void quorum_crt(uint8_t *intensity, uint8_t *rgba, int32_t *gain)
{
    const uint8_t (*K)[4] = PHOS_K[gset.phos < NPHOS ? gset.phos : 0u];
    uint32_t i, k, br = gset.bright > 10u ? 10u : gset.bright;
    uint32_t gc = 116u + br * 20u, ge = gc * br * br * 90u / 10000u;
    if (intensity) memcpy(intensity, crt_cmb, sizeof crt_cmb);
    if (gain) { gain[0] = (int32_t)gc; gain[1] = (int32_t)ge; gain[2] = ASET_LINES[aset.lines % 5u]; gain[3] = (int32_t)aset.dark * 7; }
    if (!rgba) return;
    for (i = 0; i < 256u; i++) {
        uint32_t a, z, f;
        for (k = 1; K[k][0] < i; k++) {}
        a = K[k - 1][0]; z = K[k][0]; f = z > a ? (i - a) * 256u / (z - a) : 0u;
        rgba[4u * i] = (uint8_t)((K[k - 1][1] * (256u - f) + K[k][1] * f) >> 8);
        rgba[4u * i + 1u] = (uint8_t)((K[k - 1][2] * (256u - f) + K[k][2] * f) >> 8);
        rgba[4u * i + 2u] = (uint8_t)((K[k - 1][3] * (256u - f) + K[k][3] * f) >> 8);
        rgba[4u * i + 3u] = 255u;
    }
}
