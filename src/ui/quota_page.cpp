#include "quota_page.h"
#include "ui_common.h"
#include <math.h>
#include <stdio.h>

// ---- Layout (content area under the shared header: y 46..220) ----
#define CARD_W    ((UI_SCR_W - 2 * UI_MARGIN - UI_GAP) / 2)   // 143
#define CARD_H    UI_AREA_H                                    // 174
#define CARD_PAD  10
#define INNER_W   (CARD_W - 2 * CARD_PAD)                      // 123
#define RING_D    108
#define RING_W    10
#define RING_X    ((INNER_W - RING_D) / 2)
#define RING_Y    0
#define MARK_D    12          // time marker: white dot with a card-colored rim
#define LINE1_Y   (RING_Y + RING_D + 5)
#define LINE2_Y   (LINE1_Y + 21)

#define WIN_5H_S  (5 * 3600)
#define WIN_7D_S  (7 * 86400)

struct gauge {
    lv_obj_t *arc, *num, *pct, *name, *mark, *left, *when;
    long window_s;
    uint32_t reset_at;   // epoch of the next reset, 0 = unknown
    int used;            // last % used, -1 = unknown
};

static gauge g5, g7;
static int g_tz = 0;

static lv_obj_t *make_ring(lv_obj_t *parent, lv_coord_t d, lv_coord_t w)
{
    lv_obj_t *a = lv_arc_create(parent);
    lv_obj_set_size(a, d, d);
    lv_arc_set_rotation(a, 135);
    lv_arc_set_bg_angles(a, 0, 270);
    lv_arc_set_range(a, 0, 100);
    lv_arc_set_value(a, 0);
    lv_obj_remove_style(a, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(a, 0, 0);
    lv_obj_set_style_arc_width(a, w, LV_PART_MAIN);
    lv_obj_set_style_arc_color(a, CL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(a, w, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(a, CL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(a, true, LV_PART_INDICATOR);
    return a;
}

static void make_gauge(gauge &g, lv_obj_t *body, lv_coord_t x, const char *name, long window_s)
{
    g.window_s = window_s;
    g.reset_at = 0;
    g.used = -1;

    lv_obj_t *card = ui_card(body, x, UI_TOP_Y, CARD_W, CARD_H, CARD_PAD, CARD_PAD);

    g.arc = make_ring(card, RING_D, RING_W);
    lv_obj_set_pos(g.arc, RING_X, RING_Y);

    // "37" + "%" sitting on the same baseline, centered in the ring
    lv_obj_t *row = lv_obj_create(g.arc);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 1, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -8);
    g.num = ui_label(row, &font_temp_42, CL_TEXT, "--");
    g.pct = ui_label(row, &lv_font_montserrat_16, CL_DIM, "%");
    lv_obj_set_style_translate_y(g.pct, -2, 0);
    lv_obj_add_flag(g.pct, LV_OBJ_FLAG_HIDDEN);   // shown once there is a number

    g.name = ui_label(g.arc, &font_vn_14, CL_FAINT, name);
    lv_obj_align(g.name, LV_ALIGN_CENTER, 0, 22);

    // marker for "time passed in this window" on the ring's track
    g.mark = ui_shape(card, 0, 0, MARK_D, MARK_D, LV_RADIUS_CIRCLE, CL_TEXT);
    lv_obj_set_style_border_color(g.mark, CL_CARD, 0);
    lv_obj_set_style_border_width(g.mark, 3, 0);
    lv_obj_add_flag(g.mark, LV_OBJ_FLAG_HIDDEN);

    g.left = ui_label(card, &font_vn_16, CL_TEXT, "");
    lv_obj_set_width(g.left, INNER_W);
    lv_label_set_long_mode(g.left, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g.left, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g.left, 0, LINE1_Y);

    // Full card width: "Làm mới CN 09:00" is ~131 px, more than INNER_W (123)
    g.when = ui_label(card, &font_vn_14, CL_FAINT, "");
    lv_obj_set_size(g.when, CARD_W, lv_font_get_line_height(&font_vn_14));
    lv_label_set_long_mode(g.when, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(g.when, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(g.when, -CARD_PAD, LINE2_Y);
}

lv_obj_t *quota_page_build(lv_obj_t *page, int tz_offset_sec)
{
    g_tz = tz_offset_sec;
    lv_obj_t *body = ui_page_create(page);
    make_gauge(g5, body, UI_MARGIN, "5 GIỜ", WIN_5H_S);
    make_gauge(g7, body, UI_MARGIN + CARD_W + UI_GAP, "7 NGÀY", WIN_7D_S);
    return body;
}

// "2 giờ 15 phút" / "3 ngày 4 giờ" / "45 phút"
static void fmt_left(long s, char *out, size_t n)
{
    long m = (s + 59) / 60;   // round up: "1 phút" until it actually resets
    if (m <= 0)          snprintf(out, n, "Đang làm mới");
    else if (m < 60)     snprintf(out, n, "%ld phút", m);
    else if (m < 1440) {
        if (m % 60) snprintf(out, n, "%ld giờ %ld phút", m / 60, m % 60);
        else        snprintf(out, n, "%ld giờ", m / 60);
    } else {
        long h = (m % 1440) / 60;
        if (h) snprintf(out, n, "%ld ngày %ld giờ", m / 1440, h);
        else   snprintf(out, n, "%ld ngày", m / 1440);
    }
}

// "Làm mới 14:30" today, else "Làm mới T5 09:00"
static void fmt_when(uint32_t reset_at, time_t now, char *out, size_t n)
{
    static const char *WD[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
    time_t r = (time_t)reset_at + g_tz, t = now + g_tz;
    struct tm rt, nt;
    gmtime_r(&r, &rt);
    gmtime_r(&t, &nt);
    if (rt.tm_yday == nt.tm_yday && rt.tm_year == nt.tm_year)
        snprintf(out, n, "Làm mới %02d:%02d", rt.tm_hour, rt.tm_min);
    else
        snprintf(out, n, "Làm mới %s %02d:%02d", WD[rt.tm_wday], rt.tm_hour, rt.tm_min);
}

static void place_mark(gauge &g, float frac)
{
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    float a = (135.0f + 270.0f * frac) * (float)M_PI / 180.0f;
    float r = RING_D / 2.0f - RING_W / 2.0f;
    float cx = RING_X + RING_D / 2.0f, cy = RING_Y + RING_D / 2.0f;
    lv_obj_set_pos(g.mark, (lv_coord_t)lroundf(cx + r * cosf(a) - MARK_D / 2.0f),
                   (lv_coord_t)lroundf(cy + r * sinf(a) - MARK_D / 2.0f));
    lv_obj_clear_flag(g.mark, LV_OBJ_FLAG_HIDDEN);
}

static void draw_time(gauge &g, time_t now)
{
    if (!g.reset_at || now < 1000000000) {
        lv_label_set_text(g.left, "");
        lv_label_set_text(g.when, "");
        lv_obj_add_flag(g.mark, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    long left = (long)g.reset_at - (long)now;
    if (left < 0) left = 0;
    char b[40];
    fmt_left(left, b, sizeof(b));
    lv_label_set_text(g.left, b);
    fmt_when(g.reset_at, now, b, sizeof(b));
    lv_label_set_text(g.when, b);
    place_mark(g, 1.0f - (float)left / (float)g.window_s);
}

static void set_gauge(gauge &g, int pct, uint32_t reset_at, int reset_min, time_t now)
{
    if (pct < 0) return;   // keep the last good values
    if (pct > 100) pct = 100;
    g.used = pct;
    lv_arc_set_value(g.arc, pct);
    lv_obj_set_style_arc_color(g.arc, theme_usage_color(pct), LV_PART_INDICATOR);
    lv_obj_set_style_text_font(g.num, pct >= 100 ? &lv_font_montserrat_28 : &font_temp_42, 0);
    lv_label_set_text_fmt(g.num, "%d", pct);
    lv_obj_clear_flag(g.pct, LV_OBJ_FLAG_HIDDEN);

    if (reset_at) g.reset_at = reset_at;
    else if (reset_min >= 0 && now > 1000000000) g.reset_at = (uint32_t)(now + reset_min * 60L);
    draw_time(g, now);
}

void quota_page_update(const claude_quota_t &q, time_t now_utc)
{
    if (!q.valid) return;
    set_gauge(g5, q.session_pct, q.session_reset_at, q.session_reset_min, now_utc);
    set_gauge(g7, q.week_pct, q.week_reset_at, q.week_reset_min, now_utc);
}

void quota_page_tick(time_t now_utc)
{
    draw_time(g5, now_utc);
    draw_time(g7, now_utc);
}
