/* SPDX-License-Identifier: GPL-3.0-only */
/* Quorum for Android: the finger as a lightpen. Read after the firmware (quorum_engine.c), so it can reach the pages,
 * the knobs and the drawing.
 *
 *   the header     tap "PAGE n" for the Index; tap the title for the page's next part (as OCT); tap "BANK n" for the
 *                  next bank (as SEL)
 *   Index          tap a line to open that page
 *   a value        (a knob's readout, a VDU setting) tap it: a switch steps to its next setting; anything else gets
 *                  the slide bar. Drag along the bar to move the value from where it is; slide the finger up or
 *                  down, away from the bar, for finer steps. Tap the value again, or anywhere else, to put it away.
 *   lists          drag the page to scroll what runs off it: the Disk's files, the matrices' rows and columns, the
 *                  piano roll's notes (up and down) and pages (swipe left for the next). The cursor stays in view.
 *   Disk           tap a file to choose it.
 *   mod matrix     tap an empty crosspoint: a pin, and the slide bar for its depth (a soft stop at zero; zero is no
 *                  pin, as on the FM-1); tap a pin to choose it and bring the bar back, again to put the bar away;
 *                  two quick taps take it out.
 *   gate matrix    tap a crosspoint: a pin in or out.
 * A finger that lands on a file or a crosspoint and moves scrolls instead: only a tap that stays put is a tap.
 *   footers        each page's command words (what the FM-1's PLAY, SEL, SAVE, REC did there, and its ALGORITHM /
 *                  PRESETS switches): tap one. A value among them (VOICE's ROOT, SPLIT's N) gets the slide bar.
 *   Disk           LOAD, SAVE (tap again to confirm, as the FM-1's buttons), NAME: tap letters into the name, < > move.
 *   key sequencer  tap an empty cell for a note, drag right to tie it; drag a note's left part to move it, its right
 *                  end to stretch it; two quick taps take it out. Drag the keys down the side for higher or lower;
 *                  tap a triangle (each at its step's start, as page R) for that step, swipe them for another page;
 *                  tap the STEP line for the next page.
 *   Grids          every touch on the grid switches a cell of the row now (the bright one) on or off, by its column; a
 *                  swipe switches each cell it crosses; the grid grows from what is drawn. Tap LOCK for lockstep.
 *   Modulators     the tabs across the top go straight to a part. An FG: drag its peak sideways for RISE, its end for
 *                  FALL; tap the shape for the next CURVE. An envelope: drag a stroke across the grid and it snaps to
 *                  points; tap the grid for a point (or to choose the one there); tap a point to choose it (its
 *                  level, curve, gate and mark on K1-K4), again to let it go; drag a point to move it; two quick taps
 *                  take it out.
 *
 * Taps that the panel can do become the panel's own presses (b), so the firmware does exactly what it does for the
 * buttons; values are set as the knobs set them (kn_put, after_change). */

#define BAR_W 168
#define BAR_H 36

static struct {
    int scroll, moved;                          /* a drag in the page, scrolling its list */
    float sx, sy, ax, ay, x0, y0;
    int pending;                                /* a file or a crosspoint under the finger: its tap, if it stays put */
    tgt_t at;
} TS;
static int32_t pen_x = -1, pen_y = -1;          /* the lightpen's point: where the last touch landed, shown a moment */
static uint32_t pen_t, pen_down;
static uint32_t tap_id, tap_t;                  /* the last tap (kind, a, b) and when: two quick ones */
static int tap_double(const tgt_t *t)
{
    uint32_t id = 1u + ((uint32_t)t->kind << 16 | (uint32_t)t->a << 8 | t->b), d = id == tap_id && fm1_ms - tap_t < 350u;
    tap_id = d ? 0u : id; tap_t = fm1_ms;
    return d;
}

static struct {
    int open, drag;
    uint32_t knob, pg, sb, bk;
    tgt_t at;                                   /* the value it is for */
    int32_t bx, by;                             /* the bar, screen coordinates */
    float acc, lastx, fine;
} TB;

static void bar_close(void) { TB.open = 0; TB.drag = 0; }

static void bar_open(uint32_t knob, const tgt_t *t)
{
    TB.open = 1; TB.drag = 0; TB.knob = knob; TB.at = *t;
    TB.pg = page; TB.sb = sub; TB.bk = bank;
    TB.acc = (float)kn_get(&KN[knob]);
    TB.fine = 1.0f;
    TB.bx = t->x + t->w / 2 - BAR_W / 2;
    if (TB.bx < 2) TB.bx = 2;
    if (TB.bx > 238 - BAR_W) TB.bx = 238 - BAR_W;
    TB.by = t->y - BAR_H - 3;                   /* above the value; below it if there is no room */
    if (TB.by < BODY_Y + 2) TB.by = t->y + t->h + 3;
}

static void header_tap(float x, uint32_t *b)
{
    char s[40];
    int32_t page_w, title_w;
    header_title(s);
    title_w = 4 + text_w(&FONT_S, s);
    {
        char p[12] = "PAGE ";
        s_cat(p, page_num(page, sub));
        page_w = 4 + text_w(&FONT_S, p) + 6;
    }
    if (x < (float)page_w) { if (page != PG_INDEX) *b |= 1u << BTN_HOME; return; }
    if (page == PG_INDEX) return;
    if (page_banks(page, sub) > 1u && x >= (float)title_w) { *b |= 1u << BTN_SEL; return; }
    if (page == PG_MOD || page == PG_GMAT || (page_subs(page) > 1u && !(page == PG_SEQ && sub >= NSEQ))) *b |= 1u << BTN_OCT_UP;
}

