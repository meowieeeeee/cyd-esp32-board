#include "pc_stats.h"
#include "../wifi_manager/wifi_manager.h"
#include "../wol/wol.h"
#include "../market/market.h"
#include <Arduino.h>
#include <WebServer.h>
#include <math.h>

static pc_stats_t g_pc;
static pc_stats_cb_t g_cb = nullptr;

void pc_stats_set_callback(pc_stats_cb_t cb) { g_cb = cb; }
const pc_stats_t &pc_stats_get() { return g_pc; }

bool pc_stats_fresh()
{
    return g_pc.valid && (millis() - g_pc.rx_ms) < PC_STALE_MS;
}

static float arg_f(WebServer &s, const char *key)
{
    if (!s.hasArg(key)) return NAN;
    String v = s.arg(key);
    v.trim();
    if (v.isEmpty()) return NAN;
    return v.toFloat();
}

static void handle_pc()
{
    WebServer &s = wifi_manager_server();

    g_pc.cpu       = arg_f(s, "cpu");
    g_pc.cpu_temp  = arg_f(s, "cpu_temp");
    g_pc.gpu       = arg_f(s, "gpu");
    g_pc.gpu_temp  = arg_f(s, "gpu_temp");
    g_pc.ram_used  = arg_f(s, "ram_used");
    g_pc.ram_total = arg_f(s, "ram_total");
    g_pc.vram_used  = arg_f(s, "vram_used");
    g_pc.vram_total = arg_f(s, "vram_total");
    g_pc.cpu_clock  = arg_f(s, "cpu_clock");
    g_pc.gpu_clock  = arg_f(s, "gpu_clock");
    g_pc.cpu_power  = arg_f(s, "cpu_power");
    g_pc.gpu_power  = arg_f(s, "gpu_power");
    if (s.hasArg("host")) s.arg("host").toCharArray(g_pc.host, sizeof(g_pc.host));
    if (s.hasArg("cpu_name")) s.arg("cpu_name").toCharArray(g_pc.cpu_name, sizeof(g_pc.cpu_name));
    if (s.hasArg("gpu_name")) s.arg("gpu_name").toCharArray(g_pc.gpu_name, sizeof(g_pc.gpu_name));

    // Wake-on-LAN learns the PC's MAC from the script (saved to NVS only when it changes).
    if (s.hasArg("mac")) wol_set_mac(s.arg("mac").c_str());
    if (s.hasArg("nic")) wol_set_nic_wifi(s.arg("nic") == "wifi");

    // USD/VND the script fetched from Vietcombank's JSON API (the board's own
    // requests to Vietcombank are often stalled by its bot filter)
    if (s.hasArg("usd_buy") && s.hasArg("usd_sell"))
        market_push_usd(s.arg("usd_buy").toFloat(), s.arg("usd_sell").toFloat(),
                        (time_t)s.arg("usd_t").toInt());

    g_pc.valid = true;
    g_pc.rx_ms = millis();

    s.send(200, "text/plain", "ok");
    if (g_cb) g_cb(g_pc);
}

void pc_stats_begin()
{
    memset(&g_pc, 0, sizeof(g_pc));
    g_pc.cpu = g_pc.cpu_temp = g_pc.gpu = g_pc.gpu_temp = NAN;
    g_pc.ram_used = g_pc.ram_total = g_pc.vram_used = g_pc.vram_total = NAN;
    g_pc.cpu_clock = g_pc.gpu_clock = g_pc.cpu_power = g_pc.gpu_power = NAN;

    // HTTP_ANY so both the script's POST and a quick GET test from a browser/curl work.
    wifi_manager_server().on("/pc", HTTP_ANY, handle_pc);
}
