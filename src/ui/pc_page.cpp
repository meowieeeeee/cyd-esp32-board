#include "pc_page.h"
#include "../theme.h"
#include "ui_common.h"
#include "wol_page.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// ---- Layout (320x240, full screen: no shared header, content y = 8..220) ----
//
//  +----------------+ +----------------+
//  | Ryzen 7 5700X  | | RTX 3060       |   chip name (from pc_monitor.py)
//  |    ( 37% )     | |    ( 12% )     |   load gauge, clock inside
//  |   4.45 GHz     | |   1.78 GHz     |
//  | 58°C      65 W | | 46°C     42 W  |   temperature, power
//  +----------------+ +----------------+
//  | RAM  [=====---]  44%   14.2/32 GB  |
//  | VRAM [==------]  39%    3.1/12 GB  |
//  +------------------------------------+
//   • DESKTOP-KHANH                          status line
#define TOP_Y     UI_FULL_TOP_Y
#define CHIP_W    ((UI_SCR_W - 2 * UI_MARGIN - UI_GAP) / 2)   // 143
#define CHIP_H    146
#define CHIP_PAD  10
#define ARC_D     92
#define ARC_Y     18
#define MEM_Y     (TOP_Y + CHIP_H + UI_GAP)                    // 160
#define MEM_H     (UI_FULL_TOP_Y + UI_FULL_AREA_H - MEM_Y)     // 60
#define MEM_W     (UI_SCR_W - 2 * UI_MARGIN)
#define MEM_PAD_X 12
#define MEM_PAD_Y 9
#define MEM_ROW   24

struct chip_card {
    lv_obj_t *name, *arc, *pct, *clock, *temp, *power;
    const char *fallback;   // "CPU" / "GPU" until the script sends the model
};

struct mem_row {
    lv_obj_t *bar, *pct, *detail;
};

static lv_obj_t *page;
static lv_obj_t *stats;     // CPU/GPU/memory cards (PC on)
static lv_obj_t *wol_panel; // Wake-on-LAN panel (PC off)
static lv_obj_t *lbl_status;
static chip_card cpu, gpu;
static mem_row ram, vram;

