/* SPDX-License-Identifier: GPL-3.0-only */
/* Quorum for Android, the hardware fork: the sound engine rebuilt as the machine it might have been in 1982, in place
 * of dinghy.c's render(). Read by dinghy.c (QUORUM_ANDROID) just before where render() stood.
 *
 * The machine, as boards in a card cage:
 *
 *   PARTIAL CARD   One oscillator's worth of logic, visited 64 times a sample (time-multiplexed, as Bell Labs' Alles
 *                  machine, the Synergy, the DX7's chips). A crystal / 4 is the slot clock: 6 MHz -> 1.5 MHz -> 64
 *                  slots -> 23437.5 Hz a sample; or 8 MHz -> 2 MHz -> 31250 Hz (Global: ENGINE, CLOCK). In each slot:
 *                  a 20-bit phase accumulator (frequency RAM + phase RAM + adder); the phase's top bits address a
 *                  quarter-wave LOG-SINE ROM (the two bits above them fold and sign it).
 *   NO MULTIPLIERS A partial's level is ADDED to the log-sine (both are attenuations, in fractions of 6 dB) and an
 *                  EXPONENT ROM with a shifter behind it brings the sum back to a linear word. Levels are 8-bit codes,
 *                  0.75 dB a step (eight to the 6 dB), 255 = off.
 *   OUTPUT BOARDS  Two, one a path: each its own level RAM, exponent ROM, accumulator (cleared every sample, it
 *                  saturates) and DAC. The paths never meet as numbers. After each DAC: the path's analogue low
 *                  pass (an SSM2040 four-pole, android_ssm.h; it can be switched out), its VCA and pan, the mix.
 *                  (The vowel is the level computer's: weights on the partials, before any of this.)
 *   LEVEL COMPUTER The envelope board, every 16 samples (24 at the faster clock): about 1.4 kHz. A partial's grid
 *                  level and its two strike levels are attenuations in up / down counters: a decay is a straight line
 *                  in dB (an exponential in level, for nothing), a rise is a straight line too (it starts quietly and
 *                  blooms). Bow's flutter and Breath's air come off a 16-bit shift register. The split, the tilt and
 *                  the vowel are attenuations the CPU writes (the sends), each stepping a code a tick toward what was
 *                  written; a small log / exponent pass adds grid + strike 1 + strike 2 into the one code a partial
 *                  has on each path.
 *   CONTROL DACS   The low passes' cutoff and resonance, each path's level and pan: 8-bit DACs with a small capacitor on
 *                  each output (about 3 ms). The CPU's writes step; the voltages do not, so moving them does not click.
 *   CPU            An 8-bit processor works out every partial's frequency word, its grid level and its sends, and
 *                  writes them to the boards: about 61 times a second (Global: ENGINE, CPU: SLOW; pitch and timbre
 *                  moves step), or every tick (FAST).
 *
 *   V2  log-sine 1024 x 12 bit (1/256 of 6 dB), exponent 256 x 11 bit, a partial +-2047, 12-bit DACs
 *   V1  log-sine  256 x  8 bit (1/32 of 6 dB),  exponent  32 x  8 bit, a partial +-255, 8-bit DACs (the sum's low bit
 *       dropped)
 *
 * The DACs: each bit's weight a little off (fixed, a part's own), a staircase out (held, no oversampling): the phone's
 * 44.1 kHz takes each of its samples as the staircase's mean over that time. The effects are as they were. */
#include <math.h>
#include "android_ssm.h"                        /* (each path's analogue low pass) */

