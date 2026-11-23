#pragma once
#include <lvgl.h>

// Lunar calendar page (Vietnamese text): big lunar day + month + year (Can Chi)
// on the left, weekday / solar date / day & month Can Chi on the right,
// countdown to the next Ram (15th) and Mung 1 below, next holiday on the
// status line. The top-left icon shows the moon phase of the lunar day.

lv_obj_t *lunar_page_create(lv_obj_t *parent);
void lunar_page_update(int dd, int mm, int yy);   // today's solar date (local time)
