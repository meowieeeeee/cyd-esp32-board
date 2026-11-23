#include "weather_page.h"
#include "ui_common.h"
#include "../lunar/lunar.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Full-screen layout (no page icon / dots): date + AQI, big clock, current
// conditions on the right, lunar date + city, 3 detail pills, 4 day cards.
#define CLOCK_Y    40
#define RIGHT_X    218            // current-conditions column
#define RIGHT_W    96
#define PILL_Y     151
#define PILL_H     23
#define PILL_W     101
#define PILL_STEP  106
#define DAY_N      4              // forecast cards (today + 3)
#define DAY_Y      181
#define DAY_H      57
#define DAY_W      74
#define DAY_STEP   80
#define EDGE_X     2

#define C_LUNAR    lv_color_hex(0xF2C14E)
#define C_RAINFILL lv_color_hex(0x294462)
#define C_RAINTXT  lv_color_hex(0x93A6B8)

// ---- Icon colors ----
#define IC_SUN    lv_color_hex(0xF2C14E)
#define IC_MOON   lv_color_hex(0xE8E1C9)
#define IC_CLOUD  lv_color_hex(0xC9C5D1)
#define IC_DARK   lv_color_hex(0x8E8A99)
#define IC_RAIN   lv_color_hex(0x5FA8E8)
#define IC_SNOW   lv_color_hex(0xFBFAF7)
#define IC_BOLT   lv_color_hex(0xF2C14E)

enum icon_kind { IK_NONE, IK_CLEAR, IK_PARTLY, IK_CLOUDY, IK_FOG, IK_DRIZZLE, IK_RAIN,
                 IK_SNOW, IK_STORM };

static icon_kind kind_for(int code)
{
    if (code < 0) return IK_NONE;
    if (code == 0) return IK_CLEAR;
    if (code <= 2) return IK_PARTLY;
    if (code == 3) return IK_CLOUDY;
    if (code == 45 || code == 48) return IK_FOG;
    if (code >= 51 && code <= 57) return IK_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return IK_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86) return IK_SNOW;
    if (code >= 95) return IK_STORM;
    return IK_CLOUDY;
}

// One icon = a fixed-size box whose children are redrawn on every update.
struct icon_slot {
    lv_obj_t *box;
    lv_coord_t w, h;
    lv_color_t bg;              // what's behind the icon (cuts the crescent moon)
    lv_point_precise_t bolt[4]; // lv_line keeps a pointer to its points -> must outlive it
};

static void cloud(lv_obj_t *b, float x, float y, float w, float h, lv_color_t c)
{
    // base + two puffs
    ui_shape(b, x, y + h * 0.45f, w, h * 0.55f, LV_RADIUS_CIRCLE, c);
    float d1 = h * 0.85f, d2 = h * 0.6f;
    ui_shape(b, x + w * 0.38f, y, d1, d1, LV_RADIUS_CIRCLE, c);
    ui_shape(b, x + w * 0.12f, y + h * 0.22f, d2, d2, LV_RADIUS_CIRCLE, c);
}

static void sun_or_moon(lv_obj_t *b, float x, float y, float d, bool day, lv_color_t bg)
{
    if (day) {
        ui_shape(b, x, y, d, d, LV_RADIUS_CIRCLE, IC_SUN);
    } else { // crescent: moon disc with a background-colored disc over it
        ui_shape(b, x, y, d, d, LV_RADIUS_CIRCLE, IC_MOON);
        ui_shape(b, x + d * 0.35f, y - d * 0.15f, d * 0.85f, d * 0.85f, LV_RADIUS_CIRCLE, bg);
    }
}

static void drops(lv_obj_t *b, float W, float H, int n, lv_color_t c, bool dots)
{
    float y = H * 0.70f;
    for (int i = 0; i < n; i++) {
        float x = W * (n == 2 ? 0.35f + 0.3f * i : 0.25f + 0.25f * i);
        if (dots) {
            float d = W * 0.11f;
            ui_shape(b, x - d / 2, y + H * 0.05f, d, d, LV_RADIUS_CIRCLE, c);
        } else {
            float dw = W >= 45 ? 3 : 2;
            ui_shape(b, x - dw / 2, y, dw, H * 0.28f, 1, c);
        }
    }
}