static void value_tap(uint32_t knob, const tgt_t *t)
{
    kn_t *k;
    knobs_for(page, sub, bank);
    k = &KN[knob < (uint32_t)(sizeof KN / sizeof KN[0]) ? knob : 0u];
    if (k->fmt == F_NONE) return;
    if (TB.open && TB.knob == knob) { bar_close(); return; }
    if (k->hi - k->lo < 6) {                    /* a switch: the next setting, round again */
        int32_t v = kn_get(k) + 1;
        kn_put(k, v > k->hi ? k->lo : v);
        after_change(page, sub, knob, bank);
        knobs_for(page, sub, bank);
        bar_close();
        return;
    }
    bar_open(knob, t);
}

static void bar_drag(float x, float y)
{
    kn_t *k;
    float cy = (float)TB.by + BAR_H / 2.0f, dist = (y > cy ? y - cy : cy - y) - BAR_H / 2.0f, span;
    int32_t v;
    knobs_for(page, sub, bank);
    k = &KN[TB.knob];
    if (dist < 0.0f) dist = 0.0f;
    TB.fine = 1.0f + dist / 10.0f;              /* ~3 mm away: twice as fine, and on */
    if (TB.fine > 16.0f) TB.fine = 16.0f;
    span = (float)(k->hi - k->lo + 1);
    TB.acc += (x - TB.lastx) * span / (float)(BAR_W - 12) / TB.fine;
    TB.lastx = x;
    if (TB.acc < (float)k->lo) TB.acc = (float)k->lo;
    if (TB.acc > (float)k->hi) TB.acc = (float)k->hi;
    v = (int32_t)(TB.acc + (TB.acc >= 0.0f ? 0.5f : -0.5f));
    if (k->lo < 0 && k->hi > 0 && (TB.acc < 0.0f ? -TB.acc : TB.acc) < span * 0.03f) v = 0;   /* (a soft stop at zero) */
    if (v != kn_get(k)) {
        kn_old = kn_get(k);
        kn_put(k, v);
        after_change(page, sub, TB.knob, bank);
        knobs_for(page, sub, bank);
    }
}

/* scrolling: n rows (+ further down the list) and m columns (+ further right), by page */
static uint32_t clampu(int32_t v, int32_t lo, int32_t hi) { return (uint32_t)(v < lo ? lo : v > hi ? hi : v); }
static void scroll_by(int32_t n, int32_t m)
{
    if (page == PG_MOD) {
        mtop = clampu((int32_t)mtop + n, 0, (int32_t)NSRC - (int32_t)MROWS);
        mleft = clampu((int32_t)mleft + m, 0, (int32_t)NDST - (int32_t)MCOLS);
        mrow = clampu((int32_t)mrow, (int32_t)mtop, (int32_t)mtop + (int32_t)MROWS - 1);
        mcol = clampu((int32_t)mcol, (int32_t)mleft, (int32_t)mleft + (int32_t)MCOLS - 1);
    } else if (page == PG_GMAT) {
        gtop = clampu((int32_t)gtop + n, 0, (int32_t)NGS - 8);
        gleft = clampu((int32_t)gleft + m, 0, (int32_t)NGD - (int32_t)GCOLS);
        grow = clampu((int32_t)grow, (int32_t)gtop, (int32_t)gtop + 7);
        gcol = clampu((int32_t)gcol, (int32_t)gleft, (int32_t)gleft + (int32_t)GCOLS - 1);
    } else if (page == PG_PATCH && !p_ren) {
        uint32_t ln, cl_;
        p_top = clampu((int32_t)p_top + n, 0, (int32_t)P_LINES - (int32_t)P_SHOW);
        cl_ = p_line(pcur);
        if (cl_ < p_top || cl_ >= p_top + P_SHOW) {   /* the cursor onto the nearest file in view */
            int up = cl_ < p_top;
            for (ln = up ? p_top : p_top + P_SHOW - 1u; ln < P_LINES; ln = up ? ln + 1u : ln - 1u) {
                if (ln != 0u && ln != 3u) { pcur = ln == 1u ? 0u : ln == 2u ? P_RANDOM : ln - 3u; break; }
                if (!up && ln == 0u) break;
            }
            pconf = PC_NONE; pmsg = "";
        }
    } else if (page == PG_SEQ && sub >= NSEQ) {
        uint32_t steps = P.ks.steps < 1u ? 1u : P.ks.steps > KS_MAX ? KS_MAX : P.ks.steps;
        kv_top = clampu((int32_t)kv_top - n, 21 + (int32_t)KV_ROWS - 1, 96);
        kc_note = clampu((int32_t)kc_note, (int32_t)kv_top - (int32_t)KV_ROWS + 1, (int32_t)kv_top);
        if (m) { kc_pos = clampu((int32_t)kc_pos + m * (int32_t)KS_SPP, 0, (int32_t)steps - 1); t_kedit = fm1_ms; }
    }
}
static void scroll_drag(float x, float y)
{
    float rh = 12.0f, cw = 20.0f;               /* a row's, a column's worth of finger (in screen pixels) */
    int32_t n, m;
    if (page == PG_MOD) { rh = 17.0f; cw = 29.0f; }
    else if (page == PG_GMAT) { rh = 15.0f; cw = 17.0f; }
    else if (page == PG_PATCH) rh = 16.0f;
    else if (page == PG_SEQ) { rh = 8.0f; cw = 51.0f; }
    TS.ay += TS.sy - y; TS.ax += TS.sx - x;     /* (the finger up: further down; left: further right) */
    TS.sx = x; TS.sy = y;
    n = (int32_t)(TS.ay / rh); m = (int32_t)(TS.ax / cw);
    TS.ay -= (float)n * rh; TS.ax -= (float)m * cw;
    if (n || m) scroll_by(n, m);
}
static int scrolls(void) { return page == PG_MOD || page == PG_GMAT || page == PG_PATCH; }

