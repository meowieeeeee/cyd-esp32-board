#include "tet_page.h"
#include "ui_common.h"
#include "../lunar/lunar.h"
#include <stdio.h>

#define MAI       lv_color_hex(0xF6C945)   // apricot blossom yellow
#define MAI_HEART lv_color_hex(0xD97757)
#define RED_TET   lv_color_hex(0xE0563B)

#define JD_UNIX_EPOCH 2440588L             // jd_from_solar(1, 1, 1970)

static lv_obj_t *page, *lbl_status;
static lv_obj_t *count_box, *greet_box;
static lv_obj_t *l_title, *l_days, *l_unit;
static lv_obj_t *g_title, *g_year, *g_day;

static const char *THU[7] = {"Chủ Nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư", "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};

static void make_blossom()
{
    lv_obj_t *box = ui_header_box(page, 32, 32);
    // 5 petals around the center
    static const int8_t px[5] = {11, 21, 17, 5, 1};
    static const int8_t py[5] = {0, 7, 19, 19, 7};
    for (int i = 0; i < 5; i++) ui_shape(box, px[i], py[i], 11, 11, LV_RADIUS_CIRCLE, MAI);
    ui_shape(box, 12, 12, 8, 8, LV_RADIUS_CIRCLE, MAI_HEART);
}

lv_obj_t *tet_page_create(lv_obj_t *parent)
{
    page = ui_page_create(parent);
    make_blossom();

    lv_obj_t *card = ui_card(page, UI_MARGIN, UI_TOP_Y, UI_SCR_W - 2 * UI_MARGIN, UI_AREA_H, 12, 10);

    // ---- countdown ----
    count_box = ui_page_create(card);
    l_title = ui_label(count_box, &font_vn_22, RED_TET, "");
    lv_obj_align(l_title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *row = lv_obj_create(count_box);   // "77 ngày" centered as a group
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, 18);
    l_days = ui_label(row, &font_num_72, CL_ACCENT, "--");
    l_unit = ui_label(row, &font_vn_22, CL_DIM, "ngày");
    lv_obj_set_style_pad_bottom(l_unit, 14, 0);

    // ---- greeting (Mung 1-3) ----
    greet_box = ui_page_create(card);
    g_title = ui_label(greet_box, &font_vn_22, RED_TET, "Chúc Mừng Năm Mới!");
    lv_obj_align(g_title, LV_ALIGN_TOP_MID, 0, 14);
    g_year = ui_label(greet_box, &font_vn_22, MAI, "");
    lv_obj_align(g_year, LV_ALIGN_CENTER, 0, 0);
    g_day = ui_label(greet_box, &font_vn_16, CL_DIM, "");
    lv_obj_align(g_day, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_obj_add_flag(greet_box, LV_OBJ_FLAG_HIDDEN);

    lbl_status = ui_status_label(page);
    return page;
}

void tet_page_update(time_t now_local, long tet_jd, int ly)
{
    if (!tet_jd) return;
    char cc[24], b[64];
    lunar_year_canchi(ly, cc, sizeof(cc));

    long today_jd = (long)(now_local / 86400) + JD_UNIX_EPOCH;
    long into = today_jd - tet_jd; // 0..2 during Mung 1-3

    if (into >= 0 && into <= 2) {
        lv_obj_add_flag(count_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(greet_box, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(g_year, "Xuân %s %d", cc, ly);
        lv_label_set_text_fmt(g_day, "Mùng %ld Tết", into + 1);
        ui_set_status(lbl_status, "An khang thịnh vượng!", true);
        return;
    }

    lv_obj_clear_flag(count_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(greet_box, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(l_title, "Tết %s %d", cc, ly);

    time_t target = (time_t)(tet_jd - JD_UNIX_EPOCH) * 86400; // 00:00 Mung 1, local time
    long left = (long)(target - now_local);
    if (left < 0) left = 0;
    lv_label_set_text_fmt(l_days, "%ld", left / 86400);

    int d, m, y;
    solar_from_jd(tet_jd, d, m, y);
    snprintf(b, sizeof(b), "Mùng 1 Tết: %s, %d/%d/%d", THU[weekday_from_jd(tet_jd)], d, m, y);
    ui_set_status(lbl_status, b, false);
}
