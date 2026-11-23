#include "wifi_manager.h"
#include "../web/web_ui.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>

// Hold this pin LOW at boot -> wipes the WiFi config and opens the config page.
// GPIO0 = BOOT button on the CYD board. Set to -1 to disable this feature.
#define WIFI_RESET_PIN 0

// Max time to wait when trying the saved WiFi (ms).
#define WIFI_CONNECT_TIMEOUT_MS 15000

#define MDNS_HOST "clawd" // reachable at http://clawd.local

static const char *NVS_WIFI = "wifi";

static Preferences prefs;
static WebServer server(80);
static DNSServer dns_server;

static bool server_running = false; // web server is up (AP or STA)
static bool portal_active = false;  // in AP + DNS captive-portal mode
static wifi_status_cb_t status_cb = nullptr;

static bool save_pending = false;
static uint32_t save_at = 0;

// Note: status strings stay plain ASCII (no diacritics) since the default
// LVGL font has no Vietnamese glyphs. The web page uses full Vietnamese.
static void notify(const char *msg)
{
    Serial.printf("[wifi] %s\n", msg);
    if (status_cb) status_cb(msg);
}

void wifi_manager_set_status_callback(wifi_status_cb_t cb) { status_cb = cb; }

WebServer &wifi_manager_server() { return server; }

bool wifi_manager_connected() { return WiFi.status() == WL_CONNECTED; }

bool wifi_manager_has_credentials()
{
    prefs.begin(NVS_WIFI, true); // RO
    bool has = prefs.isKey("ssid") && prefs.getString("ssid", "").length() > 0;
    prefs.end();
    return has;
}

void wifi_manager_reset()
{
    prefs.begin(NVS_WIFI, false);
    prefs.clear();
    prefs.end();
    notify("Đã xóa cấu hình WiFi");
}

// ----- WiFi web page -----
// Only the home WiFi is set here. The Claude token and everything else are on
// /settings (settings/settings.cpp).

#define MAX_NETS 12

static int signal_bars(int rssi) { return rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1; }

static void send_wifi_page()
{
    // Nearby networks, strongest first, one entry per name
    int n = WiFi.scanNetworks();
    int idx[MAX_NETS], k = 0;
    bool used[64] = {};
    while (k < MAX_NETS) {
        int best = -1;
        for (int i = 0; i < n && i < 64; i++) {
            if (used[i] || WiFi.SSID(i).isEmpty()) continue;
            if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
        }
        if (best < 0) break;
        used[best] = true;
        bool dup = false;
        for (int j = 0; j < k; j++) dup |= WiFi.SSID(idx[j]) == WiFi.SSID(best);
        if (!dup) idx[k++] = best;
    }

    bool sta = wifi_manager_connected();
    HtmlOut o(server);
    web_page_begin(o, "WiFi", portal_active ? WEB_NAV_NONE : WEB_NAV_WIFI);

    if (portal_active) {
        o << F("<h1>Kết nối WiFi</h1><p class='hint' style='margin:-6px 0 16px;font-size:14px'>"
               "Chọn WiFi ở nhà để board kết nối. Sau khi board khởi động lại, mở "
               "<b>http://clawd.local/settings</b> (hoặc IP hiện trên màn hình) để nhập Claude token "
               "và chỉnh các trang.</p>");
    } else {
        o << F("<h1>WiFi</h1>");
    }

    if (sta) {
        o << F("<section class='card' aria-labelledby='cur'><h2 id='cur'><span class='ic' aria-hidden='true'>&#x1F4F6;</span>"
               "Đang kết nối<span class='pill ok'>Online</span></h2><dl class='kv'><dt>Mạng</dt><dd>");
        o.esc(WiFi.SSID());
        o << F("</dd><dt>IP</dt><dd>") << WiFi.localIP().toString() << F("</dd><dt>Tín hiệu</dt><dd>")
          << (int)WiFi.RSSI() << F(" dBm</dd><dt>Địa chỉ</dt><dd>http://clawd.local</dd></dl></section>");
    }

    o << F("<form method='POST' action='/save' class='card' autocomplete='off'><h2><span class='ic' aria-hidden='true'>&#x1F3E0;</span>")
      << (sta ? F("Đổi mạng WiFi") : F("Mạng WiFi"))
      << F("</h2><input type='hidden' name='n' value='") << web_nonce() << F("'>");

    if (k) {
        o << F("<ul class='nets' aria-label='Mạng xung quanh'>");
        for (int j = 0; j < k; j++) {
            int i = idx[j];
            String name = web_esc(WiFi.SSID(i));
            int bars = signal_bars(WiFi.RSSI(i));
            o << F("<li><button type='button' class='net' aria-pressed='false' data-s='") << name
              << F("'><span class='bars' role='img' aria-label='Sóng ") << bars << F("/4'>");
            for (int b = 1; b <= 4; b++) o << (b <= bars ? "<i class='on'></i>" : "<i></i>");
            o << F("</span><span class='n'>") << name << F("</span>");
            if (WiFi.encryptionType(i) != WIFI_AUTH_OPEN) o << F("<span class='lock'>có mật khẩu</span>");
            o << F("</button></li>");
        }
        o << F("</ul><p class='hint'><a href='/wifi'>Quét lại</a> · không thấy mạng của bạn? Gõ tên bên dưới.</p>");
    } else {
        o << F("<p class='hint'>Không tìm thấy mạng nào. <a href='/wifi'>Quét lại</a> hoặc gõ tên bên dưới.</p>");
    }

    o << F("<label for='ssid'>Tên WiFi (SSID)</label>"
           "<input id='ssid' name='ssid' required maxlength='32' autocapitalize='off' spellcheck='false'>"
           "<label for='pass'>Mật khẩu</label><div class='pw'>"
           "<input id='pass' name='pass' type='password' maxlength='64' autocomplete='new-password'>"
           "<button type='button' data-pw='pass' aria-label='Hiện mật khẩu'>Hiện</button></div>"
           "<p class='hint'>Board chỉ dùng WiFi 2,4 GHz.</p>"
           "<div style='margin-top:16px'><button class='btn' type='submit'>Lưu &amp; khởi động lại</button></div></form>");

    web_page_end(o,
        "document.querySelectorAll('.net').forEach(function(b){b.onclick=function(){"
        "document.querySelectorAll('.net').forEach(function(x){x.setAttribute('aria-pressed','false')});"
        "b.setAttribute('aria-pressed','true');ssid.value=b.dataset.s;pass.focus()}});"
        "document.querySelectorAll('[data-pw]').forEach(function(b){b.onclick=function(){"
        "var i=document.getElementById(b.dataset.pw),h=i.type=='password';i.type=h?'text':'password';"
        "b.textContent=h?'Ẩn':'Hiện';b.setAttribute('aria-label',h?'Ẩn mật khẩu':'Hiện mật khẩu')}});");
    WiFi.scanDelete();
}