static void pending_tap(const tgt_t *t)         /* a tap that stayed put, on a file or a crosspoint */
{
    int dbl = tap_double(t);
    if (t->kind == T_DISK) { if (!p_ren) { pcur = t->a; pconf = PC_NONE; pmsg = ""; } return; }
    if (t->kind == T_GMAT && page == PG_GMAT) {
        grow = t->a; gcol = t->b;
        P.gmat[t->a] ^= (uint16_t)(1u << t->b);
        return;
    }
    if (t->kind == T_MOD && page == PG_MOD) {
        int8_t *d = &P.dep[t->a][MD(t->b)];
        int same = mrow == t->a && mcol == t->b;
        if (dbl && *d) { *d = 0; dirty_partials = 1; bar_close(); return; }   /* two quick taps: out */
        if (same && TB.open) { bar_close(); return; }                     /* the bar away */
        mrow = t->a; mcol = t->b;
        if (!*d) { *d = 32; dirty_partials = 1; }                         /* a new pin, as PLAY puts it in */
        knobs_for(page, sub, bank);
        bar_open(2u, t);                                                  /* K3: DEPTH */
    }
}


/* ---- the Modulators: an FG's handles, an envelope's grid */
static struct {
    int mode;                                   /* 0 none, 1 an FG's peak, 2 its end, 3 an envelope's point, 4 a stroke */
    uint32_t k, moved;
    int32_t idx;
    float x0, y0;
    uint16_t n;
    float sx[256], sy[256];                     /* the stroke, in grid columns and levels */
} TM;

