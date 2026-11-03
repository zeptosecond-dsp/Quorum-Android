/* SPDX-License-Identifier: GPL-3.0-only */
/* Quorum for Android: what the firmware's pages need to know about touch, read early (from android_platform.c) so the
 * drawing code can use it. The rest of the touch layer (android_touch.c) is read after the firmware.
 *
 * Touch targets: as a page draws itself it says where its touchable things are (a knob's readout, an Index line);
 * a tap goes to whatever is under the finger. The list is rebuilt every time the page is drawn. */

enum { T_KNOB, T_PAGE };                        /* a knob's value (a = K1..K4: 0..3); an Index line (a = the page) */
typedef struct { int16_t x, y, w, h; uint8_t kind, a; } tgt_t;
#define NTGT 64
static tgt_t tgt[NTGT];
static uint32_t tgt_n;
static void tgt_add(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t kind, uint32_t a)   /* screen coordinates */
{
    if (tgt_n >= NTGT) return;
    tgt[tgt_n].x = (int16_t)x; tgt[tgt_n].y = (int16_t)y; tgt[tgt_n].w = (int16_t)w; tgt[tgt_n].h = (int16_t)h;
    tgt[tgt_n].kind = (uint8_t)kind; tgt[tgt_n].a = (uint8_t)a;
    tgt_n++;
}

/* the phone's own settings (Global: VDU), kept in their own file beside the firmware's */
typedef struct { uint8_t ver, lines, dark, pad; } aset_t;
static aset_t aset = {1, 2, 6, 0};               /* lines: 0..4 (240 .. 600 a screen); dark: 0..10 */
static uint32_t aset_dirty, t_aset;
static const uint16_t ASET_LINES[5] = {240, 300, 360, 480, 600};

/* touch events from the UI thread: x, y in the firmware's screen pixels x 16 (what is drawn there, the tube's bulge
 * already undone) */
enum { TE_DOWN, TE_MOVE, TE_UP };
static int32_t q_te[128][3];
static _Atomic uint32_t q_te_w, q_te_r;
static int touch_take(int32_t *act, int32_t *x16, int32_t *y16)
{
    uint32_t r = atomic_load(&q_te_r);
    if (r == atomic_load_explicit(&q_te_w, memory_order_acquire)) return 0;
    *act = q_te[r % 128u][0]; *x16 = q_te[r % 128u][1]; *y16 = q_te[r % 128u][2];
    atomic_store_explicit(&q_te_r, r + 1u, memory_order_release);
    return 1;
}

static void touch_frame(uint32_t *b);          /* (android_touch.c) the taps, once a pass of the main loop */
static void touch_draw(void);                  /* (android_touch.c) the slide bar, over the body before the tube */