#define HW_OFF (255 << 16)                      /* an attenuation: 8-bit code << 16; 255: silent */
#define HW_FLOOR (128 << 16)                    /* -96 dB: under it, off */
static uint16_t HW_LOGSIN[1024], HW_EXP[256], HW_EXPL[256], HW_L2[256];
static int32_t HW_DAC[2][4096];
static uint32_t hw_ver, hw_clk = 99u, hw_cpu;   /* as built: the revision, the clock, the CPU */
static uint32_t hw_qbits, hw_fbits, hw_lsh, hw_accsh, hw_dhalf;
static uint32_t hw_ratio, hw_tick_n, hw_cpu_n, hw_fmul, hw_rc, hw_ac;
static uint32_t hw_phs[NP], hw_F[NP];           /* the phase RAM, the frequency RAM (20 bit) */
static uint8_t hw_lev[2][NP];                   /* the level RAM, a path */
static int32_t hw_g[NP], hw_gt[NP], hw_gr[NP];  /* the grid's counter, its target and step (as the CPU wrote them) */
static int32_t hw_k[2][NP], hw_kd[2][NP], hw_kpk[2][NP];   /* a strike's counter; its attack's step and peak */
static uint16_t hw_kn[2][NP];
static uint8_t hw_snd[6][NP], hw_sndt[6][NP];   /* the sends, and where the CPU wants them */
static uint16_t hw_lfsr = 0xACE1u;
static uint32_t hw_tick_c, hw_cpu_c, hw_pos;
static int32_t hw_cur[2];
static uint32_t hw_boot = 1;

static int32_t hw_l2c(int32_t x)                /* a level (16384 = full) -> its attenuation (code << 16) */
{
    int32_t n = 13, u;
    if (x <= 0) return HW_OFF;
    if (x >= 16384) return 0;
    while (!(x >> n)) n--;
    u = (14 - n) * 256 - (int32_t)HW_L2[((uint32_t)x << (14 - n) >> 6) & 255u];
    return u << 11;
}
static int32_t hw_c2l(int32_t c)                /* ... and back */
{
    uint32_t u;
    if (c >= HW_FLOOR) return 0;
    if (c < 0) c = 0;
    u = (uint32_t)c >> 11;
    return (int32_t)(HW_EXPL[u & 255u] >> (u >> 8));
}
static uint32_t hw_code(int32_t c) { c = (c + 0x8000) >> 16; return c >= 128 ? 255u : c < 0 ? 0u : (uint32_t)c; }

static void hw_build(uint32_t ver, uint32_t clk, uint32_t cpu)   /* the ROMs, the DACs, the dividers */
{
    uint32_t i, k, b, n, top, smax, nb, seed = 0x51ED270Bu;
    double dscale;
    if (ver == hw_ver && clk == hw_clk && cpu == hw_cpu) return;
    if (ver != hw_ver || hw_clk == 99u) {
        if (ver == 1u) { hw_qbits = 8; hw_fbits = 5; hw_lsh = 2; top = 255; smax = 255; nb = 8; hw_accsh = 1; dscale = 65532.0 / 127.0; }
        else { hw_qbits = 10; hw_fbits = 8; hw_lsh = 5; top = 2047; smax = 4095; nb = 12; hw_accsh = 0; dscale = 32.0; }
        n = 1u << hw_qbits;
        for (i = 0; i < n; i++) {
            double a = -log2(sin(((double)i + 0.5) * 1.5707963267948966 / (double)n)) * (double)(1u << hw_fbits);
            uint32_t v = (uint32_t)(a + 0.5);
            HW_LOGSIN[i] = (uint16_t)(v > smax ? smax : v);
        }
        for (i = 0; i < (1u << hw_fbits); i++) HW_EXP[i] = (uint16_t)((double)top * pow(2.0, -(double)i / (double)(1u << hw_fbits)) + 0.5);
        for (i = 0; i < 256u; i++) {
            HW_EXPL[i] = (uint16_t)(16384.0 * pow(2.0, -(double)i / 256.0) + 0.5);
            HW_L2[i] = (uint16_t)(256.0 * log2(1.0 + (double)i / 256.0) + 0.5);
        }
        hw_dhalf = 1u << (nb - 1u);
        for (k = 0; k < 2u; k++) {              /* each DAC: its bits' weights, up to half a step out at the top bit */
            double w[12], mid;
            for (b = 0; b < nb; b++) {
                double r;
                seed = seed * 1664525u + 1013904223u;
                r = (double)(seed >> 8) / 8388608.0 - 1.0;
                w[b] = (double)(1u << b) + r * 0.5 * pow(2.0, -(double)(nb - 1u - b) / 2.0);
            }
            mid = w[nb - 1u];
            for (i = 0; i < (1u << nb); i++) {
                double v = -mid;
                for (b = 0; b < nb; b++) if (i >> b & 1u) v += w[b];
                HW_DAC[k][i] = (int32_t)lrint(v * dscale);
            }
        }
        for (i = 0; i < NP; i++) hw_lev[0][i] = hw_lev[1][i] = 255;
    }
    if (clk) { hw_ratio = 46440u; hw_tick_n = 24u; hw_fmul = 92484u; hw_rc = 3127u; hw_ac = 242u; }   /* 31250 Hz; 1302 ticks a second */
    else { hw_ratio = 34829u; hw_tick_n = 16u; hw_fmul = 123313u; hw_rc = 2779u; hw_ac = 272u; }       /* 23437.5 Hz; 1465 */
    hw_cpu_n = cpu ? 1u : clk ? 21u : 24u;      /* (SLOW: 61 or 62 writes a second) */
    hw_tick_c = 0; hw_cpu_c = 0;
    if (hw_boot) {
        hw_boot = 0;
        for (i = 0; i < NP; i++) {
            hw_g[i] = hw_gt[i] = HW_OFF; hw_k[0][i] = hw_k[1][i] = HW_OFF;
            for (k = 0; k < 6u; k++) hw_snd[k][i] = hw_sndt[k][i] = 255;
        }
    }
    hw_ver = ver; hw_clk = clk; hw_cpu = cpu;
}

