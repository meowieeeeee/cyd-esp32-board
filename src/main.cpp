#include <Arduino.h>
#include <time.h>
#include "display/display.h"
#include "theme.h"
#include "assets/clawd_still.h"
#include "wifi_manager/wifi_manager.h"
#include "claude_quota/claude_quota.h"
#include "weather/weather.h"
#include "pc_stats/pc_stats.h"
#include "touch/touch.h"
#include "ui/pc_page.h"
#include "ui/weather_page.h"
#include "ui/wol_page.h"
#include "ui/lunar_page.h"
#include "ui/tet_page.h"
#include "ui/market_page.h"
#include "ui/quota_page.h"
#include "ui/ui_common.h"
#include "ui/touchcal.h"
#include "ui/events_page.h"
#include "fuel/fuel.h"
#include "events/events.h"
#include "market/market.h"
#include "lunar/lunar.h"
#include "wol/wol.h"
#include "power/screen_sleep.h"
#include "settings/settings.h"
#include <WiFi.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <esp_system.h>

// Location, brightness, screen sleep, pages: set in the browser at
// http://<board IP>/settings (see settings/settings.h).

#define TZ_OFFSET_SEC (7 * 3600)   // offset from UTC (VN = +7)

// ---- Layout of the quota page (320x240 screen) ----
#define SCR_W       320
#define MARGIN      14
#define CARD_W      (SCR_W - 2 * MARGIN)
// The page body (two ring gauges) lives in ui/quota_page.cpp.
// Shared header: clock (left), Clawd / page icon (right)
#define HEADER_TEXT_Y 3

#define SCREEN_TOUCH_AWAKE_S 60   // after a touch, stay on this long even if the PC is off
#define WAKE_TIMEOUT_S  180       // WoL: give up waiting for the PC after this
#define NO_REPLY_SHOW_S 600       // ...and show "no response" this long
#define ROTATE_PAUSE_MS 60000UL   // auto page flip pauses this long after a touch

// =============================================================================
// State
// =============================================================================

static lv_obj_t *g_root;
// Full-screen pages (one per page_id_t) under the shared header (page icon,
// clock, temperature). Tap the right half of the screen (or BOOT) for the next
// page, the left half for the previous one. On the PC page, while the PC is
// off, hold 1 s to wake it (Wake-on-LAN).
static lv_obj_t *g_pages[PG_COUNT];
static int g_nav[PG_COUNT];       // enabled pages, in display order
static int g_nav_n = 0;
static int g_page = PG_QUOTA;
static uint32_t g_page_since = 0;     // millis() when the current page was shown
static uint32_t g_last_touch = 0;     // millis() of the last touch (pauses auto flip)

static bool g_setup_mode = false;
static bool g_ignore_press = false; // the current press woke the screen -> don't act on it
static lv_obj_t *lbl_clock, *lbl_status;
static lv_image_dsc_t mascot_dsc;

static bool g_has_events = false;   // the events page only shows when there is something to count down to
static bool g_tet_season = false;
static long g_tet_jd = 0;
static int g_tet_year = 0;

// Touch calibration (2 taps: right, then left)
enum { CAL_OFF, CAL_RIGHT, CAL_LEFT, CAL_DONE };
static int g_cal = CAL_OFF;
static int g_cal_ra, g_cal_rb;
static uint32_t g_cal_done_at;

// ---- Data coming from the network task (core 0) ----
// The HTTPS fetches (quota, weather, prices) run in their own FreeRTOS task so
// the screen and touch stay responsive. LVGL is not thread-safe, so the task's
// callbacks only copy the data and raise a flag; loop() (core 1) draws it.
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static claude_quota_t g_q_in;
static weather_t g_w_in;
static market_t g_m_in;
static fuel_t g_f_in;
static volatile bool g_q_dirty = false, g_w_dirty = false, g_m_dirty = false, g_f_dirty = false;
static volatile bool g_market_visible = false;
static volatile int g_net_page = PG_QUOTA;   // page on screen, read by the network task

// =============================================================================
// Quota page
// =============================================================================

static void set_status(const char *msg)
{
    char b[160]; // long enough for the AP setup instructions
    snprintf(b, sizeof(b), "\xE2\x80\xA2 %s", msg); // "• msg"
    lv_label_set_text(lbl_status, b);
}

