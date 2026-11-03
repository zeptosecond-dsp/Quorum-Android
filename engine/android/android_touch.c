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
 *
 * Taps that the panel can do become the panel's own presses (b), so the firmware does exactly what it does for the
 * buttons; values are set as the knobs set them (kn_put, after_change). */

#define BAR_W 168
#define BAR_H 36

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
        s_cat(p, PG_NUM[page]);
        page_w = 4 + text_w(&FONT_S, p) + 6;
    }
    if (x < (float)page_w) { if (page != PG_INDEX) *b |= 1u << BTN_HOME; return; }
    if (page == PG_INDEX) return;
    if (page_banks(page, sub) > 1u && x >= (float)title_w) { *b |= 1u << BTN_SEL; return; }
    if (page_subs(page) > 1u) *b |= 1u << BTN_OCT_UP;
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
    if (v != kn_get(k)) {
        kn_put(k, v);
        after_change(page, sub, TB.knob, bank);
        knobs_for(page, sub, bank);
    }
}

static void touch_frame(uint32_t *b)
{
    int32_t act, x16, y16;
    if (TB.open && (TB.pg != page || TB.sb != sub || TB.bk != bank)) bar_close();
    while (touch_take(&act, &x16, &y16)) {
        float x = (float)x16 / 16.0f, y = (float)y16 / 16.0f;
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
            if (!hit) { bar_close(); continue; }
            if (hit->kind == T_PAGE && page == PG_INDEX) {
                icur = hit->a;                  /* the Index opens it, as SEL does */
                *b |= 1u << BTN_SEL;
                bar_close();
            } else if (hit->kind == T_KNOB) {
                tgt_t t = *hit;
                value_tap(hit->a, &t);
            }
        } else if (act == TE_MOVE) {
            if (TB.drag) bar_drag(x, y);
        } else {
            TB.drag = 0;
            TB.fine = 1.0f;
        }
    }
}

static void touch_draw(void)                    /* on the body's canvas (y from BODY_Y), before the tube */
{
    char s[24];
    int32_t bx = TB.bx, by = TB.by - BODY_Y, ax = TB.at.x, ay = TB.at.y - BODY_Y, x, pos;
    kn_t *k;
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