static void fg_drag(float x)
{
    fgp_t *f = &P.fg[TM.k];
    if (TM.mode == 1) {
        int32_t v = (int32_t)((x - (float)FGX0 - 8.0f) * 127.0f / 80.0f + 0.5f);
        f->rise = (uint8_t)cl(v, 0, 127);
        after_change(page, sub, 0u, bank);
    } else {
        int32_t v = (int32_t)((x - (float)(FGX0 + fg_w(f->rise) + fg_hold(TM.k)) - 8.0f) * 127.0f / 80.0f + 0.5f);
        f->fall = (uint8_t)cl(v, 0, 127);
        after_change(page, sub, 1u, bank);
    }
}
static float eg_gx(float x) { return (x - (float)EGX0) / (float)EGCW; }
static float eg_gy(float y) { return 16.0f - (y - (float)EGY0 - (float)BODY_Y) / (float)EGCH; }
static int32_t eg_hit(const egp_t *e, float x, float y)   /* the point nearest the finger, within reach, or -1 */
{
    int32_t best = -1;
    float bd = 9.0f * 9.0f;
    uint32_t i;
    for (i = 0; i < e->n; i++) {
        float dx = x - (float)EGX(e->pt[i].x), dy = y - (float)(EGY(e->pt[i].y) + BODY_Y), d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = (int32_t)i; }
    }
    return best;
}
static int32_t eg_insert(egp_t *e, uint32_t x, uint32_t y)   /* a point at column x (or the one there), its index */
{
    uint32_t i, j;
    for (i = 0; i < e->n && e->pt[i].x < x; i++) {}
    if (i < e->n && e->pt[i].x == x) { e->pt[i].y = (uint8_t)y; return (int32_t)i; }
    if (e->n >= EG_NPT) return -1;
    for (j = e->n; j > i; j--) e->pt[j] = e->pt[j - 1u];
    memset(&e->pt[i], 0, sizeof e->pt[i]);
    e->pt[i].x = (uint8_t)x; e->pt[i].y = (uint8_t)y; e->pt[i].crv = 2;
    e->n++;
    return (int32_t)i;
}
static void eg_delete(egp_t *e, uint32_t i)
{
    uint32_t j;
    if (i == 0u || i >= e->n) return;           /* (the first point stays: the envelope starts there) */
    for (j = i; j + 1u < e->n; j++) e->pt[j] = e->pt[j + 1u];
    e->n--;
}
static void eg_stroke(egp_t *e)                 /* a stroke into points: a point wherever its slope changes, column by
                                                   column; the points it passed over replaced */
{
    float col[33];
    int has[33] = {0};
    uint32_t i, c, lo = 33, hi = 0, nk = 0;
    int32_t ys[33];
    for (i = 1; i < TM.n; i++) {
        float x0 = TM.sx[i - 1u], y0 = TM.sy[i - 1u], x1 = TM.sx[i], y1 = TM.sy[i];
        int32_t a = (int32_t)ceilf(x0 < x1 ? x0 : x1), b = (int32_t)floorf(x0 < x1 ? x1 : x0), cc;
        for (cc = a; cc <= b; cc++) {
            if (cc < 0 || cc > 32) continue;
            col[cc] = x1 == x0 ? y1 : y0 + (y1 - y0) * ((float)cc - x0) / (x1 - x0);
            has[cc] = 1;
            if ((uint32_t)cc < lo) lo = (uint32_t)cc;
            if ((uint32_t)cc > hi) hi = (uint32_t)cc;
        }
    }
    if (lo > hi || lo == hi) return;
    for (c = lo; c <= hi; c++) ys[c] = has[c] ? cl((int32_t)(col[c] + 0.5f), 0, 16) : -1;
    {                                           /* out: the points in the stroke's columns (but the first point) */
        uint32_t j = 0;
        for (i = 0; i < e->n; i++) if (i == 0u || e->pt[i].x < lo || e->pt[i].x > hi) e->pt[j++] = e->pt[i];
        e->n = (uint8_t)j;
    }
    for (c = lo; c <= hi; c++) {                /* in: where the slope changes, and the ends */
        int keep;
        if (ys[c] < 0) continue;
        keep = c == lo || c == hi || ys[c - 1u] < 0 || ys[c + 1u] < 0 || ys[c] - ys[c - 1u] != ys[c + 1u] - ys[c];
        if (!keep) continue;
        if (c == 0u) { e->pt[0].y = (uint8_t)ys[c]; nk++; continue; }
        if (eg_insert(e, c, (uint32_t)ys[c]) >= 0) nk++;
    }
    (void)nk;
}
static int mod_down(const tgt_t *hit, float x, float y)   /* a finger down on the Modulators: handled here? */
{
    if (hit->kind == T_TAB) { bar_close(); sub = hit->a; bank = 0; hdr_force = 1; return 1; }
    if (hit->kind == T_FGPEAK || hit->kind == T_FGEND) {
        bar_close();
        TM.mode = hit->kind == T_FGPEAK ? 1 : 2; TM.k = hit->a; TM.moved = 0; TM.x0 = x; TM.y0 = y;
        return 1;
    }
    if (hit->kind == T_FGSHAPE) { bar_close(); TM.mode = 5; TM.k = hit->a; TM.moved = 0; TM.x0 = x; TM.y0 = y; return 1; }
    if (hit->kind == T_EGGRID) {
        egp_t *e = &P.eg[hit->a % NEG];
        int32_t i = eg_hit(e, x, y);
        bar_close();
        TM.k = hit->a % NEG; TM.moved = 0; TM.x0 = x; TM.y0 = y;
        if (i >= 0) { TM.mode = 3; TM.idx = i; }
        else { TM.mode = 4; TM.n = 1; TM.sx[0] = eg_gx(x); TM.sy[0] = eg_gy(y); }
        return 1;
    }
    return 0;
}
static void mod_move(float x, float y)
{
    float dx = x - TM.x0, dy = y - TM.y0;
    if (!TM.moved && dx * dx + dy * dy > 16.0f) TM.moved = 1;
    if (TM.mode == 1 || TM.mode == 2) { if (TM.moved) fg_drag(x); }
    else if (TM.mode == 3 && TM.moved) {        /* a point moved: between its neighbours, the first stays at 0 */
        egp_t *e = &P.eg[TM.k];
        int32_t i = TM.idx, gx = (int32_t)(eg_gx(x) + 0.5f), gy = (int32_t)(eg_gy(y) + 0.5f);
        if (i > 0) e->pt[i].x = (uint8_t)cl(gx, e->pt[i - 1].x + 1, i + 1 < (int32_t)e->n ? e->pt[i + 1].x - 1 : 32);
        e->pt[i].y = (uint8_t)cl(gy, 0, 16);
        eg_sel[TM.k] = i;
    } else if (TM.mode == 4 && TM.n < 256u) { TM.sx[TM.n] = eg_gx(x); TM.sy[TM.n] = eg_gy(y); TM.n++; }
}
static void mod_up(void)
{
    egp_t *e = &P.eg[TM.k % NEG];
    if (TM.mode == 5 && !TM.moved) {            /* an FG's shape tapped: the next curve */
        P.fg[TM.k].curve = (uint8_t)((P.fg[TM.k].curve + 1u) % 3u);
        after_change(page, sub, 2u, bank);
    } else if (TM.mode == 3 && !TM.moved) {     /* a point tapped: chosen; again, let go; twice quickly, out */
        tgt_t t = {0, 0, 0, 0, T_EGGRID, (uint8_t)TM.k, (uint8_t)TM.idx};
        if (tap_double(&t) && TM.idx > 0) { eg_delete(e, (uint32_t)TM.idx); eg_sel[TM.k] = -1; }
        else eg_sel[TM.k] = eg_sel[TM.k] == TM.idx ? -1 : TM.idx;
    } else if (TM.mode == 4 && !TM.moved) {     /* the grid tapped: a point there */
        int32_t gx = cl((int32_t)(TM.sx[0] + 0.5f), 0, 32), gy = cl((int32_t)(TM.sy[0] + 0.5f), 0, 16), i;
        i = eg_insert(e, (uint32_t)gx, (uint32_t)gy);
        eg_sel[TM.k] = i;
        if (i >= 0) { tgt_t t = {0, 0, 0, 0, T_EGGRID, (uint8_t)TM.k, (uint8_t)i}; tap_double(&t); }
    } else if (TM.mode == 4) { eg_stroke(e); eg_sel[TM.k] = -1; }
    TM.mode = 0;
}

/* ---- the grids: a cell of the row now, by its column */
static struct { int on; uint32_t g; int32_t last; } TG;
static void grid_cell(float x)
{
    int32_t c = (int32_t)((x - 8.0f) / 7.0f);
    grid_t *g = &G[TG.g & 1u];
    if (c < 0 || c >= (int32_t)NC || c == TG.last) return;
    TG.last = c;
    g->row ^= 1u << c;                          /* (the next step grows from it) */
    g->hist[(g->head + GHIST - 1u) % GHIST] = g->row;   /* (and the screen shows it now) */
    dirty_partials = 1;
}