static void hw_strike_hit(uint32_t s, int32_t v)   /* a hit: every member's counter told its peak and how to get there */
{
    uint32_t h;
    for (h = 0; h < NP; h++) {
        int32_t add = (int32_t)(((int64_t)kamp_t[s][h] * v) >> 15), pk, cur, start, n;
        if (add <= 0) continue;
        cur = hw_kn[s][h] ? hw_kpk[s][h] : hw_k[s][h];   /* (one landing on another's attack: from where that was going) */
        pk = hw_l2c(knee(hw_c2l(cur) + add));
        if (pk >= HW_FLOOR) continue;
        start = hw_k[s][h] < pk + (64 << 16) ? hw_k[s][h] : pk + (64 << 16);   /* (from silence: 48 dB under the peak) */
        if (start < pk) start = pk;
        n = (int32_t)(((uint32_t)(katt_v[s][h] ? katt_v[s][h] : 1u) * hw_ac) >> 8);
        if (n < 1) n = 1;
        if (n > 60000) n = 60000;
        hw_k[s][h] = start; hw_kpk[s][h] = pk; hw_kn[s][h] = (uint16_t)n; hw_kd[s][h] = (start - pk) / n;
    }
    kfire_v[s]++;
}

static void hw_cpu_write(void)                  /* the CPU's turn: frequency words, grid levels, sends */
{
    uint32_t h, j;
    for (h = 0; h < NP; h++) {
        int32_t t = hw_l2c(gtgt_v[h]), r = (int32_t)(((uint64_t)kh_v[h] * hw_rc) >> 8);
        hw_F[h] = (uint32_t)(((uint64_t)inc_v[h] * hw_fmul) >> 28) & 0xFFFFFu;
        hw_gt[h] = hw_code(t) == 255u ? HW_OFF : (int32_t)(hw_code(t) << 16);
        hw_gr[h] = r < 1 ? 1 : r;
        for (j = 0; j < 6u; j++) hw_sndt[j][h] = (uint8_t)hw_code(hw_l2c(wt_v[j][h] >> 1));
    }
}

