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
static int blob_load(uint32_t i, void *p, uint32_t n) { return i < NBLOB ? q_load("blob", (int)i, p, n) : -1; }
static int blob_save(uint32_t i, const void *p, uint32_t n) { return i < NBLOB ? q_save("blob", (int)i, p, n, SECTOR_MAX) : -1; }
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
    atomic_store_explicit(&q_mq_r, r + 1u, memory_order_release);
    return 1;                                   /* (as USB) */
}
static void midi_send(uint32_t st, uint32_t d1, uint32_t d2) { (void)st; (void)d1; (void)d2; }

static void app_audio(int32_t *out, uint32_t n);
static void app_main(void);

#include "android_ui.h"                        /* touch targets, the phone's settings, the touch queue */
static void aset_load(void)
{
    aset_t a;
    if (q_load("android", -1, &a, sizeof a) == 0 && (a.ver == 1u || a.ver == 2u)) {
        if (a.ver == 1u) { a.ver = 2u; a.pen = 1u; }   /* (from before the lightpen's switch: on) */
        aset = a;
        if (aset.pen > 1u) aset.pen = 1;
        if (aset.lines > 4u) aset.lines = 2;
        if (aset.dark > 10u) aset.dark = 6;
    }
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

static void platform_poll(void)
{
    static int32_t blk[2u * HALF_FRAMES];
    uint32_t i, before = fm1_ms;
    while (ring_fill() + HALF_FRAMES <= atomic_load(&q_target)) {   /* render as the FM-1's ISR would, a block at a time */
        uint32_t w = atomic_load(&q_rw);
        memset(blk, 0, sizeof blk);
        app_audio(blk, HALF_FRAMES);
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