/* ---- (Global: VINTAGE) the disk: now and then a page change waits on it; a patch saved or loaded always does */
static uint32_t vt_rnd = 0x2545F491u;
static uint32_t vt_rand(uint32_t n) { vt_rnd ^= vt_rnd << 13; vt_rnd ^= vt_rnd >> 17; vt_rnd ^= vt_rnd << 5; return n ? vt_rnd % n : 0u; }
static int vt_safe(void)                        /* never while the music depends on time: MIDI in, the keys running */
{
    if (P.ks.run || ks_rec || ks_auto) return 0;
    if (midi_last_ms != 0xFFFFFFFFu && fm1_ms - midi_last_ms < 3000u) return 0;
    return 1;
}
static void vt_disk(uint32_t write, uint32_t patch)
{
    static const uint32_t SMP_MS[4] = {0u, 3280u, 2362u, 1362u};   /* (the recordings' lengths) */
    if (!aset.vint || vt_busy || !vt_safe()) return;
    vt_write = write;
    if (aset.drive) { vt_smp = 0; vt_len = write ? 1600u + vt_rand(1400u) : 900u + vt_rand(1300u); }
    else { vt_smp = patch ? 3u : 1u + vt_rand(2u); vt_len = SMP_MS[vt_smp] + 120u; }   /* (the floppy: as long as it is heard) */
    vt_t0 = fm1_ms; vt_seed = vt_rand(0xFFFFFFFFu) | 1u;
    vt_busy = 1;
    mute = 1;                                   /* (the engine fades out, as for the FM-1's flash; the drive is heard) */
}
static const char *vt_message(void)
{
    static char m[32];
    uint32_t trk = (vt_seed >> 3) % 77u, el = fm1_ms - vt_t0;
    trk = (trk + el / 130u * 7u) % 77u;         /* (the head stepping about) */
    m[0] = 0;
    s_cat(m, aset.drive ? "DRIVE C: " : "DRIVE A: ");
    s_cat(m, vt_write ? "WRITE TRK " : "READ TRK ");
    if (trk < 10u) s_cat(m, "0");
    s_int(m, (int32_t)trk);
    return m;
}
static void vt_poll(void)                       /* each pass: a page changed (by anything) may wait; an access ends */
{
    static uint32_t vp = 0xFFFFFFFFu, vs;
    if (page != vp || sub != vs) {
        if (vp != 0xFFFFFFFFu && aset.vint && !vt_busy && vt_rand(VT_ODDS[aset.odds % 3u]) == 0u) vt_disk(0u, 0u);
        vp = page; vs = sub;
    }
    if (vt_busy && fm1_ms - vt_t0 >= vt_len) { vt_busy = 0; mute = 0; hdr_force = 1; }
}

/* ---- the command words */
static void cmd_label(uint32_t id, char *s)     /* (as the footer shows it now: the state in the word) */
{
    s[0] = 0;
    switch (id) {
    case C_SEED: s_cat(s, "SEED"); break;
    case C_FIRE: s_cat(s, "FIRE"); break;
    case C_FILT: s_cat(s, P.k[sub & 1u].filt ? "FILTER" : "SUM"); break;
    case C_ROUTE: s_cat(s, P.route_pre ? "PRE" : "POST"); break;
    case C_FREE: s_cat(s, P.gen[sub & 3u].mode ? "CLOCKED" : "FREE"); break;
    case C_F0: s_cat(s, P.anchor ? "F0 ON" : "F0 OFF"); break;
    case C_ROOT: s_cat(s, "ROOT "); s_note(s, P.root); break;
    case C_N: s_cat(s, "N "); s_int(s, P.sn); break;
    case C_KEYS: s_cat(s, P.kbd[(sub - 1u) & 1u] ? "KEYS ON" : "KEYS OFF"); break;
    case C_GATE: s_cat(s, P.seq[sub % NSEQ].gate[scur[sub % NSEQ] & 7u] ? "GATE ON" : "GATE OFF"); break;
    case C_SQRUN: s_cat(s, P.sq_stop[sub % NSEQ] ? "RUN OFF" : "RUN ON"); break;
    case C_DEGAUSS: s_cat(s, "DEGAUSS"); break;
    case C_REC: s_cat(s, ks_rec ? "REC ON" : "REC"); break;
    case C_AUTO: s_cat(s, ks_auto ? "AUTO ON" : "AUTO"); break;
    case C_CLEAR: s_cat(s, ks_clr && fm1_ms - t_clr < 2000u ? "CLEAR?" : "CLEAR"); break;
    case C_NOFB: s_cat(s, "NO FB"); break;
    case C_PHSLOW: s_cat(s, P.ph.slow ? "11K" : "22K"); break;
    case C_DCLEAR: s_cat(s, "CLEAR"); break;
    case C_FREEZE: s_cat(s, P.rv.freeze ? "FROZEN" : "FREEZE"); break;
    case C_RVRATE: s_cat(s, P.rv.rate ? "11K" : "22K"); break;
    case C_QUANT: s_cat(s, qsel ? "QUANT B " : "QUANT A "); s_cat(s, P.qon[qsel & 1u] ? "ON" : "OFF"); break;
    case C_RESETMAX: s_cat(s, "RESET MAX"); break;
    case C_LOAD: s_cat(s, pconf == PC_LOAD ? "LOAD?" : "LOAD"); break;
    case C_SAVE: s_cat(s, pconf == PC_SAVE ? "SAVE?" : "SAVE"); break;
    case C_NAME: s_cat(s, "NAME"); break;
    case C_NLEFT: s_cat(s, "<"); break;
    case C_NRIGHT: s_cat(s, ">"); break;
    case C_NSPACE: s_cat(s, "SPACE"); break;
    case C_NOK: s_cat(s, "OK"); break;
    case C_NCANCEL: s_cat(s, "CANCEL"); break;
    case C_LOCK: s_cat(s, "LOCK"); break;
    case C_BANK: s_cat(s, "BANK "); s_int(s, (int32_t)bank + 1); s_cat(s, "/"); s_int(s, (int32_t)page_banks(page, sub)); break;
    default: break;
    }
}
static void cmd_tap(const tgt_t *t, uint32_t *b)   /* a command word tapped: the FM-1's press, or the switch itself */
{
    switch (t->a) {
    case C_SEED: case C_FIRE: case C_KEYS: case C_GATE: case C_NOFB: case C_DCLEAR: case C_FREEZE: case C_QUANT:
    case C_RESETMAX: case C_NAME: case C_NCANCEL:
        *b |= 1u << BTN_PLAY; break;            /* (what PLAY did on this page) */
    case C_F0: case C_LOAD: case C_BANK: *b |= 1u << BTN_SEL; break;   /* (SEL: a page's banks, VOICE's F0, the Disk's load) */
    case C_SAVE: case C_NOK: *b |= 1u << BTN_SAVE; break;
    case C_REC: *b |= 1u << BTN_REC; break;
    case C_CLEAR: *b |= 1u << BTN_SEQ; break;   /* (twice in 2 s: clear) */
    case C_NLEFT: *b |= 1u << BTN_OCT_DOWN; break;
    case C_NRIGHT: *b |= 1u << BTN_OCT_UP; break;
    case C_NSPACE: if (p_ren) { p_rname[p_rpos] = ' '; p_rpos = (p_rpos + 1u) & 7u; } break;
    case C_AUTO: ks_auto ^= 1u; if (ks_auto) { P.ks.run = 0; ks_rec = 0; ks_run_was = 0; } break;   /* (SEL + REC) */
    case C_FILT: P.k[sub & 1u].filt ^= 1u; dirty_partials = 1; break;
    case C_ROUTE: P.route_pre ^= 1u; dirty_partials = 1; break;
    case C_FREE: P.gen[sub & 3u].mode ^= 1u; dirty_partials = 1; break;
    case C_PHSLOW: P.ph.slow ^= 1u; break;
    case C_RVRATE: P.rv.rate ^= 1u; dirty_partials = 1; break;
    case C_SQRUN: P.sq_stop[sub % NSEQ] ^= 1u; break;   /* (its own clock only: a gate or an address still moves it) */
    case C_DEGAUSS: dg_start_ms = fm1_ms; break;   /* (the wobble on the screen, the thunk and hum in the sound) */
    case C_PATHA: qsel = 0; break;
    case C_LOCK: P.lock ^= 1u; after_change(PG_GRID, sub, 3u, 1u); break;   /* (as bank 2's K4: B in step with A) */
    case C_PATHB: qsel = 1; break;
    case C_ROOT: case C_N: {                    /* a value: the slide bar */
        tgt_t v = *t;
        knobs_for(page, sub, bank);
        kn_old = kn_get(&KN[4]);
        value_tap(4u, &v);
    } break;
    default: break;
    }
}

