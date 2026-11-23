#include "screen_sleep.h"
#include "../display/display.h"
#include <Arduino.h>

#define FADE_OUT_MS 1200
#define FADE_IN_MS  250
#define FADE_STEP_MS 15

enum state_t { AWAKE, FADING_OUT, ASLEEP, FADING_IN };

static state_t st = AWAKE;
static uint32_t sleep_after = 0, touch_awake = 0;
static uint32_t last_pc = 0, last_touch = 0;
static uint32_t fade_start = 0, last_step = 0;
static uint8_t fade_from = 0;
static uint8_t level = 255; // current backlight PWM level

static void start_fade(state_t to)
{
    st = to;
    fade_start = millis();
    fade_from = level;
}

static void wake()
{
    if (st == FADING_OUT || st == ASLEEP) {
        Serial.println("[sleep] wake");
        start_fade(FADING_IN);
    }
}

void screen_sleep_begin(uint32_t sleep_after_ms, uint32_t touch_awake_ms)
{
    sleep_after = sleep_after_ms;
    touch_awake = touch_awake_ms;
    last_pc = last_touch = millis(); // counts from boot: PC off at boot -> sleeps after sleep_after_ms
    level = get_brightness();
    st = AWAKE;
}

void screen_sleep_pc_activity()
{
    last_pc = millis();
    wake();
}

bool screen_sleep_touch()
{
    last_touch = millis();
    bool was_dark = st == ASLEEP || st == FADING_OUT;
    wake();
    return was_dark;
}

bool screen_sleep_is_asleep() { return st == ASLEEP || st == FADING_OUT; }

void screen_sleep_loop()
{
    if (!sleep_after) return;
    uint32_t now = millis();

    bool want_awake = (now - last_pc < sleep_after) || (now - last_touch < touch_awake);
    if (st == AWAKE && !want_awake) {
        Serial.println("[sleep] no PC data -> screen off");
        start_fade(FADING_OUT);
    }

    if (st != FADING_OUT && st != FADING_IN) return;
    if (now - last_step < FADE_STEP_MS) return;
    last_step = now;

    uint8_t target = st == FADING_IN ? get_brightness() : 0;
    uint32_t dur = st == FADING_IN ? FADE_IN_MS : FADE_OUT_MS;
    uint32_t t = now - fade_start;
    if (t >= dur) {
        level = target;
        st = st == FADING_IN ? AWAKE : ASLEEP;
    } else {
        level = (uint8_t)(fade_from + ((int)target - (int)fade_from) * (int)t / (int)dur);
    }
    set_backlight_raw(level);
}
