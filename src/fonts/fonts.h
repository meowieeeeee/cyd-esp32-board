#pragma once
#include <lvgl.h>

// Tiempos (Claude's serif) is only used for the clock - digits + ':' so plain
// ASCII is enough. Everything else uses LVGL's built-in Montserrat, which has
// '°', '•', and extended Latin.
#ifdef __cplusplus
extern "C" {
#endif

extern const lv_font_t font_tiempos_34;

// Montserrat Medium with Vietnamese letters (built-in Montserrat has none) + ↑↓ from DejaVu Sans,
// generated with lv_font_conv (ASCII + Vietnamese + ° · • – ↑ ↓; 14/16 px). No LV_SYMBOL_* icons.
extern const lv_font_t font_vn_14;
extern const lv_font_t font_vn_16;
extern const lv_font_t font_vn_22;
// Digits 0-9 only: big lunar day number / Tet countdown
extern const lv_font_t font_num_52;
extern const lv_font_t font_num_72;
// Inter (weather page): big clock "0-9:" (Light 72), seconds (Regular 20),
// current temperature "-0-9°" (Medium 42)
extern const lv_font_t font_clock_72;
extern const lv_font_t font_clock_20;
extern const lv_font_t font_temp_42;
// Montserrat SemiBold 22, prices page title "Giá cả" only
extern const lv_font_t font_price_22;
// Montserrat SemiBold 20, prices page rows: digits , . $ %
extern const lv_font_t font_price_20;

#ifdef __cplusplus
}
#endif
