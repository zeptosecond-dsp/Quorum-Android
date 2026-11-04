/* SPDX-License-Identifier: GPL-3.0-only */
/* Quorum for Android: the FM-1 that dinghy.c sees, on a phone. It stands where tests/host.c stands for the computer
 * build (platform.c includes this file when QUORUM_ANDROID is set; DINGHY_HOST is set too, so dinghy.c leaves out its
 * hardware-only parts), and keeps host.c's shape so the app runs unchanged:
 *
 *   app_main     runs on its own thread (quorum_start) and never returns; platform_poll() is its clock: fm1_ms is
 *                real time, and the audio is rendered here, on the app's own thread, a block at a time into a ring,
 *                just as host.c renders it inside platform_poll. So the engine never runs on two threads at once
 *                (on the FM-1 the ISR and the main loop never overlap either) and needs no locks.
 *   audio        the Oboe callback (quorum_jni.cpp) takes frames out of the ring (quorum_pull); short of frames, it
 *                plays silence rather than wait.
 *   screen       240 x 240 RGB565 in fb[], as host.c; quorum_copy_screen turns it into ARGB for the view.
 *   panel        the 14 buttons, 7 encoders and 27 keys come from the touch panel (quorum_button, quorum_enc,
 *                quorum_keys); the LEDs go back (quorum_leds).
 *   storage      settings, blobs and slots as files in the app's own folder.
 *   MIDI         a queue in (quorum_midi); out is not wired yet. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

#ifndef APP_NAME
#define APP_NAME "QUORUM"
#endif
#ifndef APP_VER
#define APP_VER "0.0"
#endif
#define FS 44100u
#define HALF_FRAMES 256u
#define HOST_NENC 7

static volatile uint32_t fm1_ms;
static volatile struct { uint32_t notes, buttons; } fm1_in;

/* ------------------------------------------------------------------ the panel, from the UI thread */
static _Atomic uint32_t q_edges, q_rel;         /* press and release edges since the app last asked */
static uint32_t q_rel_next;                     /* a tap pressed and let go between two asks: its release the ask after */
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t b = atomic_exchange(&q_edges, 0u), r = atomic_exchange(&q_rel, 0u);
    *released = q_rel_next | (r & ~b);
    q_rel_next = r & b;
    return b;
}
static uint32_t fm1_input_note_edges(void) { return 0; }
static _Atomic int32_t q_enc[HOST_NENC];
static int32_t fm1_enc_take(uint32_t e) { return atomic_exchange(&q_enc[e % HOST_NENC], 0); }
static _Atomic uint64_t q_leds, q_glow;          /* bit id: LED id (0..13 buttons, 14.. keys) */
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_led_key(uint32_t id, int on)
{
    uint64_t m = (uint64_t)1 << (id & 63u);
    if (on) atomic_fetch_or(&q_leds, m); else atomic_fetch_and(&q_leds, ~m);
}
static void fm1_led_key_glow(uint32_t id, int on)
{
    uint64_t m = (uint64_t)1 << (id & 63u);
    if (on) atomic_fetch_or(&q_glow, m); else atomic_fetch_and(&q_glow, ~m);
}

#include "android_ui.h"                        /* touch targets, the phone's settings, the touch queue */

/* ------------------------------------------------------------------ the screen, as host.c */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#ifndef CV_MAX
#define CV_MAX (240u * 64u)
#endif
#include "gfx.c"