/* ---- the piano roll (the firmware's page R: x0 26, a step 51 wide, rows 8 high from y 2, 14 rows; four steps a page).
 * A note being placed or moved is the firmware's own floating bar (kc_place), drawn flashing, put down on lift. */
static struct {
    int mode;                                   /* 0 none, 1 stretch, 2 move, 3 the keys (scroll), 4 the triangles */
    uint32_t moved, fresh, p0;
    int32_t grab, top0, start;
    float x0, y0;
} TK;
static uint32_t ks_steps(void) { return P.ks.steps < 1u ? 1u : P.ks.steps > KS_MAX ? KS_MAX : P.ks.steps; }
static uint32_t ks_page0(void)                  /* the first step of the page shown (as draw_keys picks it) */
{
    return ((P.ks.run && !kc_place && fm1_ms - t_kedit > 3000u ? ks_pos_v : kc_pos) / KS_SPP) * KS_SPP;
}
static void ks_cell(float x, float y, uint32_t *pos, uint32_t *note)
{
    int32_t c = cl((int32_t)((x - 26.0f) / 51.0f), 0, 3), r = cl((int32_t)((y - (float)BODY_Y - 2.0f) / 8.0f), 0, (int32_t)KV_ROWS - 1);
    *pos = TK.p0 + (uint32_t)c; *note = kv_top - (uint32_t)r;
}
static int keys_down(const tgt_t *hit, float x, float y)
{
    uint32_t pos, note, steps = ks_steps();
    if (hit->kind == T_KSTRIP) { TK.mode = 3; TK.y0 = y; TK.top0 = (int32_t)kv_top; return 1; }
    if (hit->kind == T_KTRI) { TK.mode = 4; TK.x0 = x; TK.y0 = y; TK.moved = 0; TK.p0 = ks_page0(); return 1; }
    if (hit->kind == T_KPG) {                   /* the next page (round again) */
        uint32_t npg = (steps + KS_SPP - 1u) / KS_SPP;
        kc_pos = ((ks_page0() / KS_SPP + 1u) % npg) * KS_SPP; kc_len = 1; t_kedit = fm1_ms;
        return 1;
    }
    if (hit->kind != T_ROLL) return 0;
    TK.p0 = ks_page0();
    ks_cell(x, y, &pos, &note);
    t_kedit = fm1_ms;
    if (pos >= steps) return 1;
    TK.x0 = x; TK.y0 = y; TK.moved = 0;
    {
        int32_t at = ks_at(pos);
        if (at >= 0 && P.ks.note[at] == note) {  /* a note: picked up; its right end stretches, the rest moves */
            uint32_t len = P.ks.len[at] ? P.ks.len[at] : 1u, endv = (uint32_t)at + len - 1u;
            float frac = (x - 26.0f) / 51.0f - (float)(int32_t)((x - 26.0f) / 51.0f);
            if (endv > TK.p0 + 3u) endv = TK.p0 + 3u;
            TK.mode = pos == endv && (len > 1u || frac > 0.5f) ? 1 : 2;
            TK.grab = (int32_t)pos - at; TK.fresh = 0; TK.start = at;
            kc_pos = (uint32_t)at; kc_note = note; kc_len = len; kc_picked = 1; kc_place = 1;
            P.ks.note[at] = 0; P.ks.len[at] = 0;
        } else {                                /* an empty cell: a new note, stretched by the drag */
            TK.mode = 1; TK.fresh = 1; TK.grab = 0; TK.start = (int32_t)pos;
            kc_pos = pos; kc_note = note; kc_len = 1; kc_picked = 0; kc_place = 1;
        }
    }
    return 1;
}
static void keys_move(float x, float y)
{
    float dx = x - TK.x0, dy = y - TK.y0;
    uint32_t pos, note, steps = ks_steps();
    if (!TK.moved && dx * dx + dy * dy > 16.0f) TK.moved = 1;
    t_kedit = fm1_ms;
    if (TK.mode == 3) { kv_top = (uint32_t)cl(TK.top0 + (int32_t)((y - TK.y0) / 8.0f), 21 + (int32_t)KV_ROWS - 1, 96); return; }
    if (TK.mode == 4) return;
    ks_cell(x, y, &pos, &note);
    if (TK.mode == 1) kc_len = (uint32_t)cl((int32_t)pos - (int32_t)kc_pos + 1, 1, (int32_t)(steps - kc_pos));
    else if (TK.mode == 2 && TK.moved) {
        kc_pos = (uint32_t)cl((int32_t)pos - TK.grab, 0, (int32_t)steps - 1);
        kc_note = note;
        if (kc_len > steps - kc_pos) kc_len = steps - kc_pos;
    }
}
static void keys_up(float x)
{
    if (TK.mode == 4) {                         /* the triangles: a tap, that step; a swipe, a page */
        uint32_t steps = ks_steps(), npg = (steps + KS_SPP - 1u) / KS_SPP, pg = TK.p0 / KS_SPP;
        float dx = x - TK.x0;
        if (dx > 24.0f || dx < -24.0f) pg = dx < 0.0f ? (pg + 1u) % npg : (pg + npg - 1u) % npg;
        else { uint32_t j = (uint32_t)cl((int32_t)((x - 26.0f) / 51.0f + 0.5f), 0, 3);   /* (the triangle nearest: each at its step's start) */ kc_pos = TK.p0 + j < steps ? TK.p0 + j : steps - 1u; kc_len = 1; TK.mode = 0; t_kedit = fm1_ms; return; }
        kc_pos = pg * KS_SPP; kc_len = 1; t_kedit = fm1_ms;
    } else if (TK.mode == 1 || TK.mode == 2) {
        tgt_t t = {0, 0, 0, 0, T_ROLL, (uint8_t)kc_pos, (uint8_t)kc_note};
        int dbl = !TK.moved && tap_double(&t);
        if (dbl && !TK.fresh) kc_place = 0;     /* two quick taps: out (it stays picked up, and goes) */
        else { ks_put(kc_pos, kc_note, kc_len); kc_place = 0; }
        t_kedit = fm1_ms;
    }
    TK.mode = 0;
}