static void handle_root()
{
    // Setup (captive portal): the WiFi page. On the home network: settings.
    if (portal_active) {
        send_wifi_page();
        return;
    }
    server.sendHeader("Location", "/settings", true);
    server.send(302, "text/plain", "");
}

static void handle_save()
{
    if (!web_nonce_ok(server)) return;
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    ssid.trim();

    if (ssid.isEmpty() || ssid.length() > 32 || pass.length() > 64) {
        web_send_message(server, 400, "Thiếu tên WiFi", "Nhập tên WiFi (tối đa 32 ký tự) rồi thử lại.",
                         "/wifi", "Quay lại");
        return;
    }

    prefs.begin(NVS_WIFI, false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    String msg = F("Board đang khởi động lại và kết nối vào <b>");
    msg += web_esc(ssid);
    msg += F("</b>. Sau khoảng 20 giây, mở <b>http://clawd.local/settings</b> "
             "(hoặc IP hiện trên màn hình) để nhập Claude token.");
    web_send_message(server, 200, "Đã lưu WiFi", msg.c_str(), nullptr, nullptr);

    save_pending = true;
    save_at = millis();
}

static void handle_not_found()
{
    // Captive portal: redirect every request to the config page.
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    server.send(302, "text/plain", "");
}

static void start_web_server(bool captive)
{
    web_ui_register(server);
    server.on("/", HTTP_GET, handle_root);
    server.on("/wifi", HTTP_GET, send_wifi_page);
    server.on("/save", HTTP_POST, handle_save);
    if (captive) server.onNotFound(handle_not_found);
    server.begin();
    server_running = true;
}

// ----- Connect / portal -----

static bool try_connect(const String &ssid, const String &pass, uint32_t timeout_ms)
{
    if (ssid.isEmpty()) return false;

    notify("Đang kết nối WiFi đã lưu...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
        delay(200);
    }
    return WiFi.status() == WL_CONNECTED;
}

static void start_portal(const char *ap_ssid, const char *ap_pass)
{
    WiFi.mode(WIFI_AP_STA);
    bool secured = ap_pass && strlen(ap_pass) >= 8;
    WiFi.softAP(ap_ssid, secured ? ap_pass : nullptr);
    delay(100);

    IPAddress ip = WiFi.softAPIP(); // usually 192.168.4.1

    dns_server.setErrorReplyCode(DNSReplyCode::NoError);
    dns_server.start(53, "*", ip);
    start_web_server(true);
    portal_active = true;

    char buf[128];
    snprintf(buf, sizeof(buf), "Vào WiFi \"%s\" rồi mở http://%s",
             ap_ssid, ip.toString().c_str());
    notify(buf);
}

void wifi_manager_begin(const char *ap_ssid, const char *ap_pass)
{
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

#if WIFI_RESET_PIN >= 0
    pinMode(WIFI_RESET_PIN, INPUT_PULLUP);
    delay(10);
    bool force_portal = (digitalRead(WIFI_RESET_PIN) == LOW);
    if (force_portal) {
        notify("Đang giữ nút BOOT -> xóa cấu hình");
        wifi_manager_reset();
    }
#else
    bool force_portal = false;
#endif

    String ssid, pass;
    if (!force_portal) {
        // RW so the namespace gets created on first run (avoids a NOT_FOUND log).
        prefs.begin(NVS_WIFI, false);
        ssid = prefs.getString("ssid", "");
        pass = prefs.getString("pass", "");
        prefs.end();
    }

    if (!force_portal && try_connect(ssid, pass, WIFI_CONNECT_TIMEOUT_MS)) {
        configTime(0, 0, "pool.ntp.org", "time.google.com"); // UTC, for reset-time math
        if (MDNS.begin(MDNS_HOST)) MDNS.addService("http", "tcp", 80);
        start_web_server(false); // /settings (token, pages...) and /wifi stay reachable on the LAN

        char buf[80];
        snprintf(buf, sizeof(buf), "Đã kết nối: %s", WiFi.localIP().toString().c_str());
        notify(buf);
        return;
    }

    notify("Chưa có WiFi -> bật chế độ cài đặt");
    start_portal(ap_ssid, ap_pass);
}

void wifi_manager_loop()
{
    if (portal_active) dns_server.processNextRequest();
    if (server_running) server.handleClient();

    // Restart once the "saved" page has been served to the browser.
    if (save_pending && millis() - save_at > 1500) {
        save_pending = false;
        notify("Đang khởi động lại...");
        delay(200);
        ESP.restart();
    }
}
