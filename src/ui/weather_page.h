#pragma once
#include <lvgl.h>
#include <time.h>
#include "../weather/weather.h"

// Weather page (full screen, no page icon/dots): date + AQI pill, big clock
// with seconds, current temperature/conditions, lunar date, city + status,
// feels-like / humidity / wind pills and 4 day cards whose blue fill shows the
// chance of rain. Icons are drawn from LVGL shapes.

lv_obj_t *weather_page_create(lv_obj_t *parent);
void weather_page_update(const weather_t &w);
void weather_page_tick(time_t local);              // local time; call every second while shown
void weather_page_set_city(const char *city);
void weather_page_set_status(const char *msg, bool accent);  // right of the city