static void ui_quota(const claude_quota_t &q)
{
    if (q.token_missing) {   // token is entered on the settings page
        char b[64];
        snprintf(b, sizeof(b), "Nhập token: %s/settings", WiFi.localIP().toString().c_str());
        set_status(b);
        return;
    }
    if (!q.valid) {
        set_status((q.http_status == 401 || q.http_status == 403) ? "Token hết hạn" : "Lỗi API");
        return;
    }
    quota_page_update(q, time(nullptr));
    lv_label_set_text(lbl_status, ""); // clear it when things are fine
}

// "12:45" in local time for a UTC epoch
static void fmt_hhmm(time_t t, char *out, size_t n)
{
    t += TZ_OFFSET_SEC;
    struct tm tm;
    gmtime_r(&t, &tm);
    snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

static void ui_weather(const weather_t &w)
{
    weather_page_update(w);

    char st[64], hm[8];
    if (w.updated) fmt_hhmm(w.updated, hm, sizeof(hm));
    if (w.last_http == 200 && w.updated) {
        snprintf(st, sizeof(st), "Cập nhật %s", hm);
        weather_page_set_status(st, false);
    } else if (w.updated) {
        snprintf(st, sizeof(st), "Cập nhật %s, đang thử lại", hm);
        weather_page_set_status(st, true);
    } else {
        snprintf(st, sizeof(st), "Lỗi thời tiết (HTTP %d)", w.last_http);
        weather_page_set_status(st, true);
    }
}

static void on_wifi_status(const char *msg)
{
    if (!lbl_status) return;
    set_status(msg);
    lv_timer_handler();
}

// ---- callbacks running in the network task: copy + flag only ----
static void net_on_quota(const claude_quota_t &q)
{
    portENTER_CRITICAL(&g_mux);
    g_q_in = q;
    g_q_dirty = true;
    portEXIT_CRITICAL(&g_mux);
}

static void net_on_weather(const weather_t &w)
{
    portENTER_CRITICAL(&g_mux);
    g_w_in = w;
    g_w_dirty = true;
    portEXIT_CRITICAL(&g_mux);
}

static void net_on_market(const market_t &m)
{
    portENTER_CRITICAL(&g_mux);
    g_m_in = m;
    g_m_dirty = true;
    portEXIT_CRITICAL(&g_mux);
}

static void net_on_fuel(const fuel_t &f)
{
    portENTER_CRITICAL(&g_mux);
    g_f_in = f;
    g_f_dirty = true;
    portEXIT_CRITICAL(&g_mux);
}

static void show_page(int id, bool save);
static bool page_available(int id);

static void ui_fuel(const fuel_t &f)
{
    market_page_update_fuel(f, time(nullptr), TZ_OFFSET_SEC);
    // New price period: turn the screen on and show the prices (once)
    if (f.just_changed && !g_setup_mode && g_cal == CAL_OFF && page_available(PG_MARKET)) {
        screen_sleep_touch();
        market_page_set_tab(MARKET_TAB_FUEL);
        show_page(PG_MARKET, false);
        g_last_touch = millis();      // hold it there a while even with auto flip on
    }
}

// loop(): draw whatever the network task delivered
static void apply_net_updates()
{
    if (g_q_dirty) {
        claude_quota_t q;
        portENTER_CRITICAL(&g_mux);
        q = g_q_in;
        g_q_dirty = false;
        portEXIT_CRITICAL(&g_mux);
        ui_quota(q);
    }
    if (g_w_dirty) {
        weather_t w;
        portENTER_CRITICAL(&g_mux);
        w = g_w_in;
        g_w_dirty = false;
        portEXIT_CRITICAL(&g_mux);
        ui_weather(w);
    }
    if (g_m_dirty) {
        market_t m;
        portENTER_CRITICAL(&g_mux);
        m = g_m_in;
        g_m_dirty = false;
        portEXIT_CRITICAL(&g_mux);
        market_page_update(m, TZ_OFFSET_SEC);
    }
    if (g_f_dirty) {
        fuel_t f;
        portENTER_CRITICAL(&g_mux);
        f = g_f_in;
        g_f_dirty = false;
        portEXIT_CRITICAL(&g_mux);
        ui_fuel(f);
    }
}

// An HTTPS request needs ~45 KB of heap (TLS buffers) for its whole duration,
// and LVGL draws from the same heap. Don't start one while the heap is low:
// LVGL running out of memory is what froze the screen.
#define TLS_MIN_FREE    50000
#define TLS_MIN_BLOCK   20000

static bool heap_ok_for_tls()
{
    uint32_t free_b = ESP.getFreeHeap();
    uint32_t block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (free_b >= TLS_MIN_FREE && block >= TLS_MIN_BLOCK) return true;
    static uint32_t last_log = 0;
    if (millis() - last_log > 60000UL) {
        last_log = millis();
        Serial.printf("[mem] low (free %u, largest %u): HTTPS fetches paused\n", (unsigned)free_b,
                      (unsigned)block);
    }
    return false;
}

// Only the page on screen fetches its data (and nothing while the screen is
// dark): quota page -> Claude quota, weather page -> weather, prices page ->
// gold / USD / BTC / fuel. PC, lunar calendar, Tết and events pages need no
// HTTPS, so the board makes no requests at all while they are shown. Data that
// went stale while another page was up is fetched as soon as its page opens.
static void net_task(void *)
{
    for (;;) {
        // each *_loop() makes at most one request when one is due
        if (g_setup_mode) {            // setup screen: as before
            if (heap_ok_for_tls()) claude_quota_loop();
            if (heap_ok_for_tls()) weather_loop();
        } else if (!screen_sleep_is_asleep()) {
            switch (g_net_page) {
            case PG_QUOTA:
                if (heap_ok_for_tls()) claude_quota_loop();
                break;
            case PG_WEATHER:
                if (heap_ok_for_tls()) weather_loop();
                break;
            case PG_MARKET:
                if (heap_ok_for_tls()) market_loop(true);
                if (heap_ok_for_tls()) fuel_loop();
                break;
            default:
                break;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// =============================================================================
// PC page (stats while the PC is on, Wake-on-LAN panel while it's off)
// =============================================================================

static void refresh_pc_status()
{
    char b[80];
    if (pc_stats_fresh()) {
        const pc_stats_t &s = pc_stats_get();
        pc_page_set_status(s.host[0] ? s.host : "PC đã kết nối", false);
    } else if (WiFi.status() != WL_CONNECTED) {
        pc_page_set_status("Mất WiFi", true);
    } else if (!wol_has_mac()) {
        snprintf(b, sizeof(b), "Chạy pc_monitor.py -> %s", WiFi.localIP().toString().c_str());
        pc_page_set_status(b, true);
    } else {
        // hint: the PC can also be woken from any browser on the same WiFi
        snprintf(b, sizeof(b), "Bật máy từ xa: %s/wol", WiFi.localIP().toString().c_str());
        pc_page_set_status(b, false);
    }
}

static void refresh_wol(int hold_pct)
{
    const pc_stats_t &pc = pc_stats_get();
    wol_view_t v;
    v.host = pc.host;
    v.mac = wol_mac_str();
    v.nic_wifi = wol_nic_wifi();
    v.hold_pct = hold_pct;
    v.sent_ago_s = 0;

    uint32_t sent = wol_last_sent_ms();
    if (sent) v.sent_ago_s = (millis() - sent) / 1000;

    if (!wol_has_mac())                               v.state = WOL_NO_MAC;
    else if (pc_stats_fresh())                        v.state = WOL_ONLINE;
    else if (sent && v.sent_ago_s < WAKE_TIMEOUT_S)   v.state = WOL_WAKING;
    else if (sent && v.sent_ago_s < NO_REPLY_SHOW_S)  v.state = WOL_NO_REPLY;
    else                                              v.state = WOL_OFFLINE;
    wol_panel_update(v);
}

static void on_pc(const pc_stats_t &s) // web server -> runs in loop(), may draw
{
    screen_sleep_pc_activity();
    pc_page_show_wol(false);
    pc_page_update(s, true);
    refresh_pc_status();
}

// PC stopped sending (asleep / off / script closed): blank the stats and show
// the Wake-on-LAN panel instead.
static void check_pc_stale()
{
    static uint32_t last = 0;
    static bool was_fresh = true; // forces one refresh at startup
    if (millis() - last < 1000) return;
    last = millis();
    bool fresh = pc_stats_fresh();
    if (fresh != was_fresh || !fresh) {
        if (!fresh) {
            pc_page_update(pc_stats_get(), false);
            refresh_wol(-1);
        }
        pc_page_show_wol(!fresh);
        refresh_pc_status();
    }
    was_fresh = fresh;
}

static void do_wake()
{
    if (!wol_has_mac()) return;           // the panel already explains what to do
    if (!wol_send()) pc_page_set_status("Không có WiFi, chưa gửi được", true);
    refresh_wol(-1);
}

// Called every loop while the WoL panel is visible: hold progress (fast) +
// state such as "Đang bật... 14 giây" (once a second).
static void tick_wol_panel(bool ignore_hold)
{
    static uint32_t last = 0;
    static int last_hold = -1;
    uint32_t held = ignore_hold ? 0 : touch_held_ms();
    int hold = held ? (int)(held * 100 / TOUCH_LONG_MS) : -1;
    if (hold > 100) hold = 100;
    // ignore the first ~150 ms so a normal tap (page switch) doesn't flash the ring
    if (held && held < 150) hold = -1;
    if (hold != last_hold || millis() - last >= 1000) {
        last_hold = hold;
        last = millis();
        refresh_wol(hold);
    }
}

static bool wol_panel_visible() { return g_page == PG_PC && !pc_stats_fresh(); }

// =============================================================================
// Page navigation
// =============================================================================

static bool page_available(int id)
{
    if (!(settings_get().enabled & (1 << id))) return false;
    if (id == PG_TET && !g_tet_season) return false;
    if (id == PG_EVENTS && !g_has_events) return false;
    if (id == PG_FUEL) return false;   // now the "Xăng" tab of the prices page
    return true;
}

static int nav_index(int id)
{
    for (int i = 0; i < g_nav_n; i++)
        if (g_nav[i] == id) return i;
    return -1;
}

// Full-screen pages: weather (its own big clock), prices (title + tabs), lunar
// calendar and PC. Hide the shared top clock there.
static void apply_chrome(int id)
{
    bool full = id == PG_WEATHER || id == PG_MARKET || id == PG_LUNAR || id == PG_PC;
    if (!g_setup_mode) {
        if (full) {
            lv_obj_add_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void show_page(int id, bool save)
{
    g_page = id;
    g_page_since = millis();
    for (int i = 0; i < PG_COUNT; i++) {
        if (i == id) lv_obj_clear_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(g_pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    apply_chrome(id);
    if (id == PG_WEATHER) {
        time_t now = time(nullptr);
        if (now > 1000000000) weather_page_tick(now + TZ_OFFSET_SEC);
    }
    g_market_visible = id == PG_MARKET;
    g_net_page = id;                  // the network task fetches only this page's data
    if (wol_panel_visible()) refresh_wol(-1);
    if (save) { // remember across reboots
        Preferences p;
        p.begin("ui", false);
        p.putUChar("page", (uint8_t)id);
        p.end();
    }
}

// Recompute the list of visible pages (settings, Tet season).
static void rebuild_nav()
{
    const settings_t &s = settings_get();
    g_nav_n = 0;
    for (int i = 0; i < PG_COUNT; i++)
        if (page_available(s.order[i])) g_nav[g_nav_n++] = s.order[i];
    if (!g_nav_n) g_nav[g_nav_n++] = PG_QUOTA;

    show_page(nav_index(g_page) >= 0 ? g_page : g_nav[0], false);
}

// The prices page's tabs (Vàng · Tỷ giá / Xăng) are steps of the paging: right
// moves to the next tab, then on to the next page; left goes back the same way.
static void go_page(int dir, bool save = true)
{
    if (g_page == PG_MARKET) {
        int tab = market_page_tab() + dir;
        if (tab >= 0 && tab < MARKET_TAB_N) {
            market_page_set_tab(tab);
            g_page_since = millis();   // auto flip: each tab gets the full time
            return;
        }
    }
    int idx = nav_index(g_page);
    if (idx < 0) idx = 0;
    idx = (idx + dir + g_nav_n) % g_nav_n;
    if (g_nav[idx] == PG_MARKET)       // enter on the first tab going forward, the last going back
        market_page_set_tab(dir < 0 ? MARKET_TAB_N - 1 : MARKET_TAB_GOLD);
    show_page(g_nav[idx], save);
}

// Auto page flip (settings: "Tự chuyển trang"). Not saved to flash (would wear
// it out); paused after a touch, while dark, calibrating or holding.
static void auto_rotate()
{
    uint8_t sec = settings_get().rotate_sec;
    if (!sec || g_nav_n < 2 || g_cal != CAL_OFF) return;
    if (screen_sleep_is_asleep() || touch_is_down()) return;
    uint32_t now = millis();
    if (g_last_touch && now - g_last_touch < ROTATE_PAUSE_MS) return;
    if (now - g_page_since < sec * 1000UL) return;
    go_page(1, false);
}

static int load_saved_page()
{
    Preferences p;
    p.begin("ui", false); // RW so the namespace exists on first boot
    int page = p.getUChar("page", PG_QUOTA);
    p.end();
    return page < PG_COUNT ? page : PG_QUOTA;
}

// =============================================================================
// Clock, lunar calendar, Tet season
// =============================================================================

static void update_tet(long today_jd, bool force)
{
    int ly = 0;
    long tj = lunar_tet_jd(today_jd, ly);
    g_tet_jd = tj;
    g_tet_year = ly;
    long days = tj - today_jd;   // -2..0 during Mung 1-3
    uint8_t before = settings_get().tet_days;
    bool season = tj && before > 0 && days >= -2 && days <= before;
    if (season != g_tet_season || force) {
        g_tet_season = season;
        rebuild_nav();
    }
}

static void update_events(long today_jd, bool force)
{
    events_page_update(today_jd);
    event_next_t first;
    bool has = events_upcoming(today_jd, &first, 1) > 0;
    if (has != g_has_events || force) {
        g_has_events = has;
        rebuild_nav();
    }
}

static long today_jd_local()
{
    time_t now = time(nullptr);
    if (now < 1000000000) return 0;
    time_t local = now + TZ_OFFSET_SEC;
    struct tm tm;
    gmtime_r(&local, &tm);
    return jd_from_solar(tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
}

static void update_clock()
{
    time_t now = time(nullptr);
    if (now < 1000000000) return;
    time_t local = now + TZ_OFFSET_SEC;
    struct tm tm;
    gmtime_r(&local, &tm);

    static time_t last_sec = 0;
    if (local != last_sec) { // Tet countdown ticks every second while shown
        last_sec = local;
        if (g_page == PG_TET) tet_page_update(local, g_tet_jd, g_tet_year);
        if (g_page == PG_WEATHER) weather_page_tick(local);
    }

    static int last_min = -1;
    if (tm.tm_min == last_min) return;
    last_min = tm.tm_min;

    char b[8];
    snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min);
    lv_label_set_text(lbl_clock, b);
    quota_page_tick(now);   // reset countdown + time marker on the quota page

    // New day -> lunar calendar + Tet countdown
    static int last_yday = -1;
    if (tm.tm_yday != last_yday) {
        last_yday = tm.tm_yday;
        int d = tm.tm_mday, m = tm.tm_mon + 1, y = tm.tm_year + 1900;
        lunar_page_update(d, m, y);
        update_tet(jd_from_solar(d, m, y), false);
        update_events(jd_from_solar(d, m, y), false);
        tet_page_update(local, g_tet_jd, g_tet_year);
    }
}

// =============================================================================
// Settings + touch calibration
// =============================================================================

static void apply_settings()
{
    const settings_t &s = settings_get();
    weather_set_location(s.lat, s.lon);
    weather_page_set_city(s.city);
    set_brightness((uint8_t)(s.brightness * 255 / 100));
    if (!g_setup_mode) {
        screen_sleep_begin(s.sleep_min * 60UL * 1000UL, SCREEN_TOUCH_AWAKE_S * 1000UL);
        screen_sleep_touch(); // keep the screen on while the user is changing things
    }
    // Pages / Tet window / events may have changed
    long today = today_jd_local();
    if (today) {
        update_tet(today, false);
        update_events(today, true);   // also rebuilds the page list
    } else {
        rebuild_nav();
    }
}

// New Claude token saved on /settings: show the quota page, whose fetch then
// tries the token right away (claude_quota_token_changed()).
static void on_token_saved()
{
    if (g_setup_mode) return;
    set_status("Đang kiểm tra token...");
    if (!page_available(PG_QUOTA)) return;
    screen_sleep_touch();
    show_page(PG_QUOTA, false);
    g_last_touch = millis();   // hold it there a while even with auto flip on
}

static void start_touchcal()
{
    if (g_setup_mode) return;
    screen_sleep_touch();
    g_cal = CAL_RIGHT;
    touchcal_show(TC_RIGHT);
}

// Taps go here while calibrating. BOOT = skip (keeps the default guess).
static void touchcal_tap()
{
    int a, b;
    if (!touch_tap_raw(a, b)) { // BOOT button
        if (!touch_cal_valid()) touch_cal_save(1, 2048, false);
        g_cal = CAL_OFF;
        touchcal_hide();
        return;
    }
    Serial.printf("[touch] cal tap a=%d b=%d\n", a, b);
    if (g_cal == CAL_RIGHT) {
        g_cal_ra = a;
        g_cal_rb = b;
        g_cal = CAL_LEFT;
        touchcal_show(TC_LEFT);
    } else if (g_cal == CAL_LEFT) {
        int da = abs(g_cal_ra - a), db = abs(g_cal_rb - b);
        int axis = db > da ? 1 : 0;
        int r = axis ? g_cal_rb : g_cal_ra, l = axis ? b : a;
        if (abs(r - l) < 400) { // the two taps were too close: try again
            g_cal = CAL_RIGHT;
            touchcal_show(TC_RETRY);
            return;
        }
        touch_cal_save(axis, (r + l) / 2, r > l);
        g_cal = CAL_DONE;
        g_cal_done_at = millis();
        touchcal_show(TC_DONE);
    }
}

// =============================================================================
// setup / loop
// =============================================================================

// ---- Why did the board restart? ----
// Kept in RTC memory, which survives a software restart, watchdog or crash (not
// a power cut): the heap a moment before, uptime, and whether LVGL asked for it.
#define RTC_MAGIC 0xC7D5A11Eu
RTC_NOINIT_ATTR static uint32_t rtc_magic, rtc_free, rtc_block, rtc_min, rtc_up_s, rtc_lvgl;

static void rtc_note_heap()
{
    rtc_free = ESP.getFreeHeap();
    rtc_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    rtc_min = ESP.getMinFreeHeap();
    rtc_up_s = millis() / 1000;
    rtc_magic = RTC_MAGIC;
}

// LV_ASSERT_HANDLER (lv_conf.h): LVGL failed (in practice: out of memory)
extern "C" void cyd_lvgl_assert(void)
{
    rtc_note_heap();
    rtc_lvgl = 1;
    esp_restart();
}

static char g_restart_msg[120] = ""; // shown on screen for a while after an unexpected restart

static void check_last_restart()
{
    esp_reset_reason_t r = esp_reset_reason();
    bool have = rtc_magic == RTC_MAGIC;
    const char *why = nullptr;
    if (have && rtc_lvgl)                                  why = "hết bộ nhớ (LVGL)";
    else if (r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT) why = "treo (watchdog)";
    else if (r == ESP_RST_PANIC)                           why = "lỗi chương trình (panic)";
    else if (r == ESP_RST_BROWNOUT)                        why = "sụt nguồn (brownout)";
    Serial.printf("[boot] reset reason %d%s%s\n", (int)r, why ? ": " : "", why ? why : "");
    if (why && have) {
        snprintf(g_restart_msg, sizeof(g_restart_msg),
                 "Vừa khởi động lại: %s sau %lu phút \xC2\xB7 heap %lu/%lu KB, thấp nhất %lu KB",
                 why, (unsigned long)(rtc_up_s / 60), (unsigned long)(rtc_block / 1024),
                 (unsigned long)(rtc_free / 1024), (unsigned long)(rtc_min / 1024));
        Serial.printf("[boot] %s\n", g_restart_msg);
    } else if (why) {
        snprintf(g_restart_msg, sizeof(g_restart_msg), "Vừa khởi động lại: %s", why);
    }
    rtc_magic = 0;
    rtc_lvgl = 0;
}

static void hide_restart_msg(lv_timer_t *t)
{
    lv_obj_delete((lv_obj_t *)lv_timer_get_user_data(t));
}

static void show_restart_msg()
{
    if (!g_restart_msg[0]) return;
    lv_obj_t *l = lv_label_create(lv_layer_top());
    lv_label_set_text(l, g_restart_msg);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, SCR_W - 2 * MARGIN);
    lv_obj_set_style_text_font(l, &font_vn_14, 0);
    lv_obj_set_style_text_color(l, CL_TEXT, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(l, CL_RED, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(l, 8, 0);
    lv_obj_set_style_pad_all(l, 6, 0);
    lv_obj_align(l, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_timer_t *t = lv_timer_create(hide_restart_msg, 60000, l); // 1 min
    lv_timer_set_repeat_count(t, 1);
}

// Heap log (LVGL objects live in the normal heap): at boot and every 10 min,
// so a slow leak or a low point shows up in the serial monitor.
static void log_heap(const char *when)
{
    Serial.printf("[mem] %s: free %u, min %u, largest block %u\n", when,
                  (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void setup()
{
    Serial.begin(115200);
    check_last_restart();
    settings_begin();
    events_begin();

    led_init();
    display_init();
    set_brightness((uint8_t)(settings_get().brightness * 255 / 100));

    g_root = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(g_root);
    lv_obj_set_size(g_root, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(g_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_root, CL_BG, 0);
    lv_obj_set_style_bg_opa(g_root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_root, CL_TEXT, 0);
    lv_obj_set_style_text_font(g_root, &lv_font_montserrat_16, 0);

    // Page 1: Claude quota
    g_pages[PG_QUOTA] = lv_obj_create(g_root);
    lv_obj_remove_style_all(g_pages[PG_QUOTA]);
    lv_obj_set_size(g_pages[PG_QUOTA], LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(g_pages[PG_QUOTA], LV_OBJ_FLAG_SCROLLABLE);

    // Clawd mascot (top-right) = the quota page's icon. The other pages draw
    // their own icon in the same spot (UI_ICON_CX/CY in ui/ui_common.h).
    mascot_dsc.header.cf = LV_COLOR_FORMAT_RGB565A8;
    mascot_dsc.header.w = CLAWD_STILL_W;
    mascot_dsc.header.h = CLAWD_STILL_H;
    mascot_dsc.header.stride = CLAWD_STILL_W * 2;
    mascot_dsc.data = clawd_still_data;
    mascot_dsc.data_size = CLAWD_STILL_W * CLAWD_STILL_H * 3;
    lv_obj_t *mascot = lv_image_create(g_pages[PG_QUOTA]);
    lv_image_set_src(mascot, &mascot_dsc);
    lv_image_set_scale(mascot, 140); // ~55% -> ~40x26
    // image object keeps the 72x48 source size, the scaled picture is centered in it
    lv_obj_set_pos(mascot, UI_ICON_CX - CLAWD_STILL_W / 2, UI_ICON_CY - CLAWD_STILL_H / 2);

    // Clock - Tiempos serif, top-left
    lbl_clock = lv_label_create(g_root);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, &font_tiempos_34, 0);
    lv_obj_set_style_text_color(lbl_clock, CL_TEXT, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_TOP_LEFT, MARGIN, HEADER_TEXT_Y);


    lv_obj_t *quota_body = quota_page_build(g_pages[PG_QUOTA], TZ_OFFSET_SEC);

    g_pages[PG_PC] = pc_page_create(g_root);
    g_pages[PG_WEATHER] = weather_page_create(g_root);
    g_pages[PG_LUNAR] = lunar_page_create(g_root);
    g_pages[PG_TET] = tet_page_create(g_root);
    g_pages[PG_MARKET] = market_page_create(g_root);
    g_pages[PG_EVENTS] = events_page_create(g_root);
    g_pages[PG_FUEL] = lv_obj_create(g_root);    // merged into PG_MARKET ("Xăng" tab), never shown
    lv_obj_remove_style_all(g_pages[PG_FUEL]);
    weather_page_set_status("Đang tải...", false);
    weather_page_set_city(settings_get().city);

    touchcal_create(g_root);

    lbl_status = lv_label_create(g_pages[PG_QUOTA]);
    lv_obj_set_style_text_font(lbl_status, &font_vn_16, 0);
    lv_obj_set_style_text_color(lbl_status, CL_ACCENT, 0);
    lv_obj_align(lbl_status, LV_ALIGN_BOTTOM_MID, 0, -3);
    set_status("Đang khởi động");

    // No WiFi saved yet -> hide the dashboard and show only the
    // setup instructions (driven by the same status callbacks below).
    // (A missing Claude token is not setup: the quota page asks for it, see ui_quota().)
    bool needs_setup = !wifi_manager_has_credentials();
    g_setup_mode = needs_setup;
    g_page = needs_setup ? PG_QUOTA : load_saved_page();
    rebuild_nav();
    if (needs_setup) {
        lv_obj_add_flag(mascot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(lbl_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(quota_body, LV_OBJ_FLAG_HIDDEN);
        show_page(PG_QUOTA, false);

        lv_obj_t *lbl_title = lv_label_create(g_root);
        lv_label_set_text(lbl_title, "Cài đặt");
        lv_obj_set_style_text_font(lbl_title, &font_vn_22, 0);
        lv_obj_set_style_text_color(lbl_title, CL_TEXT, 0);
        lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, -50);

        lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_WRAP);
        lv_obj_set_width(lbl_status, CARD_W);
        lv_obj_set_style_text_align(lbl_status, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl_status, LV_ALIGN_CENTER, 0, 10);
    }

    lv_timer_handler();
    turn_on_display();

    wifi_manager_set_status_callback(on_wifi_status);
    wifi_manager_begin("ESP32-CYD-Setup");

    claude_quota_set_callback(net_on_quota);
    claude_quota_begin();

    weather_set_callback(net_on_weather);
    weather_begin(settings_get().lat, settings_get().lon);

    market_set_callback(net_on_market);
    market_begin();

    fuel_set_callback(net_on_fuel);
    fuel_begin();
    market_page_update_fuel(fuel_get(), 0, TZ_OFFSET_SEC); // last known prices until the first fetch

    pc_stats_set_callback(on_pc);
    pc_stats_begin();
    wol_begin();

    settings_set_changed_callback(apply_settings);
    settings_set_touchcal_callback(start_touchcal);
    settings_set_token_callback(on_token_saved);
    settings_register_routes();

    touch_init();
    if (!needs_setup) {
        screen_sleep_begin(settings_get().sleep_min * 60UL * 1000UL, SCREEN_TOUCH_AWAKE_S * 1000UL);
        if (!touch_cal_valid()) start_touchcal(); // first boot with left/right paging
    }

    // HTTPS fetching on core 0 (WiFi's core); UI + touch + web server stay on core 1.
    xTaskCreatePinnedToCore(net_task, "net", 12288, nullptr, 1, nullptr, 0);
    log_heap("boot");
    show_restart_msg();

    // Watchdog on loop(): if the UI loop ever hangs for 20 s, restart instead of
    // staying frozen (loop() normally runs every ~5-40 ms; the web server can
    // block it for up to ~5 s).
    esp_task_wdt_config_t wdt = {};
    wdt.timeout_ms = 20000;
    wdt.idle_core_mask = 0;        // only loop(): core 0 (WiFi + HTTPS) may be busy for a while
    wdt.trigger_panic = true;
    esp_task_wdt_reconfigure(&wdt);
    enableLoopWDT();
}

void loop()
{
    static uint32_t last_heap_log = 0, last_rtc_note = 0;
    if (millis() - last_heap_log > 600000UL) {
        last_heap_log = millis();
        log_heap("loop");
    }
    if (millis() - last_rtc_note > 2000UL) { // heap just before a watchdog / crash restart
        last_rtc_note = millis();
        rtc_note_heap();
    }
    wifi_manager_loop();     // web server: /pc, /wol, /settings (handlers may draw)
    apply_net_updates();
    update_clock();
    check_pc_stale();

    if (!g_setup_mode) {
        touch_event_t ev = touch_poll();
        bool down = touch_is_down();

        // Any touch keeps the screen on; a touch that wakes it is ignored
        // until released (no page switch, no Wake-on-LAN).
        if (down || ev != TOUCH_NONE) {
            if (screen_sleep_touch()) g_ignore_press = true;
            g_last_touch = millis();
        }
        if (g_ignore_press) {
            ev = TOUCH_NONE;
            if (!down) g_ignore_press = false;
        }

        if (g_cal == CAL_DONE && millis() - g_cal_done_at > 2500) {
            g_cal = CAL_OFF;
            touchcal_hide();
        }

        if (g_cal == CAL_RIGHT || g_cal == CAL_LEFT) {
            if (ev == TOUCH_TAP) touchcal_tap();
        } else if (ev == TOUCH_TAP) {
            go_page(touch_tap_side() < 0 ? -1 : 1); // left half: back, right half / BOOT: next
        } else if (ev == TOUCH_LONG && wol_panel_visible()) {
            do_wake();
        }
        if (wol_panel_visible() && g_cal == CAL_OFF) tick_wol_panel(g_ignore_press);

        auto_rotate();
        screen_sleep_loop();
    }
    lv_timer_handler();
    delay(5);
}
