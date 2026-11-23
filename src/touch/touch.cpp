#include "touch.h"
#include <SPI.h>
#include <Preferences.h>

#define TP_CLK  25
#define TP_MOSI 32
#define TP_MISO 39
#define TP_CS   33
#define TP_IRQ  36   // LOW while the panel is pressed

#define BOOT_PIN 0   // BOOT button, LOW when pressed

#define Z_THRESHOLD   400  // pressure needed to count as a touch (same as XPT2046_Touchscreen)
#define POLL_MS       25
#define TAP_MIN_MS    20   // shorter = noise
#define TAP_MAX_MS    700  // longer (but < TOUCH_LONG_MS) = ignored, so a hold never also flips the page
#define RELEASE_POLLS 3    // need this many "not pressed" polls in a row (~75 ms): the
                           // panel's pressure reading flickers a bit during a long hold

// TFT_eSPI uses the default SPI (VSPI) for the display, so touch gets HSPI.
static SPIClass tp_spi(HSPI);
static const SPISettings tp_cfg(2000000, MSBFIRST, SPI_MODE0);

static bool down = false;        // debounced pressed state
static bool long_fired = false;
static uint8_t release_count = 0;
static uint32_t last_poll = 0, press_start = 0;

// position samples of the current press (panel only)
static long sum_a = 0, sum_b = 0;
static int n_samples = 0;
static bool press_from_boot = false;
static int tap_a = -1, tap_b = -1;
static bool tap_has_pos = false;

// calibration
static bool cal_valid = false;
static uint8_t cal_axis = 1;      // default guess: channel B runs along the width
static int cal_mid = 2048;
static bool cal_right_high = false;

// One read: pressure + both position channels. Each 16-bit transfer returns
// the result of the previous command while sending the next one. The last command (0xD0, PD
// bits = 00) leaves the controller in power-down with PENIRQ enabled, so the
// IRQ pin keeps working.
static int read_panel(int &a, int &b)
{
    tp_spi.beginTransaction(tp_cfg);
    digitalWrite(TP_CS, LOW);
    tp_spi.transfer(0xB1);                    // start Z1
    int z1 = tp_spi.transfer16(0xC1) >> 3;    // read Z1, start Z2
    int z2 = tp_spi.transfer16(0x91) >> 3;    // read Z2, start A
    tp_spi.transfer16(0x91);                  // first A conversion is noisy: discard
    int a1 = tp_spi.transfer16(0x91) >> 3;    // read A (2nd sample), start A again
    a = tp_spi.transfer16(0xD1) >> 3;         // read A (3rd sample), start B
    tp_spi.transfer16(0xD1);                  // discard first B
    b = tp_spi.transfer16(0xD0) >> 3;         // read B, start B with power-down
    tp_spi.transfer16(0);
    digitalWrite(TP_CS, HIGH);
    tp_spi.endTransaction();
    a = (a + a1) / 2;
    return z1 + 4095 - z2;
}

void touch_init()
{
    pinMode(TP_CS, OUTPUT);
    digitalWrite(TP_CS, HIGH);
    pinMode(TP_IRQ, INPUT);
    pinMode(BOOT_PIN, INPUT_PULLUP);
    tp_spi.begin(TP_CLK, TP_MISO, TP_MOSI, TP_CS);
    int a, b;
    read_panel(a, b); // puts the controller into PENIRQ mode

    Preferences p;
    p.begin("touch", false);
    cal_valid = p.getBool("valid", false);
    if (cal_valid) {
        cal_axis = p.getUChar("axis", 1);
        cal_mid = p.getShort("mid", 2048);
        cal_right_high = p.getBool("rhigh", false);
    }
    p.end();
}

bool touch_cal_valid() { return cal_valid; }

void touch_cal_save(int axis, int mid, bool right_high)
{
    cal_axis = axis ? 1 : 0;
    cal_mid = mid;
    cal_right_high = right_high;
    cal_valid = true;
    Preferences p;
    p.begin("touch", false);
    p.putBool("valid", true);
    p.putUChar("axis", cal_axis);
    p.putShort("mid", (int16_t)cal_mid);
    p.putBool("rhigh", cal_right_high);
    p.end();
    Serial.printf("[touch] calibrated: axis %d, mid %d, right %s\n", cal_axis, cal_mid,
                  cal_right_high ? "high" : "low");
}

// Panel pressed? Collects a position sample when it is.
static bool panel_pressed()
{
    // Fast path: IRQ high = definitely not touched, no SPI traffic needed.
    // If a board leaves IRQ floating we still confirm with the pressure read.
    if (digitalRead(TP_IRQ) == HIGH) return false;
    int a, b;
    if (read_panel(a, b) < Z_THRESHOLD) return false;
    sum_a += a;
    sum_b += b;
    n_samples++;
    return true;
}

touch_event_t touch_poll()
{
    uint32_t now = millis();
    if (now - last_poll < POLL_MS) return TOUCH_NONE;
    last_poll = now;

    bool boot = digitalRead(BOOT_PIN) == LOW;
    bool raw = boot || panel_pressed();

    if (raw) {
        release_count = 0;
        if (!down) {             // press started
            down = true;
            long_fired = false;
            press_start = now;
            press_from_boot = boot;
            if (boot) { sum_a = sum_b = 0; n_samples = 0; }
            else if (n_samples > 1) { // keep only this press's first sample
                sum_a /= n_samples; sum_b /= n_samples; n_samples = 1;
            }
        } else if (!long_fired && now - press_start >= TOUCH_LONG_MS) {
            long_fired = true;
            return TOUCH_LONG;
        }
        return TOUCH_NONE;
    }

    if (!down) {
        sum_a = sum_b = 0;   // nothing pressed: forget stray samples
        n_samples = 0;
        return TOUCH_NONE;
    }
    if (++release_count < RELEASE_POLLS) return TOUCH_NONE;

    // released
    down = false;
    uint32_t dur = now - press_start - (RELEASE_POLLS - 1) * POLL_MS;
    bool tap = !long_fired && dur >= TAP_MIN_MS && dur <= TAP_MAX_MS;
    if (tap) {
        tap_has_pos = !press_from_boot && n_samples > 0;
        if (tap_has_pos) {
            tap_a = sum_a / n_samples;
            tap_b = sum_b / n_samples;
        }
    }
    sum_a = sum_b = 0;
    n_samples = 0;
    return tap ? TOUCH_TAP : TOUCH_NONE;
}

bool touch_is_down() { return down; }

uint32_t touch_held_ms()
{
    if (!down || long_fired) return 0;
    return millis() - press_start;
}

bool touch_tap_raw(int &a, int &b)
{
    if (!tap_has_pos) return false;
    a = tap_a;
    b = tap_b;
    return true;
}

int touch_tap_side()
{
    if (!tap_has_pos) return 0;
    int v = cal_axis ? tap_b : tap_a;
    bool high = v > cal_mid;
    return (high == cal_right_high) ? 1 : -1;
}
