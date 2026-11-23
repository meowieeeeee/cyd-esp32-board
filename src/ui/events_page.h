#pragma once
#include <lvgl.h>

// Personal countdowns: the nearest event big on top (days left, date,
// "lần thứ N"), the next ones listed below. Top-left icon: a calendar sheet
// with the days left to the nearest event.

lv_obj_t *events_page_create(lv_obj_t *parent);
void events_page_update(long today_jd);   // call once a day + after the settings change