/* ------------------------------------------------------------------ storage: a file each, in the app's folder */
#include "store.h"
static char q_dir[512];
static int q_path(char *p, size_t n, const char *what, int i)
{
    if (!q_dir[0]) return -1;
    if (i < 0) snprintf(p, n, "%s/%s.bin", q_dir, what); else snprintf(p, n, "%s/%s%d.bin", q_dir, what, i);
    return 0;
}
static int q_load(const char *what, int i, void *data, uint32_t n)
{
    char p[600];
    FILE *f;
    long len;
    if (q_path(p, sizeof p, what, i) || !(f = fopen(p, "rb"))) return -1;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len != (long)n || fread(data, 1, n, f) != n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
static int q_save(const char *what, int i, const void *data, uint32_t n, uint32_t max)
{
    char p[600], t[610];
    FILE *f;
    if (n > max || q_path(p, sizeof p, what, i)) return -1;
    snprintf(t, sizeof t, "%s.tmp", p);
    if (!(f = fopen(t, "wb"))) return -1;
    if (fwrite(data, 1, n, f) != n) { fclose(f); return -1; }
    fclose(f);
    return rename(t, p) ? -1 : 0;
}
static int settings_load(void *p, uint32_t n) { return q_load("settings", -1, p, n); }
static int settings_save(const void *p, uint32_t n) { return q_save("settings", -1, p, n, SECTOR_MAX); }
/* a blob holds two patches. The phone's records are larger than the FM-1's (the envelopes); a blob of the FM-1's size
 * (two records of 2040 bytes, as the first builds saved them) is read into the start of each of the two, the rest
 * zero, and the firmware loads it as the older version it is */
#define OLD_REC 2040u
static int blob_load(uint32_t i, void *p, uint32_t n)
{
    char path[600];
    FILE *f;
    long len;
    if (i >= NBLOB) return -1;
    if (q_load("blob", (int)i, p, n) == 0) return 0;
    if (q_path(path, sizeof path, "blob", (int)i) || !(f = fopen(path, "rb"))) return -1;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    if (len != 2L * OLD_REC || n < 2u * OLD_REC || n % 2u) { fclose(f); return -1; }
    memset(p, 0, n);
    if (fread(p, 1, OLD_REC, f) != OLD_REC || fread((uint8_t *)p + n / 2u, 1, OLD_REC, f) != OLD_REC) { fclose(f); return -1; }
    fclose(f);
    return 0;
}
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? q_save("blob", (int)i, p, n, 65536u) : -1; }
static int slot_load(uint32_t i, void *p, uint32_t n) { return i < NSLOT ? q_load("slot", (int)i, p, n) : -1; }
static int slot_save(uint32_t i, const void *p, uint32_t n) { return i < NSLOT ? q_save("slot", (int)i, p, n, SLOT_MAX) : -1; }

/* ------------------------------------------------------------------ MIDI in: a queue from the UI thread */
static uint8_t q_mq[256][3];
static _Atomic uint32_t q_mq_w, q_mq_r;
static int midi_get(uint8_t *st, uint8_t *d1, uint8_t *d2)
{
    uint32_t r = atomic_load(&q_mq_r);
    if (r == atomic_load_explicit(&q_mq_w, memory_order_acquire)) return 0;
    *st = q_mq[r % 256u][0]; *d1 = q_mq[r % 256u][1]; *d2 = q_mq[r % 256u][2];
    midi_last_ms = fm1_ms;                      /* (MIDI coming in: no disk access for a while) */
    atomic_store_explicit(&q_mq_r, r + 1u, memory_order_release);
    return 1;                                   /* (as USB) */
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

static void aset_load(void)
{
    aset_t a;
    aset3_t a3;
    aset1_t o;
    if (q_load("android", -1, &a, sizeof a) == 0 && a.ver == 4u) aset = a;
    else if (q_load("android", -1, &a3, sizeof a3) == 0 && a3.ver == 3u) {   /* (a file from before the tube's faults) */
        aset.lines = a3.lines; aset.dark = a3.dark; aset.pen = a3.pen; aset.vint = a3.vint; aset.odds = a3.odds;
        aset.drive = a3.drive; aset.burn = a3.burn; aset.persist = a3.persist; aset.draw = a3.draw; aset.warm = a3.warm;
    }
    else if (q_load("android", -1, &o, sizeof o) == 0 && (o.ver == 1u || o.ver == 2u)) {   /* (an older file: its VDU
                                                   settings kept, the vintage ones as new) */
        aset.lines = o.lines; aset.dark = o.dark; aset.pen = o.ver == 1u ? 1u : o.pen;
    }
    aset.ver = 4;
    if (aset.cap > 10u) aset.cap = 0; if (aset.fly > 10u) aset.fly = 0;
    if (aset.pen > 1u) aset.pen = 1;
    if (aset.lines > 4u) aset.lines = 2;
    if (aset.dark > 10u) aset.dark = 6;
    aset.vint &= 1u; if (aset.odds > 2u) aset.odds = 1; aset.drive &= 1u;
    if (aset.burn > 10u) aset.burn = 0; if (aset.persist > 10u) aset.persist = 0; if (aset.draw > 10u) aset.draw = 3; aset.warm &= 1u;
}
static void aset_save(void) { q_save("android", -1, &aset, sizeof aset, SECTOR_MAX); }

/* ------------------------------------------------------------------ the audio ring: the app's thread in, Oboe's out */
#define RING 8192u                              /* frames (a power of two) */
static int16_t q_ring[RING * 2u];
static _Atomic uint32_t q_rw, q_rr;             /* frames written, read */
static _Atomic uint32_t q_target = 1536u;       /* frames kept ready (~35 ms at 44.1 kHz) */
static _Atomic uint32_t q_underruns;
static uint32_t ring_fill(void) { return atomic_load(&q_rw) - atomic_load(&q_rr); }

static uint64_t q_t0;
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}