static int32_t hw_ramp(int32_t e, int32_t t, int32_t up, int32_t dn)   /* a counter toward its target (attenuations:
                                                   up is louder, a smaller number) */
{
    if (t >= HW_OFF) {
        if (e < HW_OFF) { e += dn; if (e >= HW_FLOOR) e = HW_OFF; }
        return e;
    }
    if (e >= HW_OFF) e = t + (64 << 16) < HW_FLOOR ? t + (64 << 16) : HW_FLOOR - 1;
    if (e > t) { e -= up; if (e < t) e = t; }
    else if (e < t) { e += dn; if (e > t) e = t; }
    return e;
}

static uint32_t hw_tick(void)                   /* the level computer: every partial's counters, and its two codes */
{
    uint32_t h, s, j, p, live = 0;
    for (h = 0; h < NP; h++) {
        int32_t kc[2];
        hw_g[h] = hw_ramp(hw_g[h], hw_gt[h], 3 * hw_gr[h], hw_gr[h]);
        for (s = 0; s < 2u; s++) {
            int32_t e = hw_k[s][h];
            if (hw_kn[s][h]) { e -= hw_kd[s][h]; if (--hw_kn[s][h] == 0u) e = hw_kpk[s][h]; }
            else if (e < HW_OFF || (!kimp_v[s] && kheld_v[s])) {
                int32_t tt = (kimp_v[s] || !kheld_v[s]) ? HW_OFF : hw_l2c((int32_t)(((int64_t)kamp_t[s][h] * kvel_hold[s]) >> 15));
                int32_t dn = (int32_t)(((uint64_t)kkd_v[s][h] * hw_rc) >> 8), up = (int32_t)(((uint64_t)kku_v[s][h] * hw_rc) >> 8) * 3;
                if (tt >= HW_FLOOR) tt = HW_OFF;
                e = hw_ramp(e, tt, up < 1 ? 1 : up, dn < 1 ? 1 : dn);
            }
            hw_k[s][h] = e;
            if (e < HW_OFF && kfl_v[s][h]) {    /* Bow's flutter, Breath's air: the shift register shakes the level */
                hw_lfsr = (uint16_t)((hw_lfsr >> 1) ^ (uint16_t)(-(int32_t)(hw_lfsr & 1u) & 0xB400));
                e += (int32_t)(((uint32_t)kfl_v[s][h] * 23u * (uint32_t)(hw_lfsr >> 1)) >> 15);
            }
            kc[s] = e;
            kshow_v[s][h] = hw_c2l(e);
        }
        for (j = 0; j < 6u; j++) {
            if (hw_snd[j][h] < hw_sndt[j][h]) hw_snd[j][h]++;
            else if (hw_snd[j][h] > hw_sndt[j][h]) hw_snd[j][h]--;
        }
        for (p = 0; p < 2u; p++) {
            int32_t lin = 0;
            if (hw_g[h] < HW_OFF && hw_snd[p][h] != 255u) lin += hw_c2l(hw_g[h] + ((int32_t)hw_snd[p][h] << 16));
            for (s = 0; s < 2u; s++)
                if (kc[s] < HW_OFF && hw_snd[2u + 2u * s + p][h] != 255u) lin += hw_c2l(kc[s] + ((int32_t)hw_snd[2u + 2u * s + p][h] << 16));
            hw_lev[p][h] = (uint8_t)hw_code(hw_l2c(lin));
        }
        if (hw_lev[0][h] != 255u || hw_lev[1][h] != 255u) live++;
    }
    return live;
}