static void touch_frame(uint32_t *b)
{
    int32_t act, x16, y16;
    vt_poll();
    if (vt_busy) {                              /* (the machine is busy: touches go nowhere) */
        while (touch_take(&act, &x16, &y16)) { pen_x = x16 >> 4; pen_y = y16 >> 4; pen_t = fm1_ms; pen_down = act != TE_UP; }
        TS.scroll = TS.pending = 0; TB.drag = 0; TM.mode = 0; TK.mode = 0; TG.on = 0; kc_place = 0;
        return;
    }
    if (TB.open && (TB.pg != page || TB.sb != sub || TB.bk != bank)) bar_close();
    {                                           /* the gate sources seen high, for the gate matrix's flashes */
        uint32_t g = gsrc_v, i;
        for (i = 0; g && i < 32u; i++, g >>= 1) if (g & 1u) gate_seen[i] = fm1_ms;
    }
    while (touch_take(&act, &x16, &y16)) {
        float x = (float)x16 / 16.0f, y = (float)y16 / 16.0f;
        pen_x = x16 >> 4; pen_y = y16 >> 4; pen_t = fm1_ms; pen_down = act != TE_UP;
        if (act == TE_DOWN) {
            uint32_t i;
            const tgt_t *hit = 0;
            if (TB.open && x >= TB.bx - 6 && x < TB.bx + BAR_W + 6 && y >= TB.by - 6 && y < TB.by + BAR_H + 6) {
                TB.drag = 1; TB.lastx = x;
                continue;
            }
            if (y < (float)BODY_Y) { bar_close(); header_tap(x, b); continue; }
            for (i = tgt_n; i-- > 0;)
                if (x >= tgt[i].x && x < tgt[i].x + tgt[i].w && y >= tgt[i].y && y < tgt[i].y + tgt[i].h) { hit = &tgt[i]; break; }
            if (!hit || hit->kind == T_DISK || hit->kind == T_MOD || hit->kind == T_GMAT) {
                /* the page itself, a file or a crosspoint: a drag scrolls; a tap that stays put is a tap (on up) */
                if (!hit) bar_close();
                TS.scroll = scrolls(); TS.moved = 0; TS.sx = TS.x0 = x; TS.sy = TS.y0 = y; TS.ax = TS.ay = 0.0f;
                TS.pending = hit != 0;
                if (hit) TS.at = *hit;
                continue;
            }
            if (page == PG_FG && mod_down(hit, x, y)) continue;
            if (page == PG_SEQ && sub >= NSEQ && keys_down(hit, x, y)) { bar_close(); continue; }
            if (hit->kind == T_GRIDC && page == PG_GRID) { bar_close(); TG.on = 1; TG.g = hit->a; TG.last = -1; grid_cell(x); continue; }
            if (hit->kind == T_CMD) { if (hit->a != C_ROOT && hit->a != C_N) bar_close(); cmd_tap(hit, b); continue; }
            if (hit->kind == T_CHAR) { if (p_ren) { p_rname[p_rpos] = NAME_CH[hit->a % NCHARS]; p_rpos = (p_rpos + 1u) & 7u; } continue; }
            if (hit->kind == T_PAGE && page == PG_INDEX) {
                icur = hit->a;                  /* the Index opens it, as SEL does (set here, not as a SEL press:
                                                   a press would carry on into the new page, the Disk's "load") */
                page = IDX[icur].pg; sub = IDX[icur].sb; bank = 0; hdr_force = 1;
                bar_close();
            } else if (hit->kind == T_OPT) {    /* a phone-only switch */
                bar_close();
                if (hit->a == 0u) { aset.pen ^= 1u; aset_dirty = 1; t_aset = fm1_ms; }
                if (hit->a == 1u) { aset.vint ^= 1u; aset_dirty = 1; t_aset = fm1_ms; }   /* (VINTAGE MODE) */
            } else if (hit->kind == T_KNOB) {
                tgt_t t = *hit;
                value_tap(hit->a, &t);
            }
        } else if (act == TE_MOVE) {
            if (TB.drag) bar_drag(x, y);
            else if (TM.mode) mod_move(x, y);
            else if (TK.mode) keys_move(x, y);
            else if (TG.on) grid_cell(x);
            else if (TS.scroll || TS.pending) {
                float dx = x - TS.x0, dy = y - TS.y0;
                if (!TS.moved && dx * dx + dy * dy > 25.0f) {   /* (5 pixels: it's a drag) */
                    TS.moved = 1; TS.pending = 0;
                    if (TS.scroll) bar_close();         /* (what the bar points at moves away) */
                }
                if (TS.moved && TS.scroll) scroll_drag(x, y);
            }
        } else {
            if (TM.mode) mod_up();
            if (TK.mode) keys_up(x);
            TG.on = 0;
            if (TS.pending && !TS.moved) pending_tap(&TS.at);
            TS.pending = 0;
            TS.scroll = 0;
            TB.drag = 0;
            TB.fine = 1.0f;
        }
    }
}