static void draw_icon(icon_slot &s, int code, bool day)
{
    lv_obj_t *b = s.box;
    lv_obj_clean(b);
    const float W = s.w, H = s.h;

    switch (kind_for(code)) {
    case IK_NONE:
        break;
    case IK_CLEAR: {
        float d = H * 0.75f;
        sun_or_moon(b, (W - d) / 2, (H - d) / 2, d, day, s.bg);
        break;
    }
    case IK_PARTLY:
        sun_or_moon(b, W * 0.08f, 0, H * 0.58f, day, s.bg);
        cloud(b, W * 0.25f, H * 0.32f, W * 0.75f, H * 0.66f, IC_CLOUD);
        break;
    case IK_CLOUDY:
        cloud(b, W * 0.02f, H * 0.15f, W * 0.96f, H * 0.8f, IC_CLOUD);
        break;
    case IK_FOG:
        for (int i = 0; i < 3; i++) {
            float lw = W * (i == 1 ? 0.65f : 0.85f);
            ui_shape(b, (W - lw) / 2 + (i == 1 ? W * 0.08f : 0), H * (0.2f + 0.27f * i),
                     lw, H * 0.12f, LV_RADIUS_CIRCLE, IC_DARK);
        }
        break;
    case IK_DRIZZLE:
        cloud(b, W * 0.05f, 0, W * 0.9f, H * 0.62f, IC_CLOUD);
        drops(b, W, H, 2, IC_RAIN, false);
        break;
    case IK_RAIN:
        cloud(b, W * 0.05f, 0, W * 0.9f, H * 0.62f, IC_DARK);
        drops(b, W, H, 3, IC_RAIN, false);
        break;
    case IK_SNOW:
        cloud(b, W * 0.05f, 0, W * 0.9f, H * 0.62f, IC_CLOUD);
        drops(b, W, H, 3, IC_SNOW, true);
        break;
    case IK_STORM: {
        cloud(b, W * 0.05f, 0, W * 0.9f, H * 0.62f, IC_DARK);
        const float pts[4][2] = {{0.56f, 0.55f}, {0.42f, 0.78f}, {0.57f, 0.78f}, {0.45f, 0.99f}};
        for (int i = 0; i < 4; i++) {
            s.bolt[i].x = (lv_value_precise_t)(W * pts[i][0]);
            s.bolt[i].y = (lv_value_precise_t)(H * pts[i][1]);
        }
        lv_obj_t *l = lv_line_create(b);
        lv_line_set_points(l, s.bolt, 4);
        lv_obj_set_style_line_color(l, IC_BOLT, 0);
        lv_obj_set_style_line_width(l, W >= 45 ? 3 : 2, 0);
        lv_obj_set_style_line_rounded(l, true, 0);
        break;
    }
    }
}

static void icon_init(icon_slot &s, lv_obj_t *parent, lv_coord_t w, lv_coord_t h, lv_color_t bg)
{
    s.w = w;
    s.h = h;
    s.bg = bg;
    s.box = lv_obj_create(parent);
    lv_obj_remove_style_all(s.box);
    lv_obj_set_size(s.box, w, h);
    lv_obj_clear_flag(s.box, LV_OBJ_FLAG_SCROLLABLE);
}

// ---- Widgets ----
struct day_card {
    lv_obj_t *card, *fill, *name, *temp, *drop, *rain;
    icon_slot icon;
    lv_point_precise_t drop_pts[3];
};

static lv_obj_t *page;
static lv_obj_t *lbl_date, *aqi_pill, *lbl_aqi, *lbl_clock, *lbl_sec;
static lv_obj_t *cur_temp, *cur_text, *lbl_lunar, *lbl_city, *lbl_status;
static lv_obj_t *pill_lbl[3];
static icon_slot cur_icon;
static day_card days[DAY_N];

static const char *WDAY[7] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

// Rain-drop mark: a dot with a pointed top (two short lines).
static lv_obj_t *make_drop(day_card &c, lv_obj_t *parent, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, 9, 12);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    ui_shape(box, 0, 4, 9, 8, LV_RADIUS_CIRCLE, IC_RAIN);
    c.drop_pts[0] = {1, 7};
    c.drop_pts[1] = {(lv_value_precise_t)4.5f, 0};
    c.drop_pts[2] = {8, 7};
    lv_obj_t *l = lv_line_create(box);
    lv_line_set_points(l, c.drop_pts, 3);
    lv_obj_set_style_line_color(l, IC_RAIN, 0);
    lv_obj_set_style_line_width(l, 3, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    return box;
}