/* ------------------------------------------------------------------ (Global: VINTAGE) the drive and the degauss, heard
 * Mixed in after the engine (whose output fades while the disk runs, as the FM-1's does for its flash).
 *   floppy     a 5.25" drive, recorded (ullvieib, freesound.org, CC BY 4.0: sounds/README.md): DISK1 or DISK3 for a
 *              page, DISKSAVE for a patch saved or loaded
 *   hard disk  made here: the spindle's hum and a faint high whine; quick soft chatter, the seeks in bursts
 *   degauss    a thunk, then a 50 Hz hum dying away over a second and a half */
typedef struct { float y1, y2, a1, a2, env, dec; } click_t;   /* a struck resonance: a tick, a thump */
static click_t vt_cl[4];
static uint32_t vt_rs = 0x1234567u, vt_nstep, vt_wait, vt_prev, vt_pos;
static float vt_motor, vt_ph1, vt_ph2, dg_ph, dg_amp;
static uint32_t vt_play;                        /* the floppy's recording playing (as vt_smp), vt_pos into it */
#include "vt_samples.h"
static uint32_t dg_seen = 0xFFFFFFFFu;
static float vt_noise(void) { vt_rs ^= vt_rs << 13; vt_rs ^= vt_rs >> 17; vt_rs ^= vt_rs << 5; return (float)(int32_t)vt_rs * (1.0f / 2147483648.0f); }
static void vt_strike(uint32_t i, float hz, float q, float amp, float ms)   /* set a resonance ringing */
{
    float w = 2.0f * 3.14159265f * hz / 44100.0f, r = 1.0f - w / (2.0f * q);
    click_t *c = &vt_cl[i & 3u];
    c->a1 = 2.0f * r * cosf(w); c->a2 = -r * r; c->env = amp; c->dec = expf(-1.0f / (ms * 44.1f));
    c->y1 = c->y2 = 0.0f;
}
static float vt_click(click_t *c)
{
    float x = c->env * vt_noise(), y = x + c->a1 * c->y1 + c->a2 * c->y2;
    c->y2 = c->y1; c->y1 = y; c->env *= c->dec;
    return y;
}
static void vt_audio(int32_t *blk, uint32_t n)
{
    uint32_t i, k;
    int on = vt_busy && !vt_smp, hdd = 1;      /* (the synthesised drive: the hard disk; the floppy is recorded) */
    if (vt_busy && !vt_prev && vt_smp) { vt_play = vt_smp; vt_pos = 0; }   /* the floppy: its recording, from the top */
    if (vt_kick) { vt_play = vt_kick; vt_pos = 0; vt_kick = 0; }   /* (a save heard, vintage mode or not) */
    if (!vt_busy) vt_prev = 0;
    if (!on && !vt_play && !vt_kick && !vt_gap && vt_motor < 0.001f && dg_start_ms == dg_seen && dg_amp < 0.001f) return;
    if (on && !vt_prev) { vt_rs ^= vt_seed; vt_nstep = 0; vt_wait = 2000u; }   /* the hard disk spins up */
    if (vt_busy) vt_prev = 1;
    if (vt_play) {                              /* (the recording plays out whole, even after the page has moved on) */
        const int16_t *sm = vt_play == 1u ? VT_DISK1 : vt_play == 2u ? VT_DISK3 : VT_DISKSAVE;
        uint32_t len = vt_play == 1u ? sizeof VT_DISK1 / 2u : vt_play == 2u ? sizeof VT_DISK3 / 2u : sizeof VT_DISKSAVE / 2u;
        for (i = 0; i < n && vt_pos < len; i++, vt_pos++) { blk[2u * i] += sm[vt_pos]; blk[2u * i + 1u] += sm[vt_pos]; }
        if (vt_pos >= len) { vt_play = 0; if (vt_rep && vt_busy) vt_gap = 11025u; }   /* (trying again: a pause first) */
    } else if (vt_rep && vt_busy && vt_gap) {   /* the pause over: the next try */
        if (vt_gap > n) vt_gap -= n;
        else { vt_gap = 0; vt_play = vt_rep_smp; vt_pos = 0; vt_rep--; if (vt_rep_smp < 3u) vt_rep_smp = 3u - vt_rep_smp; }
    }
    if (dg_start_ms != dg_seen) { dg_seen = dg_start_ms; dg_amp = 1.0f; vt_strike(2, 55.0f, 2.0f, 90.0f, 120.0f); }
    for (i = 0; i < n; i++) {
        float o = 0.0f;
        vt_motor += ((on ? 1.0f : 0.0f) - vt_motor) * 0.0002f;   /* (spinning up and down, ~100 ms) */
        if (vt_motor > 0.001f) {
            vt_ph1 += (hdd ? 120.0f : 60.0f) / 44100.0f; if (vt_ph1 >= 1.0f) vt_ph1 -= 1.0f;
            vt_ph2 += (hdd ? 3600.0f : 300.0f) / 44100.0f; if (vt_ph2 >= 1.0f) vt_ph2 -= 1.0f;
            o += vt_motor * (450.0f * sinf(6.2831853f * vt_ph1) + 160.0f * sinf(12.566371f * vt_ph1)
                             + (hdd ? 70.0f : 110.0f) * sinf(6.2831853f * vt_ph2) + 60.0f * vt_noise());
            if (on) {                           /* the head: runs of steps, then a pause */
                if (vt_wait) vt_wait--;
                else if (vt_nstep) {
                    vt_strike(vt_nstep & 1u, hdd ? 1300.0f : 2600.0f, hdd ? 6.0f : 4.0f, hdd ? 3600.0f : 10000.0f, hdd ? 1.2f : 2.5f);
                    vt_nstep--;
                    vt_wait = hdd ? 90u + (vt_rs >> 25) : 260u + (vt_rs >> 24) % 80u;   /* (2 ms; 6 ms between steps) */
                } else {
                    vt_nstep = 1u + (vt_rs >> 27);  /* a seek: 1 .. 32 tracks */
                    vt_wait = (hdd ? 1500u : 4000u) + (vt_rs >> 20) % (hdd ? 6000u : 14000u);   /* (then a pause to read) */
                }
            }
        }
        for (k = 0; k < 4u; k++) if (vt_cl[k].env > 0.5f || vt_cl[k].y1 > 1.0f || vt_cl[k].y1 < -1.0f) o += vt_click(&vt_cl[k]) * 0.05f;
        if (dg_amp > 0.0005f) {                 /* the degauss's hum, dying away */
            dg_ph += 50.0f / 44100.0f; if (dg_ph >= 1.0f) dg_ph -= 1.0f;
            o += dg_amp * (2200.0f * sinf(6.2831853f * dg_ph) + 900.0f * sinf(18.849556f * dg_ph) + 300.0f * vt_noise());
            dg_amp *= 0.99996f;                 /* (~1.5 s) */
        }
        blk[2u * i] += (int32_t)o;
        blk[2u * i + 1u] += (int32_t)o;
    }
}

