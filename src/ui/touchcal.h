#pragma once
#include <lvgl.h>

// Full-screen overlay for the 2-tap touch calibration.
enum touchcal_step_t { TC_RIGHT, TC_LEFT, TC_RETRY, TC_DONE };

void touchcal_create(lv_obj_t *parent);
void touchcal_show(touchcal_step_t step);
void touchcal_hide();
