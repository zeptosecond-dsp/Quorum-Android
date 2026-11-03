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
    k = &KN[knob & 3u];
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
static int scrolls(void) { return page == PG_MOD || page == PG_GMAT || page == PG_PATCH || (page == PG_SEQ && sub >= NSEQ); }

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

static void touch_frame(uint32_t *b)
{
    int32_t act, x16, y16;
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
            if (hit->kind == T_PAGE && page == PG_INDEX) {
                icur = hit->a;                  /* the Index opens it, as SEL does (set here, not as a SEL press:
                                                   a press would carry on into the new page, the Disk's "load") */
                page = IDX[icur].pg; sub = IDX[icur].sb; bank = 0; hdr_force = 1;
                bar_close();
            } else if (hit->kind == T_OPT) {    /* a phone-only switch */
                bar_close();
                if (hit->a == 0u) { aset.pen ^= 1u; aset_dirty = 1; t_aset = fm1_ms; }
            } else if (hit->kind == T_KNOB) {
                tgt_t t = *hit;
                value_tap(hit->a, &t);
            }
        } else if (act == TE_MOVE) {
            if (TB.drag) bar_drag(x, y);
            else if (TM.mode) mod_move(x, y);
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
