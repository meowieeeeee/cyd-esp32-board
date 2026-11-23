#include "market_page.h"
#include "ui_common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ---- "Giá cả" page ----
//  Giá cả           [ Vàng · Tỷ giá | Xăng ]   <- title + segmented tabs (own header,
//                        Mua      Bán            the shared clock/temp/dots are hidden)
//  ┌──────────────────────────────────────────┐
//  │ Miếng SJC       138,0    141,0     ↑0,5    │  <- gold card
//  │ Nhẫn 999.9      136,3    140,3     ↓0,2    │
//  └──────────────────────────────────────────┘
//  ┌ USD / Bitcoin ┐                               <- exchange rate card
//  Vàng triệu/lượng · SJC · VCB · 12:29 07/11      <- footer, left aligned
//
// The touch panel only knows left/right, so the tabs are steps of the normal
// paging: tap right on "Vàng · Tỷ giá" -> "Xăng" -> next page (main.cpp).

// Colors of the reference design (neutral dark, not the purple-gray cards)
#define C_PAGE_BG  lv_color_hex(0x0A0F12)
#define C_CARD     lv_color_hex(0x1A1D21)
#define C_SEG_ON   lv_color_hex(0x252C35)
#define C_TEXT     lv_color_hex(0xEEEAEE)
#define C_FAINT    lv_color_hex(0x8B91A4)
#define C_LINE     lv_color_hex(0x272B31)
#define C_UP       lv_color_hex(0x39CA7B)   // Vietnamese price boards: up = green,
#define C_DOWN     lv_color_hex(0xFF696A)   //                          down = red
#define C_ACCENT   CL_ACCENT

// Layout (screen coordinates, 320x240)
#define CARD_X     8
#define CARD_W     (UI_SCR_W - 2 * CARD_X)   // 304
#define CARD_PAD   12
#define NAME_X     (CARD_X + CARD_PAD)        // 20
#define COL1_R     206                        // fuel: right edge of "Giá"
#define COL2_R     (CARD_X + CARD_W - CARD_PAD) // 300: right edge of "Kỳ trước" / the change column
#define BUY_R      167                        // prices: right edge of "Mua"
#define SELL_R     252                        //         right edge of "Bán"
#define HEAD_Y     36
#define CARD1_Y    54
#define PRICE_ROW_H 36                        // 2 rows per card
#define CARD2_Y    (CARD1_Y + 2 * PRICE_ROW_H + 6)
#define FUEL_ROW_H 30
#define FUEL_CARD_H (FUEL_N * FUEL_ROW_H)     // 150

#define SEG_X      112
#define SEG_Y      2
#define SEG_W      (UI_SCR_W - UI_MARGIN - SEG_X) // 178
#define SEG_H      28
#define SEG_PAD    3
#define SEG_TAB_W  ((SEG_W - 2 * SEG_PAD) / MARKET_TAB_N)

#define RECENT_S   (24 * 3600)                // fuel: "vừa điều chỉnh" for a day

struct price_row {
    lv_obj_t *buy, *sell, *delta;
};

static lv_obj_t *page;
static lv_obj_t *seg_on, *seg_lbl[MARKET_TAB_N];
static lv_obj_t *panel[MARKET_TAB_N], *footer[MARKET_TAB_N];
static price_row r_bar, r_ring, r_usd, r_btc;
static lv_obj_t *fuel_price[FUEL_N], *fuel_delta[FUEL_N];
static int g_tab = MARKET_TAB_GOLD;

// ---------------------------------------------------------------------------
// Building blocks

static lv_obj_t *box(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                     lv_coord_t radius, lv_color_t color)
{
    lv_obj_t *o = ui_shape(parent, x, y, w, h, radius, color);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *t,
                      lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *l = ui_label(parent, f, c, t);
    lv_obj_set_pos(l, x, y);
    return l;
}

// right-aligned label whose right edge sits at x = right
static lv_obj_t *rtext(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *t,
                       lv_coord_t right, lv_coord_t y, lv_coord_t w)
{
    lv_obj_t *l = ui_label(parent, f, c, t);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(l, right - w, y);
    return l;
}

