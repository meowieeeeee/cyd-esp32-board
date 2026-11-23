#include "wol.h"
#include "../wifi_manager/wifi_manager.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>

static uint8_t g_mac[6];
static bool g_has_mac = false;
static char g_mac_str[18] = "";
static uint32_t g_last_sent = 0;
static bool g_nic_wifi = false;

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "AA:BB:CC:DD:EE:FF", "aa-bb-cc-dd-ee-ff" or "aabbccddeeff"
static bool parse_mac(const char *s, uint8_t out[6])
{
    int n = 0, hi = -1;
    for (; *s && n < 6; s++) {
        if (*s == ':' || *s == '-' || *s == '.' || *s == ' ') continue;
        int v = hexval(*s);
        if (v < 0) return false;
        if (hi < 0) hi = v;
        else { out[n++] = (uint8_t)(hi << 4 | v); hi = -1; }
    }
    if (n != 6 || hi >= 0) return false;
    for (; *s; s++) if (*s != ' ') return false; // trailing junk
    bool all_zero = true;
    for (int i = 0; i < 6; i++) if (out[i]) all_zero = false;
    return !all_zero;
}

static void update_str()
{
    snprintf(g_mac_str, sizeof(g_mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
}

bool wol_has_mac() { return g_has_mac; }
const char *wol_mac_str() { return g_has_mac ? g_mac_str : ""; }
uint32_t wol_last_sent_ms() { return g_last_sent; }
void wol_set_nic_wifi(bool wifi) { g_nic_wifi = wifi; }
bool wol_nic_wifi() { return g_nic_wifi; }

bool wol_set_mac(const char *s)
{
    uint8_t m[6];
    if (!parse_mac(s, m)) return false;
    if (g_has_mac && memcmp(m, g_mac, 6) == 0) return true; // unchanged -> no flash write
    memcpy(g_mac, m, 6);
    g_has_mac = true;
    update_str();
    Preferences p;
    p.begin("wol", false);
    p.putBytes("mac", g_mac, 6);
    p.end();
    Serial.printf("[wol] saved MAC %s\n", g_mac_str);
    return true;
}

bool wol_send()
{
    if (!g_has_mac || WiFi.status() != WL_CONNECTED) return false;

    uint8_t pkt[102];
    memset(pkt, 0xFF, 6);
    for (int i = 0; i < 16; i++) memcpy(pkt + 6 + i * 6, g_mac, 6);

    // Directed broadcast of our subnet (e.g. 192.168.1.255) + global broadcast.
    IPAddress ip = WiFi.localIP(), mask = WiFi.subnetMask();
    IPAddress bcast(ip[0] | (uint8_t)~mask[0], ip[1] | (uint8_t)~mask[1],
                    ip[2] | (uint8_t)~mask[2], ip[3] | (uint8_t)~mask[3]);
    IPAddress targets[2] = {bcast, IPAddress(255, 255, 255, 255)};

    WiFiUDP udp;
    for (int rep = 0; rep < 3; rep++) {
        for (IPAddress &t : targets) {
            udp.beginPacket(t, 9);
            udp.write(pkt, sizeof(pkt));
            udp.endPacket();
        }
        delay(20);
    }
    g_last_sent = millis();
    if (!g_last_sent) g_last_sent = 1;
    Serial.printf("[wol] magic packet -> %s (%s)\n", g_mac_str, bcast.toString().c_str());
    return true;
}

static void handle_wol()
{
    WebServer &s = wifi_manager_server();
    if (s.hasArg("mac") && !wol_set_mac(s.arg("mac").c_str())) {
        s.send(400, "text/plain", "bad mac");
        return;
    }
    if (!g_has_mac) {
        s.send(409, "text/plain", "no MAC yet - run pc_monitor.py once or use /wol?mac=AA:BB:CC:DD:EE:FF");
        return;
    }
    bool ok = wol_send();
    s.send(ok ? 200 : 503, "text/plain", ok ? (String("sent to ") + g_mac_str) : String("no WiFi"));
}

void wol_begin()
{
    Preferences p;
    p.begin("wol", false); // RW so the namespace exists on first boot
    if (p.getBytes("mac", g_mac, 6) == 6) {
        g_has_mac = true;
        update_str();
    }
    p.end();
    wifi_manager_server().on("/wol", HTTP_ANY, handle_wol);
}
