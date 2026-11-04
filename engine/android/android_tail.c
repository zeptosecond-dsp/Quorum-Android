/* SPDX-License-Identifier: GPL-3.0-only */
/* Read after the whole firmware (engine/android/quorum_engine.c): what the phone needs from inside it, unchanged.
 *
 * The screen: the firmware draws every page as phosphor light into crt_cmb (the picture's intensity with its halo,
 * before the tube), then puts it through the tube at 240 lines (the bulge, the dimming toward the corners, a scanline
 * every other line, the phosphor's colour ramp, 16-bit colour). The phone takes crt_cmb instead and does the tube
 * itself on the GPU (CrtView.kt), the same maths at the phone's own resolution: finer scanlines and no colour banding.
 * The ramp and the brightness come from the same VDU settings (Global page). */

/* The FM-1 sends a new page to its screen a strip at a time, so a page appears line by line, and the phone does it in
 * an instant. To keep the old machines' way of filling the screen, a new page (or a page's new part) is revealed here:
 * the screen cleared, then a text line's worth of rows at a time, each line left to right a few letters at a time,
 * over DRAW SPEED (Global: VINTAGE; 0: at once). What is already shown carries on live. It follows the page the screen
 * is drawing (shown_page), so a page held up by the disk is drawn in when it comes. */
#define RV_BANDS 15u                            /* 16 rows each: a line of text */
#define RV_CHUNKS 8u                            /* 30 pixels each: a few letters */
static uint8_t rv_shown[240u * 240u];
static uint32_t rv_page = 0xFFFFFFFFu, rv_sub, rv_t0, rv_on;
static void reveal(uint8_t *out)
{
    uint32_t u, band, r, ms = (uint32_t)aset.draw * 80u, pg = shown_page == 0xFFFFFFFFu ? page : shown_page, sb = shown_sub;
    if (pg != rv_page || sb != rv_sub) {        /* a new page: clear the screen, start drawing it */
        rv_page = pg; rv_sub = sb; rv_t0 = fm1_ms; rv_on = ms != 0u;
        memset(rv_shown, 0, sizeof rv_shown);
    }
    if (!rv_on) { memcpy(out, crt_cmb, sizeof crt_cmb); return; }
    u = (fm1_ms - rv_t0) * RV_BANDS * RV_CHUNKS / ms;
    if (u >= RV_BANDS * RV_CHUNKS) { rv_on = 0; memcpy(out, crt_cmb, sizeof crt_cmb); return; }
    band = u / RV_CHUNKS;
    memcpy(rv_shown, crt_cmb, band * 16u * 240u);   /* the lines drawn so far, live */
    for (r = band * 16u; r < band * 16u + 16u && r < 240u; r++)   /* the line being drawn, so far along */
        memcpy(&rv_shown[r * 240u], &crt_cmb[r * 240u], (u % RV_CHUNKS) * 30u);
    memcpy(out, rv_shown, sizeof rv_shown);
}

/* (Global: VINTAGE) the tube's age:
 *   persistence  the phosphor slow to fade: what moves leaves a trail (PERSISTENCE 1..10, longer and longer)
 *   screen burn  the Index burnt into the glass from all the hours it was left on: a pale ghost of it behind every
 *                page (SCREEN BURN 1..10, from barely there to a monitor almost toast). Taken from the Index as it
 *                stands, softened, each time it is on the screen whole.
 *   warm-up      at launch the tube starts dim and comes up over about nine seconds (WARM-UP ON) */
static uint8_t vt_pers[240u * 240u], vt_burn[240u * 240u];
static uint32_t vt_burn_have;
static void vintage(uint8_t *out)
{
    uint32_t i;
    if (rv_page == PG_INDEX && !rv_on && (!vt_burn_have || (fm1_ms & 0x3FFu) < 20u)) {   /* the Index, whole: the burn
                                                   (now, and again every second or so while it is up) */
        uint32_t x, y;
        for (y = 1; y < 239u; y++) for (x = 1; x < 239u; x++) {
            const uint8_t *m = &crt_cmb[y * 240u + x];
            vt_burn[y * 240u + x] = (uint8_t)(((uint32_t)m[-241] + m[-240] + m[-239] + m[-1] + m[0] + m[1] + m[239] + m[240] + m[241]) / 9u);
        }
        vt_burn_have = 1;
    }
    if (aset.persist) {
        uint32_t dk = 140u + (uint32_t)aset.persist * 11u;   /* (each frame keeps dk/256 of the trail) */
        for (i = 0; i < 240u * 240u; i++) {
            uint32_t p = vt_pers[i] * dk >> 8;
            if (out[i] > p) p = out[i];
            vt_pers[i] = (uint8_t)p; out[i] = (uint8_t)p;
        }
    }
    if (aset.burn && vt_burn_have) {
        static const uint8_t BURN_K[11] = {0, 7, 12, 18, 26, 36, 48, 62, 78, 98, 120};   /* (a faint ghost .. toast) */
        uint32_t k = BURN_K[aset.burn > 10u ? 10u : aset.burn];
        for (i = 0; i < 240u * 240u; i++) {
            uint32_t v = out[i] + vt_burn[i] * k / 100u;
            out[i] = (uint8_t)(v > 255u ? 255u : v);
        }
    }
    if (aset.warm && fm1_ms < 9000u) {
        float t = (float)fm1_ms / 9000.0f, g = 0.12f + 0.88f * t * t * (3.0f - 2.0f * t);
        for (i = 0; i < 240u * 240u; i++) out[i] = (uint8_t)((float)out[i] * g);
    }
}

/* intensity: 240 x 240 bytes; rgba: the phosphor ramp, 256 x RGBA (full 8-bit, from the same table crt_build uses);
 * gain: the tube's gain in the middle and in the corners (crt_build's gc, ge), then the scanlines (Global: VDU): lines a
 * screen height, darkness in per cent; then the degauss: ms since it began, or -1 */
void quorum_crt(uint8_t *intensity, uint8_t *rgba, int32_t *gain)
{
    const uint8_t (*K)[4] = PHOS_K[gset.phos < NPHOS ? gset.phos : 0u];
    uint32_t i, k, br = gset.bright > 10u ? 10u : gset.bright;
    uint32_t gc = 116u + br * 20u, ge = gc * br * br * 90u / 10000u;
    if (intensity) { reveal(intensity); vintage(intensity); }
    if (gain) {
        uint32_t dg = fm1_ms - dg_start_ms;
        gain[0] = (int32_t)gc; gain[1] = (int32_t)ge; gain[2] = ASET_LINES[aset.lines % 5u]; gain[3] = (int32_t)aset.dark * 7;
        gain[4] = dg_start_ms != 0xFFFFFFFFu && dg < 2000u ? (int32_t)dg : -1;   /* the degauss: ms since, or -1 */
        gain[5] = aset.cap; gain[6] = aset.fly;   /* (VINTAGE: the tube's faults, 0..10: CrtView draws them) */
    }
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