static void hw_osc(int32_t *aa, int32_t *ab, uint32_t n)   /* the partial card: n samples, both accumulators */
{
    const uint32_t qb = hw_qbits, qm = (1u << qb) - 1u, fb = hw_fbits, fm = (1u << fb) - 1u, ls = hw_lsh, psh = 18u - qb;
    uint32_t h, i;
    for (h = 0; h < NP; h++) {
        const uint32_t ca = hw_lev[0][h], cb = hw_lev[1][h], F = hw_F[h], la = ca << ls, lb = cb << ls;
        uint32_t p = hw_phs[h];
        if (ca == 255u && cb == 255u) { hw_phs[h] = (p + F * n) & 0xFFFFFu; continue; }   /* (silent: the phase runs on) */
        for (i = 0; i < n; i++) {
            uint32_t a, q, x, s, sh;
            int32_t v;
            p = (p + F) & 0xFFFFFu;
            a = p >> psh; q = a >> qb; x = a & qm;
            if (q & 1u) x ^= qm;
            x = HW_LOGSIN[x];
            if (ca != 255u) { s = x + la; sh = s >> fb; v = sh > 15u ? 0 : (int32_t)(HW_EXP[s & fm] >> sh); aa[i] += q & 2u ? -v : v; }
            if (cb != 255u) { s = x + lb; sh = s >> fb; v = sh > 15u ? 0 : (int32_t)(HW_EXP[s & fm] >> sh); ab[i] += q & 2u ? -v : v; }
        }
        hw_phs[h] = p;
    }
}

