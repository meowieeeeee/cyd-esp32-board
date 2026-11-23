#pragma once
// Small helpers shared by the page UIs (PC, weather, Wake-on-LAN).
#include <lvgl.h>
#include "../theme.h"

// Same frame as the quota page: content between y=46 and y=220,
// status line at the bottom.
#define UI_SCR_W   320
#define UI_MARGIN  14
#define UI_TOP_Y   46
#define UI_AREA_H  174
#define UI_GAP     6
// Full-screen pages (no shared header: page icon, clock, temperature, dots)
#define UI_FULL_TOP_Y  8
#define UI_FULL_AREA_H (UI_TOP_Y + UI_AREA_H - UI_FULL_TOP_Y)   // 212, ends at y 220 like UI_AREA_H

static inline lv_obj_t *ui_page_create(lv_obj_t *parent)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_remove_style_all(p);
    lv_obj_set_size(p, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static inline lv_obj_t *ui_card(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                                lv_coord_t h, lv_coord_t pad_x, lv_coord_t pad_y)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_bg_color(c, CL_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_hor(c, pad_x, 0);
    lv_obj_set_style_pad_ver(c, pad_y, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

static inline lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color,
                                 const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

// Plain filled shape (circle when radius = LV_RADIUS_CIRCLE)
static inline lv_obj_t *ui_shape(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w,
                                 lv_coord_t h, lv_coord_t radius, lv_color_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

// Page icon in the shared header: top-right, across from the clock (left).
// The Clawd mascot sits here on the quota page.
#define UI_ICON_CX (UI_SCR_W - UI_MARGIN - 20)
#define UI_ICON_CY 20

static inline lv_obj_t *ui_header_box(lv_obj_t *page, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *b = lv_obj_create(page);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, UI_ICON_CX - w / 2, UI_ICON_CY - h / 2);
    lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    return b;
}

// Bottom status line: "• msg" in accent (orange) or faint color.
static inline lv_obj_t *ui_status_label(lv_obj_t *page)
{
    lv_obj_t *l = ui_label(page, &font_vn_16, CL_ACCENT, "");
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -3);
    return l;
}

static inline void ui_set_status(lv_obj_t *l, const char *msg, bool accent)
{
    if (msg && msg[0]) lv_label_set_text_fmt(l, "\xE2\x80\xA2 %s", msg);
    else lv_label_set_text(l, "");
    lv_obj_set_style_text_color(l, accent ? CL_ACCENT : CL_FAINT, 0);
}