static void make_chip(chip_card &c, lv_coord_t x, const char *fallback)
{
    c.fallback = fallback;
    lv_obj_t *card = ui_card(stats, x, TOP_Y, CHIP_W, CHIP_H, CHIP_PAD, CHIP_PAD);
    const lv_coord_t inner = CHIP_W - 2 * CHIP_PAD;

    c.name = ui_label(card, &font_vn_16, CL_TEXT, fallback);
    lv_obj_set_width(c.name, inner);
    lv_label_set_long_mode(c.name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(c.name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(c.name, 0, -2);

    c.arc = lv_arc_create(card);
    lv_obj_set_size(c.arc, ARC_D, ARC_D);
    lv_obj_set_pos(c.arc, (inner - ARC_D) / 2, ARC_Y);
    lv_arc_set_rotation(c.arc, 135);
    lv_arc_set_bg_angles(c.arc, 0, 270);
    lv_arc_set_range(c.arc, 0, 100);
    lv_arc_set_value(c.arc, 0);
    lv_obj_remove_style(c.arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(c.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(c.arc, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(c.arc, 0, 0);
    lv_obj_set_style_arc_width(c.arc, 8, LV_PART_MAIN);
    lv_obj_set_style_arc_color(c.arc, CL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(c.arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_width(c.arc, 8, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(c.arc, CL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(c.arc, true, LV_PART_INDICATOR);

    c.pct = ui_label(c.arc, &lv_font_montserrat_22, CL_TEXT, "--");
    lv_obj_align(c.pct, LV_ALIGN_CENTER, 0, -7);
    c.clock = ui_label(c.arc, &lv_font_montserrat_14, CL_FAINT, "");
    lv_obj_align(c.clock, LV_ALIGN_CENTER, 0, 15);

    // bottom line: temperature left, power right
    c.temp = ui_label(card, &lv_font_montserrat_16, CL_TEXT, "--\xC2\xB0""C");
    lv_obj_align(c.temp, LV_ALIGN_BOTTOM_LEFT, 0, 2);
    c.power = ui_label(card, &lv_font_montserrat_16, CL_DIM, "");
    lv_obj_align(c.power, LV_ALIGN_BOTTOM_RIGHT, 0, 2);
}

static void make_mem(mem_row &r, lv_obj_t *card, lv_coord_t y, const char *name)
{
    lv_obj_t *n = ui_label(card, &lv_font_montserrat_14, CL_DIM, name);
    lv_obj_set_pos(n, 0, y);

    r.bar = lv_bar_create(card);
    lv_obj_set_pos(r.bar, 48, y + 5);
    lv_obj_set_size(r.bar, 84, 8);
    lv_bar_set_range(r.bar, 0, 100);
    lv_bar_set_value(r.bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(r.bar, CL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(r.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(r.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_color(r.bar, CL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_radius(r.bar, 4, LV_PART_INDICATOR);

    r.pct = ui_label(card, &lv_font_montserrat_16, CL_TEXT, "--");
    lv_obj_set_width(r.pct, 44);
    lv_obj_set_style_text_align(r.pct, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(r.pct, 136, y - 1);

    r.detail = ui_label(card, &lv_font_montserrat_14, CL_FAINT, "");
    lv_obj_align(r.detail, LV_ALIGN_TOP_RIGHT, 0, y);
}

lv_obj_t *pc_page_create(lv_obj_t *parent)
{
    page = lv_obj_create(parent);
    lv_obj_remove_style_all(page);
    lv_obj_set_size(page, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);

    stats = ui_page_create(page);
    wol_panel = wol_panel_create(page);
    lv_obj_add_flag(wol_panel, LV_OBJ_FLAG_HIDDEN);

    make_chip(cpu, UI_MARGIN, "CPU");
    make_chip(gpu, UI_MARGIN + CHIP_W + UI_GAP, "GPU");

    lv_obj_t *mem = ui_card(stats, UI_MARGIN, MEM_Y, MEM_W, MEM_H, MEM_PAD_X, MEM_PAD_Y);
    make_mem(ram, mem, 0, "RAM");
    make_mem(vram, mem, MEM_ROW, "VRAM");

    lbl_status = ui_label(page, &font_vn_16, CL_ACCENT, "");
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -3);

    pc_stats_t empty = {};
    empty.cpu = empty.cpu_temp = empty.gpu = empty.gpu_temp = NAN;
    empty.ram_used = empty.ram_total = empty.vram_used = empty.vram_total = NAN;
    empty.cpu_clock = empty.gpu_clock = empty.cpu_power = empty.gpu_power = NAN;
    pc_page_update(empty, false);
    return page;
}

static void set_chip(chip_card &c, const char *name, float load, float temp, float mhz, float watt)
{
    lv_label_set_text(c.name, name && name[0] ? name : c.fallback);

    if (isnan(load)) {
        lv_label_set_text(c.pct, "--");
        lv_arc_set_value(c.arc, 0);
    } else {
        int p = (int)lroundf(load);
        if (p < 0) p = 0;
        if (p > 100) p = 100;
        lv_label_set_text_fmt(c.pct, "%d%%", p);
        lv_arc_set_value(c.arc, p);
        lv_obj_set_style_arc_color(c.arc, theme_usage_color(p), LV_PART_INDICATOR);
    }

    char b[16];
    if (isnan(mhz) || mhz <= 0) b[0] = 0;
    else snprintf(b, sizeof(b), "%.2f GHz", mhz / 1000.0f);
    lv_label_set_text(c.clock, b);

    if (isnan(temp)) {
        lv_label_set_text(c.temp, "--\xC2\xB0""C");
        lv_obj_set_style_text_color(c.temp, CL_FAINT, 0);
    } else {
        lv_label_set_text_fmt(c.temp, "%d\xC2\xB0""C", (int)lroundf(temp));
        lv_obj_set_style_text_color(c.temp, theme_temp_color(temp), 0);
    }

    if (isnan(watt)) b[0] = 0;
    else snprintf(b, sizeof(b), "%d W", (int)lroundf(watt));
    lv_label_set_text(c.power, b);
}

static void set_mem(mem_row &r, float used, float total)
{
    if (isnan(used) || isnan(total) || total <= 0) {
        lv_label_set_text(r.pct, "--");
        lv_label_set_text(r.detail, "");
        lv_bar_set_value(r.bar, 0, LV_ANIM_OFF);
        return;
    }
    int p = (int)lroundf(used * 100.0f / total);
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    lv_label_set_text_fmt(r.pct, "%d%%", p);
    lv_bar_set_value(r.bar, p, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(r.bar, theme_usage_color(p), LV_PART_INDICATOR);
    char b[24];
    snprintf(b, sizeof(b), "%.1f/%.0f GB", used, total);
    lv_label_set_text(r.detail, b);
}

void pc_page_update(const pc_stats_t &in, bool fresh)
{
    // When the PC stops sending, blank the numbers rather than freeze old ones
    // (the chip names stay).
    pc_stats_t s = in;
    if (!fresh) {
        s.cpu = s.cpu_temp = s.gpu = s.gpu_temp = NAN;
        s.ram_used = s.ram_total = s.vram_used = s.vram_total = NAN;
        s.cpu_clock = s.gpu_clock = s.cpu_power = s.gpu_power = NAN;
    }
    set_chip(cpu, s.cpu_name, s.cpu, s.cpu_temp, s.cpu_clock, s.cpu_power);
    set_chip(gpu, s.gpu_name, s.gpu, s.gpu_temp, s.gpu_clock, s.gpu_power);
    set_mem(ram, s.ram_used, s.ram_total);
    set_mem(vram, s.vram_used, s.vram_total);
}

void pc_page_show_wol(bool show)
{
    if (show) {
        lv_obj_add_flag(stats, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wol_panel, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(stats, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wol_panel, LV_OBJ_FLAG_HIDDEN);
    }
}

void pc_page_set_status(const char *msg, bool accent)
{
    char b[96];
    if (msg && msg[0]) snprintf(b, sizeof(b), "\xE2\x80\xA2 %s", msg); // "• msg"
    else b[0] = 0;
    lv_label_set_text(lbl_status, b);
    lv_obj_set_style_text_color(lbl_status, accent ? CL_ACCENT : CL_FAINT, 0);
}
