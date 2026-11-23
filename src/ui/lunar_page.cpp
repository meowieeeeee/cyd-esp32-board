#include "lunar_page.h"
#include "ui_common.h"
#include "../lunar/lunar.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- Layout (full screen: no shared header) ----
//
//  +-------------+ +----------------------+
//  |THÁNG 11·2026| | (o) 13 tháng 10      |   lunar date + moon phase
//  |     21      | | Ngày        Kỷ Hợi   |   Can Chi of day / month / year
//  |   Thứ Bảy   | | Tháng       Kỷ Hợi   |
//  |             | | Năm        Bính Ngọ  |
//  +-------------+ | Tiết       Lập đông  |   solar term
//                  +----------------------+
//  +--------------------------------------+
//  | Rằm         2 ngày | Mùng 1  18 ngày |
//  |--------------------------------------|
//  | Giáng sinh            33 ngày · 24/12|   next 3 holidays
//  | Tết Dương lịch         41 ngày · 1/1 |
//  | Ông Công Ông Táo      70 ngày · 30/1 |
//  +--------------------------------------+
#define TOP_Y    UI_FULL_TOP_Y                        // 8
#define TOP_H    124
#define SHEET_X  10
#define SHEET_W  136
#define BAND_H   22
#define INFO_X   (SHEET_X + SHEET_W + UI_GAP)         // 152
#define INFO_W   (UI_SCR_W - 10 - INFO_X)             // 158
#define INFO_PAD 12
#define LIST_Y   (TOP_Y + TOP_H + UI_GAP)             // 138
#define LIST_W   (UI_SCR_W - 20)
#define LIST_H   (232 - LIST_Y)                       // 94
#define LIST_PAD 12
#define HOL_N    3
#define HOL_Y    26
#define HOL_STEP 19

#define MOON_D   20
#define MOON_LIT lv_color_hex(0xF2E6B8)
#define C_BAND   lv_color_hex(0xC8463D)               // calendar-sheet red
#define C_SUNDAY lv_color_hex(0xF0625A)

static lv_obj_t *page;
static lv_obj_t *lbl_month, *lbl_day, *lbl_wday;
static lv_obj_t *moon, *moon_shadow, *lbl_lunar;
static lv_obj_t *v_dcc, *v_mcc, *v_ycc, *v_term;
static lv_obj_t *ram_cnt, *mung1_cnt;
static lv_obj_t *hol_name[HOL_N], *hol_when[HOL_N];

static const char *THU[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};

// ---- Moon phase: a lit disc with a dark disc sliding over it, clipped round ----
static void make_moon(lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    moon = ui_shape(parent, x, y, MOON_D, MOON_D, LV_RADIUS_CIRCLE, CL_TRACK);
    lv_obj_set_style_clip_corner(moon, true, 0);
    ui_shape(moon, 0, 0, MOON_D, MOON_D, LV_RADIUS_CIRCLE, MOON_LIT);
    moon_shadow = ui_shape(moon, 0, 0, MOON_D, MOON_D, LV_RADIUS_CIRCLE, CL_TRACK);
}

static void set_moon_phase(int lunar_day)
{
    // 0 = new moon (day 1), 0.5 = full moon (day 15/16), back to 1
    float p = (lunar_day - 1) / 29.53f;
    float lit = (1 - cosf(2 * 3.14159265f * p)) / 2;     // illuminated fraction 0..1
    int shift = (int)lroundf(lit * 2 * MOON_D);         // shadow slides away as it gets fuller
    if (shift > 2 * MOON_D) shift = 2 * MOON_D;
    // waxing (first half): lit on the right -> shadow leaves to the left
    lv_obj_set_x(moon_shadow, p < 0.5f ? -shift : shift);
}

