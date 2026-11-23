#pragma once
#include <lvgl.h>
#include <time.h>

// Tet countdown page: "Tết Đinh Mùi 2027" and the big number of days left;
// during Mung 1-3 a "Chúc Mừng Năm Mới" greeting instead.
// Top-left icon: a yellow apricot blossom (hoa mai).

lv_obj_t *tet_page_create(lv_obj_t *parent);

// now_local: seconds since 1970 in local time (UTC epoch + timezone offset).
// tet_jd / lunar_year from lunar_tet_jd().
void tet_page_update(time_t now_local, long tet_jd, int lunar_year);