static void pen_draw(void)                      /* the lightpen's point: a small cross, while down and a moment after */
{
    int32_t x = pen_x, y = pen_y - BODY_Y;
    if (!aset.pen || pen_x < 0 || (!pen_down && fm1_ms - pen_t > 400u) || y < 0 || y >= BODY_H) return;
    cv_rect(x - 6, y, 4, 1, PH_HI); cv_rect(x + 3, y, 4, 1, PH_HI);
    cv_rect(x, y - 6, 1, 4, PH_HI); cv_rect(x, y + 3, 1, 4, PH_HI);
    cv_rect(x, y, 1, 1, PH_MID);
}

static void touch_draw(void)                    /* on the body's canvas (y from BODY_Y), before the tube */
{
    char s[24];
    int32_t bx = TB.bx, by = TB.by - BODY_Y, ax = TB.at.x, ay = TB.at.y - BODY_Y, x, pos;
    kn_t *k;
    pen_draw();
    if (TM.mode == 4 && TM.moved && page == PG_FG) {   /* the stroke being drawn, dim */
        uint32_t i;
        for (i = 1; i < TM.n; i++) {
            int32_t x0 = EGX0 + (int32_t)(TM.sx[i - 1u] * EGCW), y0 = EGY0 + (int32_t)((16.0f - TM.sy[i - 1u]) * EGCH);
            int32_t x1 = EGX0 + (int32_t)(TM.sx[i] * EGCW), y1 = EGY0 + (int32_t)((16.0f - TM.sy[i]) * EGCH), st, j;
            st = iabs(x1 - x0) > iabs(y1 - y0) ? iabs(x1 - x0) : iabs(y1 - y0);
            for (j = 0; j <= st; j++) cv_pset(x0 + (st ? (x1 - x0) * j / st : 0), y0 + (st ? (y1 - y0) * j / st : 0), PH_LO);
        }
    }
    if (!TB.open || TB.pg != page || TB.sb != sub || TB.bk != bank) return;
    knobs_for(page, sub, bank);
    k = &KN[TB.knob];
    /* the value it is for, boxed */
    cv_rect(ax, ay, TB.at.w, 1, PH_HI); cv_rect(ax, ay + TB.at.h - 1, TB.at.w, 1, PH_HI);
    cv_rect(ax, ay, 1, TB.at.h, PH_HI); cv_rect(ax + TB.at.w - 1, ay, 1, TB.at.h, PH_HI);
    /* the bar */
    cv_rect(bx, by, BAR_W, BAR_H, PH_BG);
    cv_rect(bx, by, BAR_W, 1, PH_HI); cv_rect(bx, by + BAR_H - 1, BAR_W, 1, PH_HI);
    cv_rect(bx, by, 1, BAR_H, PH_HI); cv_rect(bx + BAR_W - 1, by, 1, BAR_H, PH_HI);
    cv_text(bx + 4, by + 2, &FONT_S, k->name, PH_LO);
    kn_text(s, k, sub ? (sub - 1u) & 1u : 0u);
    cv_text(bx + 4 + text_w(&FONT_S, k->name) + 8, by + 2, &FONT_S, s, PH_HI);
    if (TB.drag && TB.fine >= 1.5f) {
        char f[12] = "FINE X";
        s_int(f, (int32_t)(TB.fine + 0.5f));
        cv_text(bx + BAR_W - 4 - text_w(&FONT_S, f), by + 2, &FONT_S, f, PH_HI);
    } else cv_text(bx + BAR_W - 4 - text_w(&FONT_S, "< >"), by + 2, &FONT_S, "< >", PH_LO);
    for (x = bx + 6; x < bx + BAR_W - 6; x += 3) cv_pset(x, by + BAR_H - 8, PH_MID);
    pos = k->hi > k->lo ? (kn_get(k) - k->lo) * (BAR_W - 12) / (k->hi - k->lo) : 0;
    cv_rect(bx + 6 + pos - 1, by + BAR_H - 13, 3, 10, PH_HI);
}
