#include "touchcal.h"
#include "ui_common.h"

static lv_obj_t *ov, *title, *hint, *target, *arrow;

void touchcal_create(lv_obj_t *parent)
{
    ov = lv_obj_create(parent);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(ov, CL_BG, 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    title = ui_label(ov, &font_vn_22, CL_TEXT, "");
    lv_obj_set_width(title, 200);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(title, LV_LABEL_LONG_WRAP);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -30);

    hint = ui_label(ov, &font_vn_14, CL_FAINT, "Nhấn nút BOOT để bỏ qua");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    target = ui_shape(ov, 0, 0, 56, 56, LV_RADIUS_CIRCLE, CL_ACCENT);
    lv_obj_set_style_bg_opa(target, LV_OPA_30, 0);
    lv_obj_set_style_border_color(target, CL_ACCENT, 0);
    lv_obj_set_style_border_width(target, 3, 0);
    arrow = ui_label(target, &lv_font_montserrat_28, CL_TEXT, "");
    lv_obj_center(arrow);

    lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN);
}

void touchcal_show(touchcal_step_t step)
{
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ov);
    lv_obj_clear_flag(target, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(hint, LV_OBJ_FLAG_HIDDEN);

    switch (step) {
    case TC_RIGHT:
    case TC_RETRY:
        lv_label_set_text(title, step == TC_RETRY ? "Chưa rõ, thử lại: chạm vào bên PHẢI"
                                                  : "Chạm vào bên PHẢI màn hình");
        lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
        lv_obj_align(target, LV_ALIGN_RIGHT_MID, -12, 40);
        break;
    case TC_LEFT:
        lv_label_set_text(title, "Giờ chạm vào bên TRÁI");
        lv_label_set_text(arrow, LV_SYMBOL_LEFT);
        lv_obj_align(target, LV_ALIGN_LEFT_MID, 12, 40);
        break;
    case TC_DONE:
        lv_label_set_text(title, "Xong! Chạm trái để lùi trang, chạm phải để sang trang");
        lv_obj_add_flag(target, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hint, LV_OBJ_FLAG_HIDDEN);
        break;
    }
}

void touchcal_hide() { lv_obj_add_flag(ov, LV_OBJ_FLAG_HIDDEN); }