static void render(int32_t *out)                /* SUB frames at 44.1 kHz */
{
    const int32_t qm = hw_ver == 1u ? ~255 : ~15;   /* (the effects' own word, as it was) */
    int32_t acc[2][SUB], fin[3][SUB], sg[2][3], fs[3] = {0, 0, 0};
    uint32_t i, k, n = 0, pos, idx, got = 0;
    static uint32_t live;
    hw_build(P.engver == 1u ? 1u : 2u, aset.hwclk & 1u, aset.hwcpu & 1u);
    for (i = 0; i < 2u; i++) { sg[i][0] = P.ph.send[i]; sg[i][1] = P.dl.send[i]; sg[i][2] = P.rv.send[i]; }
    logic_block();
    for (pos = hw_pos, i = 0; i < SUB; i++) { pos += hw_ratio; if (pos >= 65536u) { pos -= 65536u; n++; } }   /* the samples
                                                   the converters make in this time */
    for (i = 0; i < n; i++) acc[0][i] = acc[1][i] = 0;
    for (idx = 0; idx < n; ) {
        uint32_t seg = hw_tick_n - hw_tick_c;
        if (hw_tick_c == 0u) {
            if (hw_cpu_c == 0u) hw_cpu_write();
            hw_cpu_c = (hw_cpu_c + 1u) % hw_cpu_n;
            live = hw_tick();
        }
        if (seg > n - idx) seg = n - idx;
        hw_osc(acc[0] + idx, acc[1] + idx, seg);
        hw_tick_c = (hw_tick_c + seg) % hw_tick_n;
        idx += seg;
    }
    live_v = live;
    for (k = 0; k < 2u; k++) for (i = 0; i < n; i++) {   /* the accumulators into their DACs: saturating */
        int32_t v = acc[k][i] >> hw_accsh, lim = (int32_t)hw_dhalf - 1;
        acc[k][i] = HW_DAC[k][(uint32_t)(cl(v, -lim, lim) + (int32_t)hw_dhalf)];
    }
    for (i = 0; i < SUB; i++) {                 /* after the converters: each path's filter, makeup, level and pan */
        int32_t x[2], l = 0, r = 0, f, sn[3] = {0, 0, 0};
        uint32_t np = hw_pos + hw_ratio;
        if (np >= 65536u) {                     /* a new sample in this frame: the staircase's mean across it */
            uint32_t a = 65536u - hw_pos, b = np - 65536u;
            for (k = 0; k < 2u; k++) {
                int32_t nx = acc[k][got];
                x[k] = (int32_t)(((int64_t)hw_cur[k] * a + (int64_t)nx * b) / (int64_t)hw_ratio);
                hw_cur[k] = nx;
            }
            got++; np = b;
        } else { x[0] = hw_cur[0]; x[1] = hw_cur[1]; }
        hw_pos = np;
        for (k = 0; k < 2u; k++) {
            /* the level and the pan are voltages from 8-bit control DACs, each with a small capacitor on its output
             * (about 3 ms): the CPU's writes step, the voltage does not, so one moved quickly does not click. (The low
             * pass's two have theirs in android_ssm.h.) */
            static int32_t cv[2][3];
            static uint32_t cv_init = 1;
            const int32_t tg[3] = {pgl_v[k], pgr_v[k], plv_v[k]};
            uint32_t j;
            int32_t y = x[k];
            if (cv_init) { uint32_t a, b; for (a = 0; a < 2u; a++) for (b = 0; b < 3u; b++) cv[a][b] = -1; cv_init = 0; }
            for (j = 0; j < 3u; j++) cv[k][j] = cv[k][j] < 0 ? tg[j] << 8 : cv[k][j] + (((tg[j] << 8) - cv[k][j]) >> 7);
            if (hw_lon_v[k])                    /* the path's low pass, if it is switched in: after the DAC, before the VCA */
                y = (int32_t)lrintf(hwf_run(&hw_flt[k], k, (float)y * (1.0f / 65536.0f), (float)hw_lcut_v[k], (float)hw_lres_v[k]) * 65536.0f);
            else hw_flt[k].on = 0;
            y = cl(y, -200000, 200000);
            l += (y * (cv[k][0] >> 8)) >> 15;
            r += (y * (cv[k][1] >> 8)) >> 15;
            y = (y * (cv[k][2] >> 8)) >> 15;    /* the sends: after the level, before the pan */
            for (f = 0; f < 3; f++) sn[f] += y * sg[k][f];
        }
        for (f = 0; f < 3; f++) {               /* the effects take theirs at 22.05 kHz, in their own word, as before */
            fs[f] += sn[f] >> 7;
            if (i & 1u) { int32_t v = (fs[f] / 2) & qm; fin[f][i - 1u] = fin[f][i] = v; fs[f] = 0; }
        }
        out[2u * i] += soft_out(l);
        out[2u * i + 1u] += soft_out(r);
    }
    {   /* the effects, in the chosen order: each takes its sends and (the link) the one before's output */
        static const uint8_t ORD[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};   /* P D R */
        int32_t wet[SUB], phm[2] = {0, 0};
        uint8_t php[2] = {0, 0};
        verb_mod_t vm;
        for (k = 0; k < NSRC; k++) {            /* the matrix into the phaser's two sides (it takes over a side) */
            int32_t d0 = P.dep[k][D_PHL], d1 = P.dep[k][D_PHR];
            if (d0) { phm[0] += src_v[k] * d0 >> 6; php[0] = 1; }
            if (d1) { phm[1] += src_v[k] * d1 >> 6; php[1] = 1; }
        }
        ph_pin_v[0] = php[0]; ph_pin_v[1] = php[1];
        vm.size = 0; vm.decay = fxm_v[2]; vm.damp = 0; vm.ret = fxm_v[3]; vm.fb = 0;
        for (pos = 0; pos < 3u; pos++) {
            uint32_t fx = ORD[P.rt.order % 6u][pos];
            int32_t *x = fin[fx];
            if (pos) {
                int32_t lk = (int32_t)(pos == 1u ? P.rt.link1 : P.rt.link2) * 258;
                if (lk) for (i = 0; i < SUB; i++) x[i] += (wet[i] * lk) >> 15;
            }
            if (fx == 0u) ph_run(x, wet, out, SUB, phm, php, &P.ph);
            else if (fx == 1u) qd_run(x, wet, out, SUB, fxm_v[0], fxm_v[1], &P.dl);
            else verb_run(x, out, SUB, &vm, wet);
        }
        for (i = 0; i < 2u * SUB; i++) out[i] = soft_out(out[i]);   /* and out: the mix is analogue's */
    }
}
#ifdef HW_PROBE                                 /* (tests: the strikes' counters, summed as levels) */
int32_t quorum_hw_probe(int32_t s) { int32_t t = 0; uint32_t h; for (h = 0; h < NP; h++) t += hw_c2l(hw_k[s & 1][h]); return t; }
#endif