// "key ........ value" line, value right-aligned at `right`
static lv_obj_t *kv_row(lv_obj_t *card, lv_coord_t x, lv_coord_t right, lv_coord_t y, const char *key,
                        const lv_font_t *font, lv_color_t kc)
{
    lv_obj_t *k = ui_label(card, font, kc, key);
    lv_obj_set_pos(k, x, y);
    lv_obj_t *v = ui_label(card, font, CL_DIM, "");
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_width(v, 100);
    lv_obj_set_pos(v, right - 100, y);
    return v;
}

// Shrinks to font_vn_14 when the text doesn't fit in w at font_vn_16.
static void set_text_fit(lv_obj_t *l, const char *t, lv_coord_t w)
{
    const lv_font_t *f = lv_text_get_width(t, strlen(t), &font_vn_16, 0) <= w ? &font_vn_16 : &font_vn_14;
    lv_obj_set_style_text_font(l, f, 0);
    lv_label_set_text(l, t);
}

lv_obj_t *lunar_page_create(lv_obj_t *parent)
{
    page = ui_page_create(parent);

    // ---- Calendar sheet: month band, big solar day, weekday ----
    lv_obj_t *sheet = ui_card(page, SHEET_X, TOP_Y, SHEET_W, TOP_H, 0, 0);
    lv_obj_set_style_clip_corner(sheet, true, 0);
    lv_obj_t *band = ui_shape(sheet, 0, 0, SHEET_W, BAND_H, 0, C_BAND);
    lbl_month = ui_label(band, &font_vn_14, CL_TEXT, "");
    lv_obj_center(lbl_month);
    lbl_day = ui_label(sheet, &font_num_72, CL_TEXT, "--");
    lv_obj_align(lbl_day, LV_ALIGN_TOP_MID, 0, BAND_H + 5);
    lbl_wday = ui_label(sheet, &font_vn_16, CL_DIM, "");
    lv_obj_align(lbl_wday, LV_ALIGN_TOP_MID, 0, 98);

    // ---- Lunar date, Can Chi, solar term ----
    lv_obj_t *info = ui_card(page, INFO_X, TOP_Y, INFO_W, TOP_H, INFO_PAD, 10);
    const lv_coord_t iw = INFO_W - 2 * INFO_PAD;
    make_moon(info, 0, 0);
    lbl_lunar = ui_label(info, &font_vn_16, CL_ACCENT, "");
    lv_obj_set_pos(lbl_lunar, MOON_D + 6, 0);
    lv_obj_set_width(lbl_lunar, iw - MOON_D - 6);
    lv_label_set_long_mode(lbl_lunar, LV_LABEL_LONG_CLIP);
    v_dcc = kv_row(info, 0, iw, 28, "Ngày", &font_vn_14, CL_FAINT);
    v_mcc = kv_row(info, 0, iw, 46, "Tháng", &font_vn_14, CL_FAINT);
    v_ycc = kv_row(info, 0, iw, 64, "Năm", &font_vn_14, CL_FAINT);
    v_term = kv_row(info, 0, iw, 84, "Tiết", &font_vn_14, CL_FAINT);

    // ---- Rằm / Mùng 1, then the next holidays ----
    lv_obj_t *list = ui_card(page, SHEET_X, LIST_Y, LIST_W, LIST_H, LIST_PAD, 6);
    const lv_coord_t lw = LIST_W - 2 * LIST_PAD;
    const lv_coord_t half = (lw - 16) / 2;
    ram_cnt = kv_row(list, 0, half, 0, "Rằm", &font_vn_16, CL_TEXT);
    ui_shape(list, half + 7, 3, 1, 14, 0, CL_TRACK);
    mung1_cnt = kv_row(list, half + 16, lw, 0, "Mùng 1", &font_vn_16, CL_TEXT);
    ui_shape(list, 0, 22, lw, 1, 0, CL_TRACK);
    for (int i = 0; i < HOL_N; i++) {
        lv_coord_t y = HOL_Y + i * HOL_STEP;
        hol_name[i] = ui_label(list, &font_vn_14, CL_TEXT, "");
        lv_obj_set_pos(hol_name[i], 0, y);
        hol_when[i] = ui_label(list, &font_vn_14, CL_FAINT, "");
        lv_obj_align(hol_when[i], LV_ALIGN_TOP_RIGHT, 0, y);
    }
    return page;
}

