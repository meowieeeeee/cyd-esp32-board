#include "wol_page.h"
#include "ui_common.h"
#include <stdio.h>
#include <string.h>

// ---- Layout ----
#define PC_H     54
#define BTN_Y    (UI_FULL_TOP_Y + PC_H + UI_GAP)
#define BTN_H    (UI_FULL_AREA_H - PC_H - UI_GAP)  // 152 (full screen, no shared header)
#define RING_D   96
#define KNOB_D   70

static lv_obj_t *page;
static lv_obj_t *dot, *lbl_host, *lbl_state, *lbl_mac;
static lv_obj_t *ring, *knob, *knob_icon, *lbl_title, *lbl_hint;

lv_obj_t *wol_panel_create(lv_obj_t *parent)
{
    page = ui_page_create(parent);
    const lv_coord_t card_w = UI_SCR_W - 2 * UI_MARGIN;

    // ---- PC card ----
    lv_obj_t *pc = ui_card(page, UI_MARGIN, UI_FULL_TOP_Y, card_w, PC_H, 14, 8);

    dot = ui_shape(pc, 0, 4, 10, 10, LV_RADIUS_CIRCLE, CL_FAINT);

    lbl_host = ui_label(pc, &font_vn_16, CL_TEXT, "PC");
    lv_obj_set_pos(lbl_host, 18, 0);
    lv_obj_set_width(lbl_host, card_w - 28 - 90);
    lv_label_set_long_mode(lbl_host, LV_LABEL_LONG_DOT);

    lbl_state = ui_label(pc, &font_vn_14, CL_FAINT, "");
    lv_obj_align(lbl_state, LV_ALIGN_TOP_RIGHT, 0, 1);

    lbl_mac = ui_label(pc, &font_vn_14, CL_FAINT, "");
    lv_obj_set_pos(lbl_mac, 18, 21);

    // ---- Power button ----
    lv_obj_t *card = ui_card(page, UI_MARGIN, BTN_Y, card_w, BTN_H, 14, (BTN_H - RING_D) / 2);

    ring = lv_arc_create(card);
    lv_obj_set_size(ring, RING_D, RING_D);
    lv_obj_align(ring, LV_ALIGN_LEFT_MID, 6, 0);
    lv_arc_set_rotation(ring, 270);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_range(ring, 0, 100);
    lv_arc_set_value(ring, 0);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(ring, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ring, 0, 0);
    lv_obj_set_style_arc_width(ring, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, CL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 7, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, CL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(ring, true, LV_PART_INDICATOR);

    knob = ui_shape(ring, 0, 0, KNOB_D, KNOB_D, LV_RADIUS_CIRCLE, CL_PILL);
    lv_obj_center(knob);
    knob_icon = ui_label(knob, &lv_font_montserrat_28, CL_DIM, LV_SYMBOL_POWER);
    lv_obj_center(knob_icon);

    lbl_title = ui_label(card, &font_vn_22, CL_TEXT, "");
    lv_obj_set_pos(lbl_title, RING_D + 24, 12);

    lbl_hint = ui_label(card, &font_vn_14, CL_FAINT, "");
    lv_obj_set_pos(lbl_hint, RING_D + 24, 44);
    lv_obj_set_width(lbl_hint, card_w - 28 - RING_D - 24);
    lv_label_set_long_mode(lbl_hint, LV_LABEL_LONG_WRAP);

    return page;
}

void wol_panel_update(const wol_view_t &v)
{
    char b[64];

    // ---- PC card ----
    bool online = v.state == WOL_ONLINE;
    lv_label_set_text(lbl_host, v.host && v.host[0] ? v.host : "PC");
    lv_obj_set_style_bg_color(dot, online ? CL_GREEN : (v.state == WOL_WAKING ? CL_AMBER : CL_FAINT), 0);
    lv_label_set_text(lbl_state, online ? "ĐANG BẬT" : (v.state == WOL_WAKING ? "ĐANG MỞ" : "ĐANG TẮT"));
    lv_obj_set_style_text_color(lbl_state, online ? CL_GREEN : (v.state == WOL_WAKING ? CL_AMBER : CL_FAINT), 0);
    if (v.mac && v.mac[0]) snprintf(b, sizeof(b), "%s%s", v.mac, v.nic_wifi ? "  (Wi-Fi)" : "");
    else snprintf(b, sizeof(b), "Chưa biết địa chỉ MAC");
    lv_label_set_text(lbl_mac, b);

    // ---- Button ----
    const char *title = "", *hint = "";
    lv_color_t ring_col = CL_ACCENT, icon_col = CL_DIM;
    int ring_val = 0;

    switch (v.state) {
    case WOL_NO_MAC:
        title = "Chưa có MAC";
        hint = "Chạy pc_monitor.py khi PC đang bật";
        icon_col = CL_FAINT;
        break;
    case WOL_OFFLINE:
        title = "Giữ để bật";
        hint = v.nic_wifi ? "PC dùng Wi-Fi: có thể không bật được"
                          : "Giữ màn hình 1 giây để bật máy";
        break;
    case WOL_WAKING:
        title = "Đang bật...";
        snprintf(b, sizeof(b), "Đã gửi lệnh %u giây trước", (unsigned)v.sent_ago_s);
        hint = b;
        ring_val = 100;
        ring_col = CL_AMBER;
        icon_col = CL_AMBER;
        break;
    case WOL_NO_REPLY:
        title = "Chưa bật";
        hint = "PC chưa lên. Xem lại WoL trong BIOS, giữ để thử lại";
        icon_col = CL_RED;
        break;
    case WOL_ONLINE:
        title = "PC đã bật";
        hint = "Giữ để gửi lại lệnh";
        ring_val = 100;
        ring_col = CL_GREEN;
        icon_col = CL_GREEN;
        break;
    }

    // While holding, the ring shows progress instead.
    if (v.hold_pct >= 0 && v.state != WOL_NO_MAC) {
        ring_val = v.hold_pct;
        ring_col = CL_ACCENT;
        icon_col = CL_TEXT;
    }

    lv_label_set_text(lbl_title, title);
    lv_label_set_text(lbl_hint, hint);
    lv_arc_set_value(ring, ring_val);
    lv_obj_set_style_arc_color(ring, ring_col, LV_PART_INDICATOR);
    lv_obj_set_style_text_color(knob_icon, icon_col, 0);
}
