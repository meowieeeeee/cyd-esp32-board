#include "events_page.h"
#include "ui_common.h"
#include "../events/events.h"
#include "../lunar/lunar.h"
#include <stdio.h>

#define TOP_H  100
#define LIST_Y (UI_TOP_Y + TOP_H + UI_GAP)
#define LIST_H (UI_AREA_H - TOP_H - UI_GAP)   // 68
#define LIST_N 3

static lv_obj_t *page, *lbl_status;
static lv_obj_t *t_name, *t_days, *t_unit, *t_today, *t_date, *t_extra;
static lv_obj_t *l_name[LIST_N], *l_days[LIST_N];
static lv_obj_t *icon_num;

static const char *THU[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};

static void make_icon()
{
    lv_obj_t *b = ui_header_box(page, 30, 30);
    lv_obj_t *sheet = ui_shape(b, 1, 2, 28, 27, 4, CL_TEXT);
    lv_obj_set_style_clip_corner(sheet, true, 0);
    ui_shape(sheet, 0, 0, 28, 8, 0, CL_ACCENT);       // red top band
    ui_shape(b, 7, 0, 3, 6, 1, CL_DIM);               // rings
    ui_shape(b, 20, 0, 3, 6, 1, CL_DIM);
    icon_num = ui_label(sheet, &lv_font_montserrat_14, CL_BG, "");
    lv_obj_align(icon_num, LV_ALIGN_CENTER, 0, 4);
}

lv_obj_t *events_page_create(lv_obj_t *parent)
{
    page = ui_page_create(parent);
    make_icon();
    const lv_coord_t w = UI_SCR_W - 2 * UI_MARGIN;

    // ---- nearest event ----
    lv_obj_t *top = ui_card(page, UI_MARGIN, UI_TOP_Y, w, TOP_H, 12, 8);
    t_name = ui_label(top, &font_vn_22, CL_TEXT, "");
    lv_obj_set_width(t_name, w - 24);
    lv_label_set_long_mode(t_name, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(t_name, 0, 0);

    lv_obj_t *row = lv_obj_create(top);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_set_pos(row, 0, 26);
    t_days = ui_label(row, &font_num_52, CL_ACCENT, "");
    t_unit = ui_label(row, &font_vn_16, CL_DIM, "ngày");
    lv_obj_set_style_pad_bottom(t_unit, 10, 0);
    t_today = ui_label(top, &font_vn_22, CL_ACCENT, "Hôm nay!");
    lv_obj_set_pos(t_today, 0, 44);

    t_date = ui_label(top, &font_vn_14, CL_DIM, "");
    lv_obj_align(t_date, LV_ALIGN_BOTTOM_RIGHT, 0, -20);
    t_extra = ui_label(top, &font_vn_14, CL_ACCENT, "");
    lv_obj_align(t_extra, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    // ---- next ones ----
    lv_obj_t *list = ui_card(page, UI_MARGIN, LIST_Y, w, LIST_H, 12, 4);
    for (int i = 0; i < LIST_N; i++) {
        l_name[i] = ui_label(list, &font_vn_14, CL_DIM, "");
        lv_obj_set_width(l_name[i], w - 24 - 80);
        lv_label_set_long_mode(l_name[i], LV_LABEL_LONG_DOT);
        lv_obj_set_pos(l_name[i], 0, i * 20);
        l_days[i] = ui_label(list, &font_vn_14, CL_TEXT, "");
        lv_obj_align(l_days[i], LV_ALIGN_TOP_RIGHT, 0, i * 20);
    }

    lbl_status = ui_status_label(page);
    return page;
}

static void fmt_days(long d, char *b, size_t n)
{
    if (d <= 0) snprintf(b, n, "Hôm nay");
    else if (d == 1) snprintf(b, n, "Ngày mai");
    else snprintf(b, n, "%ld ngày", d);
}

void events_page_update(long today)
{
    event_next_t up[LIST_N + 1];
    int n = events_upcoming(today, up, LIST_N + 1);
    char b[64];

    if (!n) {
        lv_label_set_text(t_name, "Chưa có sự kiện");
        lv_label_set_text(t_days, "");
        lv_obj_add_flag(t_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(t_today, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(t_date, "");
        lv_label_set_text(t_extra, "");
        lv_label_set_text(icon_num, "");
        for (int i = 0; i < LIST_N; i++) { lv_label_set_text(l_name[i], ""); lv_label_set_text(l_days[i], ""); }
        ui_set_status(lbl_status, "Thêm sự kiện ở trang cài đặt (/settings)", true);
        return;
    }

    // ---- nearest ----
    const event_next_t &x = up[0];
    const event_t &e = events_get(x.idx);
    lv_label_set_text(t_name, e.name);
    bool today_ev = x.days == 0;
    if (today_ev) {
        lv_label_set_text(t_days, "");
        lv_obj_add_flag(t_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(t_today, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text_fmt(t_days, "%ld", x.days);
        lv_obj_clear_flag(t_unit, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(t_today, LV_OBJ_FLAG_HIDDEN);
    }
    if (today_ev) lv_label_set_text(icon_num, "!");
    else if (x.days > 99) lv_label_set_text(icon_num, "99+");
    else lv_label_set_text_fmt(icon_num, "%ld", x.days);

    int d, m, y;
    solar_from_jd(x.jd, d, m, y);
    if (e.type == EV_LUNAR)
        snprintf(b, sizeof(b), "%s, %d/%d (âm %d/%d)", THU[weekday_from_jd(x.jd)], d, m, e.day, e.month);
    else
        snprintf(b, sizeof(b), "%s, %d/%d/%d", THU[weekday_from_jd(x.jd)], d, m, y);
    lv_label_set_text(t_date, b);
    if (x.count > 0) lv_label_set_text_fmt(t_extra, "lần thứ %d", x.count);
    else lv_label_set_text(t_extra, "");

    // ---- the next ones ----
    for (int i = 0; i < LIST_N; i++) {
        if (i + 1 < n) {
            lv_label_set_text(l_name[i], events_get(up[i + 1].idx).name);
            fmt_days(up[i + 1].days, b, sizeof(b));
            lv_label_set_text(l_days[i], b);
        } else {
            lv_label_set_text(l_name[i], i == 0 && n == 1 ? "Không còn sự kiện nào khác" : "");
            lv_label_set_text(l_days[i], "");
        }
    }

    snprintf(b, sizeof(b), "%d sự kiện \xC2\xB7 sửa ở trang cài đặt", events_count());
    ui_set_status(lbl_status, today_ev ? "Hôm nay có sự kiện!" : b, today_ev);
}