lv_obj_t *weather_page_create(lv_obj_t *parent)
{
    page = ui_page_create(parent);

    // ---- Top row: date, AQI pill ----
    lbl_date = ui_label(page, &font_vn_16, CL_TEXT, "");
    lv_obj_set_pos(lbl_date, 6, 9);

    aqi_pill = lv_obj_create(page);
    lv_obj_remove_style_all(aqi_pill);
    lv_obj_set_size(aqi_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(aqi_pill, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(aqi_pill, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(aqi_pill, 8, 0);
    lv_obj_set_style_pad_ver(aqi_pill, 2, 0);
    lv_obj_clear_flag(aqi_pill, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(aqi_pill, LV_ALIGN_TOP_RIGHT, -5, 6);
    lbl_aqi = ui_label(aqi_pill, &font_vn_14, lv_color_hex(0x1A1A1A), "");
    lv_obj_add_flag(aqi_pill, LV_OBJ_FLAG_HIDDEN);

    // ---- Big clock + seconds ----
    lbl_clock = ui_label(page, &font_clock_72, CL_TEXT, "--:--");
    lv_obj_set_pos(lbl_clock, 1, CLOCK_Y);
    lbl_sec = ui_label(page, &font_clock_20, CL_FAINT, "");
    lv_obj_set_pos(lbl_sec, 184, CLOCK_Y + 23);

    // ---- Current conditions (right column) ----
    icon_init(cur_icon, page, 42, 26, CL_BG);
    lv_obj_set_pos(cur_icon.box, 262, 30);

    cur_temp = ui_label(page, &font_temp_42, CL_TEXT, "--");
    lv_obj_set_width(cur_temp, RIGHT_W);
    lv_obj_set_style_text_align(cur_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cur_temp, RIGHT_X, 55);

    cur_text = ui_label(page, &font_vn_14, CL_FAINT, "");
    lv_obj_set_width(cur_text, RIGHT_W + 4);
    lv_label_set_long_mode(cur_text, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(cur_text, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(cur_text, RIGHT_X - 2, 96);

    // ---- Lunar date, city / status ----
    lbl_lunar = ui_label(page, &font_vn_14, C_LUNAR, "");
    lv_obj_set_pos(lbl_lunar, 8, 115);

    lbl_city = ui_label(page, &font_vn_14, CL_FAINT, "");
    lv_obj_set_pos(lbl_city, 8, 133);

    lbl_status = ui_label(page, &font_vn_14, CL_FAINT, "");
    lv_obj_set_width(lbl_status, 180);
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(lbl_status, UI_SCR_W - 6 - 180, 133);

    // ---- Feels like / humidity / wind ----
    for (int i = 0; i < 3; i++) {
        lv_obj_t *p = ui_card(page, EDGE_X + i * PILL_STEP, PILL_Y, PILL_W, PILL_H, 0, 0);
        lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
        pill_lbl[i] = ui_label(p, &font_vn_14, CL_DIM, "--");
        lv_obj_center(pill_lbl[i]);
    }

    // ---- 4-day forecast ----
    for (int i = 0; i < DAY_N; i++) {
        day_card &c = days[i];
        c.card = ui_card(page, EDGE_X + i * DAY_STEP, DAY_Y, DAY_W, DAY_H, 0, 0);
        lv_obj_set_style_radius(c.card, 10, 0);
        lv_obj_set_style_clip_corner(c.card, true, 0);

        // chance of rain = blue fill rising from the bottom
        c.fill = ui_shape(c.card, 0, DAY_H, DAY_W, 0, 0, C_RAINFILL);

        c.name = ui_label(c.card, &font_vn_14, CL_FAINT, i == 0 ? "Nay" : "--");
        lv_obj_set_pos(c.name, 6, 3);

        icon_init(c.icon, c.card, 25, 17, CL_CARD);
        lv_obj_set_pos(c.icon.box, DAY_W - 25 - 5, 3);

        c.temp = ui_label(c.card, &font_vn_16, CL_TEXT, "--");
        lv_obj_set_pos(c.temp, 6, 19);

        c.drop = make_drop(c, c.card, 6, 41);
        c.rain = ui_label(c.card, &font_vn_14, C_RAINTXT, "");
        lv_obj_set_pos(c.rain, 18, 39);
    }

    return page;
}

// ---- Clock / calendar (called every second while the page is shown) ----
void weather_page_tick(time_t local)
{
    struct tm tm;
    gmtime_r(&local, &tm);
    char b[48];

    static int last_sec = -1, last_min = -1, last_yday = -1;
    if (tm.tm_sec != last_sec) {
        last_sec = tm.tm_sec;
        snprintf(b, sizeof(b), "%02d", tm.tm_sec);
        lv_label_set_text(lbl_sec, b);
    }
    if (tm.tm_min != last_min) {
        last_min = tm.tm_min;
        snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min);
        lv_label_set_text(lbl_clock, b);
        // seconds follow the clock's width ("11:11" is narrower than "08:08")
        lv_obj_update_layout(lbl_clock);
        lv_obj_set_x(lbl_sec, lv_obj_get_x(lbl_clock) + lv_obj_get_width(lbl_clock) + 4);
    }
    if (tm.tm_yday != last_yday) {
        last_yday = tm.tm_yday;
        int d = tm.tm_mday, m = tm.tm_mon + 1, y = tm.tm_year + 1900;
        if (tm.tm_wday == 0) snprintf(b, sizeof(b), "Chủ nhật, %02d/%02d/%d", d, m, y);
        else snprintf(b, sizeof(b), "Thứ %d, %02d/%02d/%d", tm.tm_wday + 1, d, m, y);
        lv_label_set_text(lbl_date, b);

        lunar_date_t l = lunar_from_solar(d, m, y);
        char cc[24];
        lunar_day_canchi(l.jd, cc, sizeof(cc));
        snprintf(b, sizeof(b), "%d/%d%s âm lịch \xC2\xB7 %s", l.day, l.month, l.leap ? "n" : "", cc);
        lv_label_set_text(lbl_lunar, b);
    }
}

static void fmt_deg(float v, char *b, size_t n)
{
    if (isnan(v)) snprintf(b, n, "--");
    else snprintf(b, n, "%d\xC2\xB0", (int)lroundf(v));
}

void weather_page_update(const weather_t &w)
{
    char b[48];

    if (w.aqi >= 0) {
        snprintf(b, sizeof(b), "AQI %d \xC2\xB7 %s", w.aqi, weather_aqi_text(w.aqi));
        lv_label_set_text(lbl_aqi, b);
        lv_color_t bg = lv_color_hex(weather_aqi_color(w.aqi));
        lv_obj_set_style_bg_color(aqi_pill, bg, 0);
        // dark text on the light colors (green/yellow/orange), white on the dark ones
        lv_obj_set_style_text_color(lbl_aqi, w.aqi <= 150 ? lv_color_hex(0x1A1A1A) : CL_TEXT, 0);
        lv_obj_clear_flag(aqi_pill, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(aqi_pill, LV_OBJ_FLAG_HIDDEN);
    }

    if (w.valid) {
        fmt_deg(w.temp_c, b, sizeof(b));
        lv_label_set_text(cur_temp, b);
        lv_label_set_text(cur_text, weather_code_text(w.code));
        draw_icon(cur_icon, w.code, w.is_day);

        if (isnan(w.feels_c)) snprintf(b, sizeof(b), "Cảm giác --");
        else snprintf(b, sizeof(b), "Cảm giác %d\xC2\xB0", (int)lroundf(w.feels_c));
        lv_label_set_text(pill_lbl[0], b);
        if (w.humidity >= 0) snprintf(b, sizeof(b), "Ẩm %d%%", w.humidity);
        else snprintf(b, sizeof(b), "Ẩm --");
        lv_label_set_text(pill_lbl[1], b);
        if (!isnan(w.wind_kmh)) snprintf(b, sizeof(b), "Gió %d km/h", (int)lroundf(w.wind_kmh));
        else snprintf(b, sizeof(b), "Gió --");
        lv_label_set_text(pill_lbl[2], b);
    }

    if (w.forecast_valid) {
        for (int i = 0; i < DAY_N; i++) {
            const weather_day_t &d = w.days[i];
            day_card &c = days[i];
            if (i > 0) lv_label_set_text(c.name, d.wday >= 0 ? WDAY[d.wday] : "--");

            if (isnan(d.tmin) || isnan(d.tmax)) snprintf(b, sizeof(b), "--");
            else snprintf(b, sizeof(b), "%d\xE2\x80\x93%d\xC2\xB0", (int)lroundf(d.tmin), (int)lroundf(d.tmax));
            lv_label_set_text(c.temp, b);

            int pct = d.rain_pct;
            int fh = pct > 0 ? (DAY_H * (pct > 100 ? 100 : pct) + 50) / 100 : 0;
            lv_obj_set_pos(c.fill, 0, DAY_H - fh);
            lv_obj_set_height(c.fill, fh);
            if (pct < 0) {
                lv_label_set_text(c.rain, "");
                lv_obj_add_flag(c.drop, LV_OBJ_FLAG_HIDDEN);
            } else if (pct < 10) {  // no drop, just "dry"
                lv_label_set_text(c.rain, "Khô ráo");
                lv_obj_set_x(c.rain, 6);
                lv_obj_set_style_text_color(c.rain, CL_FAINT, 0);
                lv_obj_add_flag(c.drop, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_label_set_text_fmt(c.rain, "%d%%", pct);
                lv_obj_set_x(c.rain, 18);
                // rainy days stand out in white
                lv_obj_set_style_text_color(c.rain, pct >= 40 ? CL_TEXT : C_RAINTXT, 0);
                lv_obj_clear_flag(c.drop, LV_OBJ_FLAG_HIDDEN);
            }
            draw_icon(c.icon, d.code, true);
        }
    }
}

void weather_page_set_city(const char *city) { lv_label_set_text(lbl_city, city ? city : ""); }

void weather_page_set_status(const char *msg, bool accent)
{
    lv_label_set_text(lbl_status, msg ? msg : "");
    lv_obj_set_style_text_color(lbl_status, accent ? CL_ACCENT : CL_FAINT, 0);
}
