/* SPDX-License-Identifier: GPL-3.0-only */
/* Quorum for Android, the hardware fork: each path's analogue low pass, after its DAC and before its VCA: a model of
 * a filter built on the SSM2040 (Dave Rossum's chip of 1978: the Prophet-5 revs 1 and 2, the E-mu modular, the Octave
 * Cat SRM), wired as the data sheet's four-pole low pass with resonance.
 *
 * The chip is four transconductance cells, each with a capacitor and a buffer: four one-pole low passes in a row, the
 * fourth's output fed back, inverted, to the first's input for the resonance. What makes it sound as it does is WHERE
 * each cell bends. A cell's input pair sees the difference between what comes in and what its own capacitor already
 * holds, and it is that difference the pair rounds off (a tanh):
 *
 *      dy/dt = wc * tanh(a * (x - y)) / a          (a ladder's stage rounds x and y each on its own)
 *
 * So anything well under the cutoff passes clean (x and y are nearly the same there: nothing to round), and only fast
 * edges and the resonance's own swing are slowed: no grit in the pass band, the bass left whole and smooth, and a
 * resonance that swells into a rounded sine as it is driven instead of breaking up. With the resonance up, the level
 * under the peak falls (1 / (1 + k)), as the real ones do: nothing makes it up.
 *
 * The sums: twice the phone's rate (88.2 kHz); each cell a trapezoidal one-pole, the feedback loop solved in the same
 * step (no sample's delay round it: the tuning and the resonance hold right up the range), each cell's bend taken from
 * where it stood a step ago. The cutoff is exponential in its control voltage (an 8-bit DAC behind a capacitor: 256
 * steps over 16 Hz .. 20 kHz, about half a semitone each), the resonance another. The two filters are two parts off
 * the shelf: their tuning, their resonance, each cell's capacitor, their offsets a little apart, for good. */
#include <math.h>
typedef struct { float s[4], d[4], cut, res, xp; uint32_t rng, on; } hwf_t;
static hwf_t hw_flt[2] = {{{0}, {0}, -1.0f, 0.0f, 0.0f, 0x1234567u, 0}, {{0}, {0}, -1.0f, 0.0f, 0.0f, 0x89ABCDEu, 0}};
static const float HWF_TUNE[2] = {1.021f, 0.968f};      /* the cutoff's scale: a third of a semitone sharp, a half flat */
static const float HWF_K[2] = {0.97f, 1.04f};           /* the resonance's */
static const float HWF_C[2][4] = {{1.00f, 1.03f, 0.98f, 1.01f}, {0.99f, 1.02f, 1.04f, 0.97f}};   /* each cell's capacitor */
static const float HWF_DC[2] = {0.0006f, -0.0009f};     /* the output's offset */
#define HWF_A 1.5f                              /* how soon a cell bends: full scale in is 1 */
#define HWF_KMAX 4.4f                           /* the resonance's top: it sings a little past 4 */

static float hwf_hz(float code) { return 16.35f * powf(2.0f, code * (124.0f / 3072.0f)); }   /* the control voltage (0..255) -> Hz */
static float hwf_k(float code) { return HWF_KMAX * code * (1.0f / 255.0f); }

static float hwf_run(hwf_t *f, uint32_t k, float x, float ccode, float rcode)   /* a sample at 44.1 kHz; x: 1 = full scale */
{
    float g, kk, fc, out = 0.0f, G[4];
    uint32_t os, i;
    if (f->cut < 0.0f || !f->on) {              /* (switched in: the voltages there at once, the capacitors empty) */
        f->cut = ccode; f->res = rcode; f->xp = x; f->on = 1;
        for (i = 0; i < 4u; i++) f->s[i] = f->d[i] = 0.0f;
    }
    f->cut += (ccode - f->cut) * (1.0f / 128.0f);   /* (the control DACs' capacitors: about 3 ms) */
    f->res += (rcode - f->res) * (1.0f / 128.0f);
    fc = hwf_hz(f->cut) * HWF_TUNE[k];
    if (fc > 21000.0f) fc = 21000.0f;
    g = tanf(3.14159265f * fc / 88200.0f);
    kk = hwf_k(f->res) * HWF_K[k];
    f->rng = f->rng * 1664525u + 1013904223u;
    for (os = 0; os < 2u; os++) {
        float in = (os ? x : 0.5f * (f->xp + x)) + ((float)(int32_t)(f->rng >> (8u + 8u * os) & 255u) - 127.5f) * 2.0e-7f;   /* (the
                                                   cells' own hiss: -100 dB or so; it is what the resonance starts from) */
        float gam = 1.0f, S = 0.0f, u, y;
        for (i = 0; i < 4u; i++) {              /* each cell's gain now, as bent as it was a step ago */
            float z = HWF_A * f->d[i], t = z > 0.02f || z < -0.02f ? tanhf(z) / z : 1.0f, gi = g * HWF_C[k][i] * t;
            G[i] = gi / (1.0f + gi);
        }
        for (i = 0; i < 4u; i++) { S = S * G[i] + (1.0f - G[i]) * f->s[i]; gam *= G[i]; }
        y = (gam * in + S) / (1.0f + kk * gam); /* the fourth cell's output, the loop closed */
        u = in - kk * y;
        for (i = 0; i < 4u; i++) {
            float v = (u - f->s[i]) * G[i], o = v + f->s[i];
            f->s[i] = o + v;
            f->d[i] = u - o;
            u = o;
        }
        out += u;
    }
    f->xp = x;
    return 0.5f * out + HWF_DC[k];
}

static float hwf_mag(float ccode, float rcode, uint32_t k, float hz)   /* the response at hz, for the page's picture */
{
    float x = hz / (hwf_hz(ccode) * HWF_TUNE[k]), a = 1.0f - x * x, b = 2.0f * x, re = a * a - b * b + hwf_k(rcode) * HWF_K[k], im = 2.0f * a * b;
    return 1.0f / sqrtf(re * re + im * im);
}