static void platform_poll(void)
{
    static int32_t blk[2u * HALF_FRAMES];
    uint32_t i, before = fm1_ms;
    while (ring_fill() + HALF_FRAMES <= atomic_load(&q_target)) {   /* render as the FM-1's ISR would, a block at a time */
        uint32_t w = atomic_load(&q_rw);
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
        vt_audio(blk, HALF_FRAMES);             /* (the drive, the degauss: after the engine's fade) */
        for (i = 0; i < HALF_FRAMES; i++) {
            int32_t l = blk[2u * i], r = blk[2u * i + 1u];
            q_ring[2u * ((w + i) % RING)] = (int16_t)(l > 32767 ? 32767 : l < -32767 ? -32767 : l);
            q_ring[2u * ((w + i) % RING) + 1u] = (int16_t)(r > 32767 ? 32767 : r < -32767 ? -32767 : r);
        }
        atomic_store_explicit(&q_rw, w + HALF_FRAMES, memory_order_release);
    }
    fm1_ms = (uint32_t)((now_us() - q_t0) / 1000u);
    if (fm1_ms == before) {                     /* (called again within the millisecond: rest, don't spin) */
        usleep(500);
        fm1_ms = (uint32_t)((now_us() - q_t0) / 1000u);
    }
}

/* ================================================================== the outside: called from quorum_jni.cpp */
static void *q_thread(void *arg)
{
    (void)arg;
    app_main();                                 /* (never returns) */
    for (;;) platform_poll();
    return NULL;
}
void quorum_start(const char *dir)
{
    static int started;
    pthread_t t;
    if (started) return;
    started = 1;
    snprintf(q_dir, sizeof q_dir, "%s", dir ? dir : "");
    q_t0 = now_us();
    aset_load();
    pthread_create(&t, NULL, q_thread, NULL);
    pthread_detach(t);
}
/* Oboe's callback: n stereo frames as float; whatever the ring lacks is silence */
void quorum_pull(float *out, int32_t n)
{
    uint32_t r = atomic_load(&q_rr), have = atomic_load_explicit(&q_rw, memory_order_acquire) - r, i, k = (uint32_t)n;
    if (have < k) { atomic_fetch_add(&q_underruns, 1u); k = have; }
    for (i = 0; i < k; i++) {
        out[2u * i] = (float)q_ring[2u * ((r + i) % RING)] * (1.0f / 32768.0f);
        out[2u * i + 1u] = (float)q_ring[2u * ((r + i) % RING) + 1u] * (1.0f / 32768.0f);
    }
    for (; i < (uint32_t)n; i++) out[2u * i] = out[2u * i + 1u] = 0.0f;
    atomic_store_explicit(&q_rr, r + k, memory_order_release);
}
void quorum_set_latency(uint32_t frames)
{
    if (frames < 512u) frames = 512u;
    if (frames > RING - 2u * HALF_FRAMES) frames = RING - 2u * HALF_FRAMES;
    atomic_store(&q_target, frames);
}
uint32_t quorum_underruns(void) { return atomic_load(&q_underruns); }