static void fmt_count(long days, char *b, size_t n)
{
    if (days <= 0) snprintf(b, n, "Hôm nay");
    else if (days == 1) snprintf(b, n, "Ngày mai");
    else snprintf(b, n, "%ld ngày", days);
}

static void set_count(lv_obj_t *l, long days)
{
    char b[24];
    fmt_count(days, b, sizeof(b));
    lv_label_set_text(l, b);
    lv_obj_set_style_text_color(l, days <= 1 ? CL_ACCENT : CL_DIM, 0);
}

void lunar_page_update(int dd, int mm, int yy)
{
    char b[64], cc[32];
    lunar_date_t L = lunar_from_solar(dd, mm, yy);
    lunar_upcoming_t up;
    lunar_upcoming(L.jd, up);
    int wd = weekday_from_jd(L.jd);

    // sheet: solar date (red on Sundays and holidays, like a printed calendar)
    snprintf(b, sizeof(b), "THÁNG %d \xC2\xB7 %d", mm, yy);
    lv_label_set_text(lbl_month, b);
    lv_label_set_text_fmt(lbl_day, "%d", dd);
    lv_obj_set_style_text_color(lbl_day, wd == 0 || up.today_holiday ? C_SUNDAY : CL_TEXT, 0);
    lv_label_set_text(lbl_wday, THU[wd]);
    lv_obj_set_style_text_color(lbl_wday, wd == 0 ? C_SUNDAY : CL_DIM, 0);

    // lunar date
    const char *mname = L.month == 1 ? "Giêng" : L.month == 12 ? "Chạp" : nullptr;
    if (mname) snprintf(b, sizeof(b), "%d tháng %s", L.day, mname);
    else snprintf(b, sizeof(b), "%d tháng %d", L.day, L.month);
    if (L.leap) snprintf(b + strlen(b), sizeof(b) - strlen(b), " nhuận");
    set_text_fit(lbl_lunar, b, lv_obj_get_style_width(lbl_lunar, 0));
    set_moon_phase(L.day);

    lunar_day_canchi(L.jd, cc, sizeof(cc));
    lv_label_set_text(v_dcc, cc);
    lunar_month_canchi(L.month, L.year, cc, sizeof(cc));
    lv_label_set_text(v_mcc, cc);
    lunar_year_canchi(L.year, cc, sizeof(cc));
    lv_label_set_text(v_ycc, cc);
    lv_label_set_text(v_term, lunar_solar_term(L.jd));

    // Rằm / Mùng 1
    set_count(ram_cnt, up.ram.jd - L.jd);
    set_count(mung1_cnt, up.mung1.jd - L.jd);

    // next holidays: name ...... "33 ngày · 24/12" (today's in accent)
    for (int i = 0; i < HOL_N; i++) {
        if (i >= up.n_holidays) {
            lv_label_set_text(hol_name[i], "");
            lv_label_set_text(hol_when[i], "");
            continue;
        }
        const lunar_event_t &e = up.holidays[i];
        long days = e.jd - L.jd;
        int d, m, y;
        solar_from_jd(e.jd, d, m, y);
        lv_label_set_text(hol_name[i], e.name);
        char cnt[24];
        fmt_count(days, cnt, sizeof(cnt));
        if (days <= 0) snprintf(b, sizeof(b), "%s", cnt);
        else snprintf(b, sizeof(b), "%s \xC2\xB7 %d/%d", cnt, d, m);
        lv_label_set_text(hol_when[i], b);
        lv_color_t c = days <= 1 ? CL_ACCENT : CL_FAINT;
        lv_obj_set_style_text_color(hol_when[i], c, 0);
        lv_obj_set_style_text_color(hol_name[i], days <= 0 ? CL_ACCENT : CL_TEXT, 0);
    }
}