static lv_obj_t *make_panel()
{
    lv_obj_t *p = ui_page_create(page);
    lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

static void column_heads(lv_obj_t *p, lv_coord_t r1, const char *h1, lv_coord_t r2, const char *h2)
{
    rtext(p, &font_vn_14, C_FAINT, h1, r1, HEAD_Y, 80);
    rtext(p, &font_vn_14, C_FAINT, h2, r2, HEAD_Y, 80);
}

// One price line: name | buy | sell | change (all on one line, baselines aligned)
static void make_line(lv_obj_t *p, price_row &r, lv_coord_t y, const char *name)
{
    lv_coord_t ty = y + (PRICE_ROW_H - 20) / 2;
    text(p, &font_vn_16, C_TEXT, name, NAME_X, ty);
    r.buy = rtext(p, &font_price_20, C_TEXT, "--", BUY_R, ty, 80);
    r.sell = rtext(p, &font_price_20, C_TEXT, "--", SELL_R, ty, 90);
    r.delta = rtext(p, &font_vn_14, C_FAINT, "", COL2_R, ty + 3, 54);
}

// A card holding two price lines with a divider
static void make_card2(lv_obj_t *p, lv_coord_t y, price_row &a, const char *na, price_row &b,
                       const char *nb)
{
    box(p, CARD_X, y, CARD_W, 2 * PRICE_ROW_H, 12, C_CARD);
    make_line(p, a, y, na);
    box(p, NAME_X, y + PRICE_ROW_H - 1, COL2_R - NAME_X, 1, 0, C_LINE);
    make_line(p, b, y + PRICE_ROW_H, nb);
}

static lv_obj_t *make_footer(lv_obj_t *p)
{
    lv_obj_t *l = text(p, &font_vn_14, C_FAINT, "Đang tải...", UI_MARGIN, 240 - 6 - 17);
    lv_obj_set_size(l, UI_SCR_W - 2 * UI_MARGIN, 17);   // one line, "..." if too long
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    return l;
}

static void set_footer(int tab, const char *msg, bool accent)
{
    lv_label_set_text(footer[tab], msg);
    lv_obj_set_style_text_color(footer[tab], accent ? C_ACCENT : C_FAINT, 0);
}

// ---------------------------------------------------------------------------

lv_obj_t *market_page_create(lv_obj_t *parent)
{
    page = ui_page_create(parent);
    lv_obj_set_style_bg_color(page, C_PAGE_BG, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);

    // Title + segmented tabs
    text(page, &font_price_22, C_TEXT, "Giá cả", UI_MARGIN, 4);
    box(page, SEG_X, SEG_Y, SEG_W, SEG_H, 9, C_CARD);
    seg_on = box(page, SEG_X + SEG_PAD, SEG_Y + SEG_PAD, SEG_TAB_W, SEG_H - 2 * SEG_PAD, 7, C_SEG_ON);
    static const char *const NAMES[MARKET_TAB_N] = {"Vàng \xC2\xB7 Tỷ giá", "Xăng"};
    for (int i = 0; i < MARKET_TAB_N; i++) {
        seg_lbl[i] = ui_label(page, &font_vn_14, C_FAINT, NAMES[i]);
        lv_obj_set_width(seg_lbl[i], SEG_TAB_W);
        lv_obj_set_style_text_align(seg_lbl[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(seg_lbl[i], SEG_X + SEG_PAD + i * SEG_TAB_W, SEG_Y + (SEG_H - 17) / 2);
    }

    // Vàng · Tỷ giá: gold card + exchange rate card
    lv_obj_t *p = panel[MARKET_TAB_GOLD] = make_panel();
    column_heads(p, BUY_R, "Mua", SELL_R, "Bán");
    make_card2(p, CARD1_Y, r_bar, "Miếng SJC", r_ring, "Nhẫn SJC");
    make_card2(p, CARD2_Y, r_usd, "USD", r_btc, "Bitcoin");
    // Bitcoin has no buy/sell: USD price in the sell column, buy column empty
    lv_label_set_text(r_btc.buy, "");
    footer[MARKET_TAB_GOLD] = make_footer(p);

    // Xăng: one card, a row per product
    p = panel[MARKET_TAB_FUEL] = make_panel();
    column_heads(p, COL1_R, "Giá", COL2_R, "Kỳ trước");
    box(p, CARD_X, CARD1_Y, CARD_W, FUEL_CARD_H, 12, C_CARD);
    for (int i = 0; i < FUEL_N; i++) {
        lv_coord_t y = CARD1_Y + i * FUEL_ROW_H;
        lv_coord_t ty = y + (FUEL_ROW_H - 19) / 2;
        text(p, &font_vn_16, C_TEXT, FUEL_NAMES[i], NAME_X, ty);
        fuel_price[i] = rtext(p, &font_vn_16, C_TEXT, "--", COL1_R, ty, 70);
        fuel_delta[i] = rtext(p, &font_vn_14, C_FAINT, "", COL2_R, ty + 1, 80);
        if (i < FUEL_N - 1) box(p, NAME_X, y + FUEL_ROW_H - 1, COL2_R - NAME_X, 1, 0, C_LINE);
    }
    footer[MARKET_TAB_FUEL] = make_footer(p);

    market_page_set_tab(MARKET_TAB_GOLD);
    return page;
}

void market_page_set_tab(int tab)
{
    if (tab < 0 || tab >= MARKET_TAB_N) return;
    g_tab = tab;
    for (int i = 0; i < MARKET_TAB_N; i++) {
        if (i == tab) lv_obj_clear_flag(panel[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(panel[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_text_color(seg_lbl[i], i == tab ? C_TEXT : C_FAINT, 0);
    }
    lv_obj_set_x(seg_on, SEG_X + SEG_PAD + tab * SEG_TAB_W);
}

int market_page_tab() { return g_tab; }

// ---------------------------------------------------------------------------
// Formatting

// 1234567.8 -> "1.234.568" (Vietnamese thousands separator)
static void fmt_int_vn(double v, char *out, size_t n)
{
    char raw[24];
    snprintf(raw, sizeof(raw), "%.0f", fabs(v));
    int len = strlen(raw), o = 0;
    if (v < 0 && o < (int)n - 1) out[o++] = '-';
    for (int i = 0; i < len && o < (int)n - 1; i++) {
        if (i && (len - i) % 3 == 0 && o < (int)n - 1) out[o++] = '.';
        out[o++] = raw[i];
    }
    out[o] = 0;
}

// 119.5 -> "119,50" (Vietnamese decimal comma)
static void fmt_dec_vn(double v, int decimals, char *out, size_t n)
{
    snprintf(out, n, "%.*f", decimals, v);
    for (char *c = out; *c; c++) if (*c == '.') *c = ',';
}

// VND -> million with 1 decimal, 2 when needed: 141000000 -> "141,0", 140250000 -> "140,25"
static void fmt_mil(double vnd, char *out, size_t n)
{
    long tenk = lround(fabs(vnd) / 1e4);          // in 10,000 VND
    fmt_dec_vn(vnd / 1e6, tenk % 10 ? 2 : 1, out, n);
}

// "HH:MM DD/MM" local time
static void fmt_when(time_t t, int tz, char *out, size_t n)
{
    t += tz;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d %02d/%02d", tm.tm_hour, tm.tm_min, tm.tm_mday, tm.tm_mon + 1);
}

// Change label: "↑0,5" green / "↓0,2" red, hidden when 0
static void set_delta(lv_obj_t *l, double d, const char *val)
{
    if (d == 0 || !val) {
        lv_label_set_text(l, "");
        return;
    }
    lv_label_set_text_fmt(l, "%s%s", d > 0 ? "\xE2\x86\x91" : "\xE2\x86\x93", val);
    lv_obj_set_style_text_color(l, d > 0 ? C_UP : C_DOWN, 0);
}

static void show_gold_row(price_row &r, double buy, double sell, double dsell)
{
    char a[24];
    fmt_mil(buy, a, sizeof(a));
    lv_label_set_text(r.buy, a);
    fmt_mil(sell, a, sizeof(a));
    lv_label_set_text(r.sell, a);
    fmt_mil(fabs(dsell), a, sizeof(a));
    set_delta(r.delta, dsell, a);
}

// ---------------------------------------------------------------------------

void market_page_update(const market_t &m, int tz)
{
    char a[32], b[128];

    // ---- Vàng (million VND / lượng) ----
    if (m.gold_ok) show_gold_row(r_bar, m.gold_buy, m.gold_sell, m.gold_dsell);
    if (m.ring_ok) show_gold_row(r_ring, m.ring_buy, m.ring_sell, m.ring_dsell);

    // ---- Tỷ giá: USD (VND), Bitcoin (USD) ----
    if (m.usd_ok) {
        fmt_int_vn(m.usd_buy, a, sizeof(a));
        lv_label_set_text(r_usd.buy, a);
        fmt_int_vn(m.usd_sell, a, sizeof(a));
        lv_label_set_text(r_usd.sell, a);
        fmt_int_vn(fabs(m.usd_dsell), a, sizeof(a));
        set_delta(r_usd.delta, m.usd_dsell, a);
    }
    if (m.btc_ok) {
        fmt_int_vn(m.btc_usd, a, sizeof(a));
        snprintf(b, sizeof(b), "%s $", a);
        lv_label_set_text(r_btc.sell, b);
        fmt_dec_vn(fabs(m.btc_change), 1, a, sizeof(a));
        snprintf(b, sizeof(b), "%s%%", a);
        set_delta(r_btc.delta, m.btc_change, b);
    }

    // ---- Footer: units + sources + newest update, or an item that never loaded ----
    const char *err_what = nullptr;
    int err = 0;
    if (!m.gold_ok && m.gold_err)     { err_what = "giá vàng"; err = m.gold_err; }
    else if (!m.usd_ok && m.usd_err)  { err_what = "tỷ giá USD"; err = m.usd_err; }
    else if (!m.btc_ok && m.btc_err)  { err_what = "Bitcoin"; err = m.btc_err; }
    if (err_what) {
        snprintf(b, sizeof(b), "Lỗi tải %s (%d)", err_what, err);
        set_footer(MARKET_TAB_GOLD, b, true);
    } else if (m.gold_ok || m.usd_ok) {
        time_t t = m.gold_time > m.usd_time ? m.gold_time : m.usd_time;
        fmt_when(t, tz, a, sizeof(a));
        snprintf(b, sizeof(b), "Vàng triệu/lượng \xC2\xB7 SJC \xC2\xB7 VCB \xC2\xB7 %s", a);
        set_footer(MARKET_TAB_GOLD, b, false);
    }
}

// 19100 -> "19.100"
static void fmt_vnd(long v, char *out, size_t n)
{
    if (v >= 1000) snprintf(out, n, "%ld.%03ld", v / 1000, v % 1000);
    else snprintf(out, n, "%ld", v);
}

void market_page_update_fuel(const fuel_t &f, time_t now, int tz)
{
    char a[24], b[128];

    if (f.ok) {
        for (int i = 0; i < FUEL_N; i++) {
            if (f.price[i]) fmt_vnd(f.price[i], a, sizeof(a));
            else strcpy(a, "--");
            lv_label_set_text(fuel_price[i], a);

            long d = f.delta[i];
            if (!f.has_delta || !f.price[i]) {
                lv_label_set_text(fuel_delta[i], "--");
                lv_obj_set_style_text_color(fuel_delta[i], C_FAINT, 0);
            } else if (d == 0) {
                lv_label_set_text(fuel_delta[i], "0");
                lv_obj_set_style_text_color(fuel_delta[i], C_FAINT, 0);
            } else {
                fmt_vnd(labs(d), a, sizeof(a));
                set_delta(fuel_delta[i], (double)d, a);
            }
        }
    }

    // Footer: "Petrolimex · vùng 1 · từ 15:00 05/11"
    char when[64] = "";
    if (strlen(f.effective) >= 10) {
        int y, m, d, hh = -1, mm = 0;
        sscanf(f.effective, "%d-%d-%d", &y, &m, &d);
        if (strlen(f.effective) >= 16) sscanf(f.effective + 11, "%d:%d", &hh, &mm);
        if (hh >= 0) snprintf(when, sizeof(when), " \xC2\xB7 từ %02d:%02d %02d/%02d", hh, mm, d, m);
        else snprintf(when, sizeof(when), " \xC2\xB7 từ %02d/%02d", d, m);
    }
    bool recent = f.changed_at && now > 1000000000 && now - f.changed_at < RECENT_S;

    if (!f.ok && f.last_error) {
        snprintf(b, sizeof(b), "Lỗi tải giá xăng (%d)", f.last_error);
        set_footer(MARKET_TAB_FUEL, b, true);
    } else if (!f.ok) {
        set_footer(MARKET_TAB_FUEL, "Đang tải...", false);
    } else if (recent) {
        snprintf(b, sizeof(b), "Vừa điều chỉnh giá%s", when);
        set_footer(MARKET_TAB_FUEL, b, true);
    } else {
        snprintf(b, sizeof(b), "Petrolimex \xC2\xB7 vùng 1%s", when);
        set_footer(MARKET_TAB_FUEL, b, false);
    }
    (void)tz;
}