void quorum_button(int32_t id, int32_t down)
{
    uint32_t m;
    if (id < 0 || id > 31) return;
    m = 1u << id;
    if (down) { fm1_in.buttons |= m; atomic_fetch_or(&q_edges, m); }
    else { fm1_in.buttons &= ~m; atomic_fetch_or(&q_rel, m); }
}
void quorum_enc(int32_t id, int32_t detents) { if (id >= 0 && id < HOST_NENC) atomic_fetch_add(&q_enc[id], detents); }
void quorum_keys(uint32_t mask) { fm1_in.notes = mask; }
void quorum_midi(uint8_t st, uint8_t d1, uint8_t d2)
{
    uint32_t w = atomic_load(&q_mq_w);
    if (w - atomic_load(&q_mq_r) >= 256u) return;   /* (full: dropped) */
    q_mq[w % 256u][0] = st; q_mq[w % 256u][1] = d1; q_mq[w % 256u][2] = d2;
    atomic_store_explicit(&q_mq_w, w + 1u, memory_order_release);
}
uint64_t quorum_leds(void) { return atomic_load(&q_leds); }
uint64_t quorum_glow(void) { return atomic_load(&q_glow); }
/* the screen as ARGB_8888, 240 x 240 */
void quorum_copy_screen(uint32_t *argb)
{
    uint32_t i;
    for (i = 0; i < 240u * 240u; i++) {
        uint32_t c = fb[i], r = (c >> 11) * 255u / 31u, g = (c >> 5 & 63u) * 255u / 63u, b = (c & 31u) * 255u / 31u;
        argb[i] = 0xFF000000u | r << 16 | g << 8 | b;
    }
}

/* a touch on the screen: act 0 down, 1 move, 2 up; x, y where it landed on the glass, in the firmware's pixels
 * (0 .. 240). The tube bulges the picture, so what is drawn there is a little further out: the same sum as the
 * shader (CrtView.kt), so the finger lands on what it sees. */
void quorum_touch(int32_t act, float x, float y)
{
    float qx = x - 0.5f, qy = y - 0.5f, dx = qx - 120.0f, dy = qy - 120.0f, k2 = 30.0f * (dx * dx + dy * dy);
    float sx = qx + dx * k2 / 16777216.0f + 0.5f, sy = qy + dy * k2 / 16777216.0f + 0.5f;
    uint32_t w = atomic_load(&q_te_w);
    if (w - atomic_load(&q_te_r) >= 128u) return;
    q_te[w % 128u][0] = act; q_te[w % 128u][1] = (int32_t)(sx * 16.0f); q_te[w % 128u][2] = (int32_t)(sy * 16.0f);
    atomic_store_explicit(&q_te_w, w + 1u, memory_order_release);
}
