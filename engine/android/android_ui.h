/* SPDX-License-Identifier: GPL-3.0-only */
/* Quorum for Android: what the firmware's pages need to know about touch, read early (from android_platform.c) so the
 * drawing code can use it. The rest of the touch layer (android_touch.c) is read after the firmware.
 *
 * Touch targets: as a page draws itself it says where its touchable things are (a knob's readout, an Index line);
 * a tap goes to whatever is under the finger. The list is rebuilt every time the page is drawn. */

enum { T_KNOB, T_PAGE, T_DISK, T_MOD, T_GMAT, T_OPT, T_TAB, T_FGSHAPE, T_FGPEAK, T_FGEND, T_EGGRID,
       T_CMD, T_CHAR, T_ROLL, T_KSTRIP, T_KTRI, T_KPG, T_GRIDC };   /* T_GRIDC: a grid's cells (a = the grid) */   /* T_CMD: a command word in a page's footer (a = C_...);
                                                   T_CHAR: a letter of the Disk's name picker (a = its index); the piano
                                                   roll: its lane, its keys down the side, its triangles, its PG line */
/* the command words (each page's footer: tappable, as the CMI's command line). Most are what the FM-1's PLAY, SEL, SAVE,
 * REC, SEQ, OCT did on that page; a few were the ALGORITHM / PRESETS encoders' */
enum { C_SEED, C_FIRE, C_FILT, C_ROUTE, C_FREE, C_F0, C_ROOT, C_N, C_KEYS, C_GATE, C_REC, C_AUTO, C_CLEAR, C_NOFB,
       C_PHSLOW, C_DCLEAR, C_FREEZE, C_RVRATE, C_PATHA, C_PATHB, C_QUANT, C_RESETMAX, C_LOAD, C_SAVE, C_NAME, C_NLEFT,
       C_NRIGHT, C_NSPACE, C_NOK, C_NCANCEL, C_BANK, C_LOCK, C_SQRUN, C_DEGAUSS, C_INIT, C_RAND,
       C_TUNDN, C_TUNUP, C_SCLDN, C_SCLUP, C_ROOTDN, C_ROOTUP, NCMD };
static int32_t kn_old;                         /* a value as it was before the finger moved it (VOICE ROOT: the held
                                                   paths move with it) */   /* a knob's value (a = K1..K4: 0..3); an Index line (a = the
                                                   page); a Disk file (a = its slot); a crosspoint of the mod or gate
                                                   matrix (a = the source, b = the destination as the page orders
                                                   them); a phone-only switch (a: 0 the lightpen's cursor); a
                                                   Modulators tab (a = the part); an FG's shape, its peak, its end
                                                   (a = the FG); an envelope's grid (a = the envelope) */
typedef struct { int16_t x, y, w, h; uint8_t kind, a, b; } tgt_t;
#define NTGT 128
static tgt_t tgt[NTGT];
static uint32_t tgt_n;
static void tgt_add(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t kind, uint32_t a)   /* screen coordinates */
{
    if (tgt_n >= NTGT) return;
    tgt[tgt_n].x = (int16_t)x; tgt[tgt_n].y = (int16_t)y; tgt[tgt_n].w = (int16_t)w; tgt[tgt_n].h = (int16_t)h;
    tgt[tgt_n].kind = (uint8_t)kind; tgt[tgt_n].a = (uint8_t)a; tgt[tgt_n].b = 0;
    tgt_n++;
}
static void tgt_add2(int32_t x, int32_t y, int32_t w, int32_t h, uint32_t kind, uint32_t a, uint32_t b)
{
    tgt_add(x, y, w, h, kind, a);
    if (tgt_n && tgt[tgt_n - 1u].kind == kind) tgt[tgt_n - 1u].b = (uint8_t)b;
}

/* the phone's own settings (Global: VDU), kept in their own file beside the firmware's */
typedef struct { uint8_t ver, lines, dark, pen; } aset1_t;   /* (the file as builds before the vintage page wrote it) */
typedef struct { uint8_t ver, lines, dark, pen, vint, odds, drive, burn, persist, draw, warm, pad; } aset_t;
static aset_t aset = {3, 2, 6, 1, 0, 1, 0, 0, 0, 3, 0, 0};   /* lines: 0..4 (240 .. 600 a screen); dark: 0..10; pen: the
                                                   lightpen's cursor; (Global: VINTAGE) vint: vintage mode, off until
                                                   asked; odds: a disk access on a page change, rare / 1 in 10 /
                                                   cranky; drive: 5.25" floppy (recorded) / hard disk; burn, persist: 0..10; draw: the
                                                   page drawn in over draw x 80 ms; warm: a warm-up at launch */
static const uint8_t VT_ODDS[3] = {25, 10, 4};   /* a disk access: one page change in so many */

/* (Global: VINTAGE) the disk: an access running (the page frozen, the sound faded, the drive heard); the drive's sounds
 * and the degauss's are mixed in after the engine's fade (android_platform.c) */
static volatile uint32_t vt_busy, vt_write, vt_seed, vt_smp;   /* vt_smp: the floppy's recording playing (1 DISK1, 2 DISK3,
                                                   3 DISKSAVE; 0 the hard disk, heard as synthesised) */
static uint32_t vt_t0, vt_len, vt_snd_t;
static volatile uint32_t dg_start_ms = 0xFFFFFFFFu;   /* the degauss: when it started */
static uint32_t midi_last_ms = 0xFFFFFFFFu;     /* the last MIDI message read: a disk access waits 3 s after it */
static uint32_t aset_dirty, t_aset;
static const uint16_t ASET_LINES[5] = {240, 300, 360, 480, 600};

/* the gate matrix's sources, as the phone shows them: lit a moment after each gate, so a short one still flashes */
static uint32_t gate_seen[32];
#define GATE_HOLD_MS 90u

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

/* (Disk) a patch named on the phone's own keyboard: the engine asks (nm_ask: the slot), the app shows its dialog and
 * answers (nm_done: 1 a name in nm_name, 2 cancelled) */
static volatile uint32_t nm_ask, nm_shown, nm_done;
static volatile uint32_t pn_route, pn_st_dn[2], pn_st_up[2];
static _Atomic uint32_t pn_flip[2];             /* (the lights over the keyboard: cells of each grid's row to turn over) */   /* (the keyboard's buttons: the split, strike 1 and 2) */
static volatile uint32_t vt_kick;                /* a recording to start now, the page not frozen (1..3, as vt_smp) */
static uint32_t vt_cf_t0, vt_cf_on;              /* (the save's confirmation: the sound faded a moment) */
static void vt_saved(void);                    /* (android_touch.c) a patch saved: the drive heard, vintage or not */
static char nm_name[9];
static void cmd_label(uint32_t id, char *s);   /* (android_touch.c) a command word as it reads now */
static uint32_t shown_page = 0xFFFFFFFFu, shown_sub;   /* the page the screen is drawing (frozen while the disk runs) */
static const char *vt_message(void);           /* (android_touch.c) the header's line while the disk runs */
static void vt_disk(uint32_t write, uint32_t patch);   /* (android_touch.c) a disk access now (vintage mode: on); patch: a
                                                   patch saved or loaded */
static void touch_frame(uint32_t *b);          /* (android_touch.c) the taps, once a pass of the main loop */
static void touch_draw(void);                  /* (android_touch.c) the slide bar, over the body before the tube */
