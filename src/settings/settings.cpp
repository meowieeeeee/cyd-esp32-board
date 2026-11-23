#include "settings.h"
#include "../wifi_manager/wifi_manager.h"
#include "../events/events.h"
#include "../claude_quota/claude_quota.h"
#include "../claude_quota/claude_token_store.h"
#include "../web/web_ui.h"
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>

static settings_t g_s;
static settings_cb_t g_changed = nullptr, g_touchcal = nullptr, g_token_cb = nullptr;

static const char *PAGE_NAMES[PG_COUNT] = {
    "Quota Claude", "Máy tính (PC + bật máy từ xa)", "Thời tiết", "Lịch Âm",
    "Đếm ngược Tết", "Giá cả (vàng / xăng / tỷ giá)", "Sự kiện của bạn", "Giá xăng dầu",
};

// Default order (also used to slot in pages added by a firmware update)
static const uint8_t DEFAULT_ORDER[PG_COUNT] = {PG_QUOTA, PG_PC,     PG_WEATHER, PG_LUNAR,
                                                PG_TET,   PG_EVENTS, PG_MARKET,  PG_FUEL};

struct city_t { const char *name; float lat, lon; };
static const city_t CITIES[] = {
    {"TP. Hồ Chí Minh", 10.8231f, 106.6297f}, {"Hà Nội", 21.0285f, 105.8542f},
    {"Đà Nẵng", 16.0544f, 108.2022f},         {"Hải Phòng", 20.8449f, 106.6881f},
    {"Cần Thơ", 10.0452f, 105.7469f},         {"Huế", 16.4637f, 107.5909f},
    {"Nha Trang", 12.2388f, 109.1967f},       {"Đà Lạt", 11.9404f, 108.4583f},
    {"Vũng Tàu", 10.3460f, 107.0843f},        {"Quy Nhơn", 13.7820f, 109.2196f},
    {"Buôn Ma Thuột", 12.6667f, 108.0500f},   {"Biên Hòa", 10.9574f, 106.8427f},
    {"Thủ Dầu Một", 10.9804f, 106.6519f},     {"Hạ Long", 20.9599f, 107.0425f},
    {"Vinh", 18.6796f, 105.6813f},            {"Thái Nguyên", 21.5942f, 105.8480f},
};

const settings_t &settings_get() { return g_s; }
void settings_set_changed_callback(settings_cb_t cb) { g_changed = cb; }
void settings_set_touchcal_callback(settings_cb_t cb) { g_touchcal = cb; }
void settings_set_token_callback(settings_cb_t cb) { g_token_cb = cb; }
const char *settings_page_name(int id) { return id >= 0 && id < PG_COUNT ? PAGE_NAMES[id] : ""; }

static void defaults()
{
    g_s.lat = 10.8231f;
    g_s.lon = 106.6297f;
    strlcpy(g_s.city, "TP. Hồ Chí Minh", sizeof(g_s.city));
    g_s.sleep_min = 5;
    g_s.brightness = 100;
    g_s.tet_days = 30;
    g_s.rotate_sec = 0;
    memcpy(g_s.order, DEFAULT_ORDER, PG_COUNT);
    g_s.enabled = (1 << PG_COUNT) - 1;
}

// The first n entries hold every page id < n exactly once.
static bool order_ok(const uint8_t *o, int n)
{
    uint16_t seen = 0;
    for (int i = 0; i < n; i++) {
        if (o[i] >= n || (seen & (1 << o[i]))) return false;
        seen |= 1 << o[i];
    }
    return true;
}

// Order saved by an older firmware with fewer pages: insert each new page
// after the page that precedes it in DEFAULT_ORDER.
static void migrate_order(uint8_t *o, int n)
{
    while (n < PG_COUNT) {
        uint8_t id = n;                 // ids are added in order
        int pos = n;                    // default: at the end
        for (int k = 0; k < PG_COUNT; k++) {
            if (DEFAULT_ORDER[k] != id) continue;
            for (int b = k - 1; b >= 0; b--) {           // nearest earlier page that exists
                for (int i = 0; i < n; i++)
                    if (o[i] == DEFAULT_ORDER[b]) { pos = i + 1; b = -1; break; }
            }
            break;
        }
        memmove(o + pos + 1, o + pos, n - pos);
        o[pos] = id;
        n++;
    }
}

void settings_begin()
{
    defaults();
    Preferences p;
    p.begin("cfg", false);
    if (p.isKey("lat")) {
        g_s.lat = p.getFloat("lat", g_s.lat);
        g_s.lon = p.getFloat("lon", g_s.lon);
        p.getString("city", g_s.city, sizeof(g_s.city));
        g_s.sleep_min = p.getUChar("sleep", g_s.sleep_min);
        g_s.brightness = p.getUChar("bright", g_s.brightness);
        g_s.tet_days = p.getUChar("tet", g_s.tet_days);
        g_s.rotate_sec = p.getUChar("rotate", g_s.rotate_sec);
        uint8_t o[PG_COUNT];
        size_t n = p.getBytesLength("order");
        if (n >= 1 && n <= PG_COUNT && p.getBytes("order", o, n) == n && order_ok(o, n)) {
            if ((int)n < PG_COUNT) migrate_order(o, n);
            memcpy(g_s.order, o, PG_COUNT);
        }
        uint16_t en = p.isKey("pages16") ? p.getUShort("pages16", g_s.enabled)
                                         : p.getUChar("pages", 0xFF); // older firmware: 8-bit
        // pages that did not exist when the settings were saved start enabled
        for (int id = n ? (int)n : PG_COUNT; id < PG_COUNT; id++) en |= 1 << id;
        g_s.enabled = en;
    }
    p.end();
    if (g_s.brightness < 10 || g_s.brightness > 100) g_s.brightness = 100;
    if (!(g_s.enabled & ((1 << PG_COUNT) - 1))) g_s.enabled = 1 << PG_QUOTA;
}

static void save()
{
    Preferences p;
    p.begin("cfg", false);
    p.putFloat("lat", g_s.lat);
    p.putFloat("lon", g_s.lon);
    p.putString("city", g_s.city);
    p.putUChar("sleep", g_s.sleep_min);
    p.putUChar("bright", g_s.brightness);
    p.putUChar("tet", g_s.tet_days);
    p.putUChar("rotate", g_s.rotate_sec);
    p.putUShort("pages16", g_s.enabled);
    p.putBytes("order", g_s.order, PG_COUNT);
    p.end();
}

// ---- Web page ----

static void token_card(HtmlOut &o)
{
    String tok = claude_token_load();
    const claude_quota_t &q = claude_quota_get();   // last poll (written by the net task)
    String state;
    const char *cls;
    if (tok.isEmpty())                                  { cls = "warn"; state = F("Chưa có token"); }
    else if (q.valid)                                   { cls = "ok";   state = F("Hoạt động"); }
    else if (q.http_status == 401 || q.http_status == 403) { cls = "err"; state = F("Hết hạn / bị thu hồi"); }
    else if (q.http_status == 0)                        { cls = "";     state = F("Chưa kiểm tra"); }
    else { cls = "err"; state = String(F("Lỗi API (")) + q.http_status + ")"; }

    o << F("<section class='card' aria-labelledby='h-tok'><h2 id='h-tok'><span class='ic' aria-hidden='true'>&#x1F511;</span>"
           "Claude token<span class='pill ") << cls << "'>" << state << F("</span></h2>");
    if (tok.length() > 20) {
        o << F("<p class='mono' style='margin:0 0 10px'>Đang dùng: ");
        o.esc(tok.substring(0, 13));
        o << F("…");
        o.esc(tok.substring(tok.length() - 4));
        o << F("</p>");
    }
    o << F("<label for='token'>") << (tok.isEmpty() ? F("Token") : F("Token mới"))
      << F("</label><div class='pw'><input id='token' name='token' type='password' autocomplete='off' "
           "autocapitalize='off' spellcheck='false' maxlength='256' placeholder='")
      << (tok.isEmpty() ? F("sk-ant-oat01-…") : F("Để trống = giữ token hiện tại"))
      << F("'><button type='button' data-pw='token' aria-label='Hiện token'>Hiện</button></div>"
           "<p class='hint'>Tạo bằng lệnh <code>claude setup-token</code> trên máy có Claude Code "
           "(hiệu lực khoảng 1 năm). Chạy lại lệnh sẽ thu hồi token cũ.</p></section>");
}

static void select_opt(HtmlOut &o, const String &value, const String &text, bool sel)
{
    o << F("<option value='") << value << "'" << (sel ? " selected" : "") << ">" << text << F("</option>");
}

static void handle_get()
{
    WebServer &srv = wifi_manager_server();
    HtmlOut o(srv);
    web_page_begin(o, "Cài đặt", WEB_NAV_SETTINGS);
    o << F("<h1>Cài đặt</h1>");

    if (srv.hasArg("tokerr"))
        o << F("<p class='note err' role='alert'>Token không hợp lệ (phải bắt đầu bằng <code>sk-ant-</code>), "
               "chưa lưu token. Các cài đặt khác đã được lưu.</p>");
    else if (srv.hasArg("tok"))
        o << F("<p class='note ok' role='status'>Đã lưu token mới. Board chuyển sang trang Quota để kiểm tra.</p>");
    else if (srv.hasArg("saved"))
        o << F("<p class='note ok' role='status'>Đã lưu và áp dụng.</p>");
    if (srv.hasArg("cal"))
        o << F("<p class='note ok' role='status'>Nhìn màn hình board và chạm theo hướng dẫn.</p>");

    o << F("<form method='POST' action='/settings' autocomplete='off'><input type='hidden' name='n' value='")
      << web_nonce() << F("'>");

    // ---- Claude token ----
    token_card(o);

    // ---- Weather location ----
    o << F("<section class='card' aria-labelledby='h-w'><h2 id='h-w'><span class='ic' aria-hidden='true'>&#x2600;&#xFE0F;</span>Thời tiết</h2>"
           "<label for='city'>Thành phố</label><select id='city' name='city'>");
    bool matched = false;
    for (const city_t &c : CITIES) {
        bool sel = strcmp(c.name, g_s.city) == 0;
        matched |= sel;
        o << F("<option value='") << c.name << F("' data-lat='") << String(c.lat, 4) << F("' data-lon='")
          << String(c.lon, 4) << "'" << (sel ? " selected" : "") << ">" << c.name << F("</option>");
    }
    o << F("<option value='custom'") << (matched ? "" : " selected") << F(">Tọa độ khác…</option></select>"
           "<div class='grid' style='margin-top:4px'><div><label for='lat'>Vĩ độ</label>"
           "<input id='lat' name='lat' inputmode='decimal' value='") << String(g_s.lat, 4)
      << F("'></div><div><label for='lon'>Kinh độ</label><input id='lon' name='lon' inputmode='decimal' value='")
      << String(g_s.lon, 4) << F("'></div></div></section>");

    // ---- Screen ----
    o << F("<section class='card' aria-labelledby='h-s'><h2 id='h-s'><span class='ic' aria-hidden='true'>&#x1F4A1;</span>Màn hình</h2>"
           "<label for='bright'>Độ sáng</label><div class='range'>"
           "<input id='bright' type='range' name='bright' min='10' max='100' step='5' value='")
      << (int)g_s.brightness << F("'><output for='bright' id='bv'>") << (int)g_s.brightness
      << F("%</output></div><label for='sleep'>Tắt màn hình khi PC không gửi dữ liệu sau</label><select id='sleep' name='sleep'>");
    const uint8_t SLEEP_OPTS[] = {0, 1, 2, 5, 10, 15, 30, 60};
    for (uint8_t m : SLEEP_OPTS)
        select_opt(o, String(m), m ? String(m) + " phút" : String("Không bao giờ tắt"), m == g_s.sleep_min);
    o << F("</select><label for='rotate'>Tự chuyển trang</label><select id='rotate' name='rotate'>");
    const uint8_t ROT_OPTS[] = {0, 5, 10, 15, 20, 30, 60, 120};
    for (uint8_t r : ROT_OPTS) {
        String label = r == 0 ? String("Tắt") : (r < 60 ? String(r) + " giây" : String(r / 60) + " phút");
        select_opt(o, String(r), r ? "Mỗi " + label : label, r == g_s.rotate_sec);
    }
    o << F("</select><p class='hint'>Chạm màn hình sẽ tạm dừng tự chuyển trong 1 phút.</p></section>");

    // ---- Pages: switch + order (up / down) ----
    o << F("<section class='card' aria-labelledby='h-p'><h2 id='h-p'><span class='ic' aria-hidden='true'>&#x1F5C2;&#xFE0F;</span>Các trang</h2>"
           "<ul class='list' id='pages'>");
    for (int pos = 0, shown_n = 0; pos < PG_COUNT; pos++) {
        int id = g_s.order[pos];
        if (id == PG_FUEL) continue;   // now a tab of the prices page
        shown_n++;
        bool on = g_s.enabled & (1 << id);
        String sid(id);
        o << F("<li class='row") << (on ? "" : " off") << F("'><input class='sw' type='checkbox' id='en") << sid
          << F("' name='en") << sid << "'" << (on ? " checked" : "") << F("><label class='name' for='en") << sid
          << "'>" << PAGE_NAMES[id] << F("</label><input type='hidden' name='ord") << sid << F("' value='")
          << shown_n << F("'><span class='mv'><button type='button' data-mv='-1' aria-label='Đưa lên'>&#x2191;</button>"
                          "<button type='button' data-mv='1' aria-label='Đưa xuống'>&#x2193;</button></span></li>");
    }
    o << F("</ul><p class='hint'>Tắt để ẩn trang, dùng mũi tên để đổi thứ tự.</p>"
           "<label for='tet'>Hiện Đếm ngược Tết trước Tết (ngày)</label>"
           "<input id='tet' type='number' name='tet' min='0' max='120' inputmode='numeric' value='")
      << (int)g_s.tet_days
      << F("'><p class='hint'>Trang chỉ xuất hiện từ N ngày trước Tết đến hết mùng 3. 0 = không bao giờ.</p></section>");

    // ---- Personal events ----
    o << F("<section class='card' aria-labelledby='h-e'><h2 id='h-e'><span class='ic' aria-hidden='true'>&#x1F4C5;</span>Sự kiện của bạn</h2>"
           "<p class='hint' style='margin-top:-4px'>Sinh nhật, kỷ niệm, giỗ (âm lịch), chuyến đi… Năm: với sự kiện hằng năm "
           "là năm bắt đầu (để hiện \"lần thứ N\"), có thể bỏ trống; với sự kiện một lần là năm diễn ra.</p>");
    const char *TYPES[3] = {"Dương lịch, hằng năm", "Âm lịch, hằng năm", "Một lần (dương lịch)"};
    int visible = events_count() + 1;
    if (visible > EVENT_MAX) visible = EVENT_MAX;
    for (int i = 0; i < EVENT_MAX; i++) {
        event_t e = {};
        if (i < events_count()) e = events_get(i);
        String si(i);
        o << F("<div class='ev'") << (i < visible ? "" : " hidden") << F("><div class='head'><input name='evn") << si
          << F("' maxlength='38' aria-label='Tên sự kiện' placeholder='Tên sự kiện' value='");
        o.esc(String(e.name));
        o << F("'><button type='button' class='x' data-del aria-label='Xóa sự kiện'>&times;</button></div>"
               "<div class='grid'><select name='evt") << si << F("' aria-label='Loại'>");
        for (int t = 0; t < 3; t++) select_opt(o, String(t), TYPES[t], e.type == t);
        o << F("</select></div><div class='grid'><input type='number' min='1' max='31' inputmode='numeric' name='evd") << si
          << F("' aria-label='Ngày' placeholder='Ngày' value='") << (e.day ? String(e.day) : String(""))
          << F("'><input type='number' min='1' max='12' inputmode='numeric' name='evm") << si
          << F("' aria-label='Tháng' placeholder='Tháng' value='") << (e.month ? String(e.month) : String(""))
          << F("'><input type='number' min='1900' max='2100' inputmode='numeric' name='evy") << si
          << F("' aria-label='Năm' placeholder='Năm' value='") << (e.year ? String(e.year) : String("")) << F("'></div></div>");
    }
    o << F("<button type='button' class='btn ghost' id='addev'>+ Thêm sự kiện</button></section>"
           "<div class='bar'><button class='btn' type='submit'>Lưu</button></div></form>");

    // ---- Touch calibration ----
    o << F("<section class='card' aria-labelledby='h-t' style='margin-top:14px'><h2 id='h-t'><span class='ic' aria-hidden='true'>&#x1F446;</span>Cảm ứng</h2>"
           "<p class='hint' style='margin:-4px 0 12px'>Chạm nửa trái màn hình để lùi trang, nửa phải để sang trang. "
           "Nếu bị ngược hoặc không nhạy, hiệu chỉnh lại.</p>"
           "<form method='POST' action='/touchcal'><input type='hidden' name='n' value='") << web_nonce()
      << F("'><button class='btn sec' type='submit'>Hiệu chỉnh cảm ứng</button></form></section>");

    web_page_end(o,
        // city -> coordinates
        "city.onchange=function(){var o=city.options[city.selectedIndex];if(o.dataset.lat){lat.value=o.dataset.lat;lon.value=o.dataset.lon}};"
        // brightness value
        "bright.oninput=function(){bv.value=bright.value+'%'};"
        // show / hide token
        "document.querySelectorAll('[data-pw]').forEach(function(b){b.onclick=function(){"
        "var i=document.getElementById(b.dataset.pw),h=i.type=='password';i.type=h?'text':'password';"
        "b.textContent=h?'Ẩn':'Hiện';b.setAttribute('aria-label',h?'Ẩn token':'Hiện token')}});"
        // pages: order + dimmed when off
        "function num(){var r=pages.children;for(var i=0;i<r.length;i++){r[i].querySelector('[type=hidden]').value=i+1;"
        "var b=r[i].querySelectorAll('.mv button');b[0].disabled=i==0;b[1].disabled=i==r.length-1}}"
        "pages.onclick=function(e){var b=e.target.closest('[data-mv]');if(!b)return;var li=b.closest('li');"
        "if(b.dataset.mv<0){if(li.previousElementSibling)pages.insertBefore(li,li.previousElementSibling)}"
        "else if(li.nextElementSibling)pages.insertBefore(li.nextElementSibling,li);num();b.focus()};"
        "pages.onchange=function(e){if(e.target.classList.contains('sw'))e.target.closest('li').classList.toggle('off',!e.target.checked)};"
        "num();"
        // events: add / remove
        "addev.onclick=function(){var h=document.querySelector('.ev[hidden]');if(h){h.hidden=false;h.querySelector('input').focus()}"
        "if(!document.querySelector('.ev[hidden]'))addev.hidden=true};"
        "if(!document.querySelector('.ev[hidden]'))addev.hidden=true;"
        "document.querySelectorAll('[data-del]').forEach(function(b){b.onclick=function(){var ev=b.closest('.ev');"
        "ev.querySelectorAll('input').forEach(function(i){i.value=''});"
        "if(document.querySelectorAll('.ev:not([hidden])').length>1)ev.hidden=true;addev.hidden=false}});");
}

static void handle_post()
{
    WebServer &srv = wifi_manager_server();
    if (!web_nonce_ok(srv)) return;

    float lat = srv.arg("lat").toFloat(), lon = srv.arg("lon").toFloat();
    if (lat >= -90 && lat <= 90 && lon >= -180 && lon <= 180 && (lat != 0 || lon != 0)) {
        g_s.lat = lat;
        g_s.lon = lon;
    }
    String city = srv.arg("city");
    if (city == "custom" || city.isEmpty()) city = "Tọa độ tùy chọn";
    else {
        // a preset whose coordinates were edited by hand is no longer that city
        for (const city_t &c : CITIES)
            if (city == c.name && (fabsf(c.lat - g_s.lat) > 0.01f || fabsf(c.lon - g_s.lon) > 0.01f))
                city = "Tọa độ tùy chọn";
    }
    strlcpy(g_s.city, city.c_str(), sizeof(g_s.city));

    int b = srv.arg("bright").toInt();
    if (b >= 10 && b <= 100) g_s.brightness = b;
    int sl = srv.arg("sleep").toInt();
    if (sl >= 0 && sl <= 240) g_s.sleep_min = sl;
    int rot = srv.arg("rotate").toInt();
    if (rot >= 0 && rot <= 240) g_s.rotate_sec = rot;
    int tet = srv.arg("tet").toInt();
    if (tet >= 0 && tet <= 120) g_s.tet_days = tet;

    // pages: enabled flags + order (sort ids by the number sent, ties keep the old order)
    uint16_t en = 0;
    int key[PG_COUNT];
    for (int id = 0; id < PG_COUNT; id++) {
        if (srv.hasArg("en" + String(id))) en |= 1 << id;
        int k = srv.arg("ord" + String(id)).toInt();
        key[id] = k > 0 ? k : 99;
    }
    if (!en) en = 1 << PG_QUOTA;
    g_s.enabled = en;
    uint8_t o[PG_COUNT];
    memcpy(o, g_s.order, PG_COUNT);
    for (int i = 1; i < PG_COUNT; i++) { // stable insertion sort by key
        uint8_t v = o[i];
        int j = i - 1;
        while (j >= 0 && key[o[j]] > key[v]) { o[j + 1] = o[j]; j--; }
        o[j + 1] = v;
    }
    memcpy(g_s.order, o, PG_COUNT);

    // personal events: rows with an empty name are dropped
    event_t evs[EVENT_MAX];
    int nev = 0;
    for (int i = 0; i < EVENT_MAX; i++) {
        String nm = srv.arg("evn" + String(i));
        nm.trim();
        if (nm.isEmpty()) continue;
        event_t e = {};
        strlcpy(e.name, nm.c_str(), sizeof(e.name));
        e.type = (uint8_t)constrain(srv.arg("evt" + String(i)).toInt(), 0, 2);
        e.day = (uint8_t)constrain(srv.arg("evd" + String(i)).toInt(), 0, 31);
        e.month = (uint8_t)constrain(srv.arg("evm" + String(i)).toInt(), 0, 12);
        e.year = (uint16_t)constrain(srv.arg("evy" + String(i)).toInt(), 0, 2100);
        evs[nev++] = e;
    }
    events_set(evs, nev);

    save();
    if (g_changed) g_changed();

    // Claude token: empty = keep the stored one
    const char *tok_q = "";
    String tok = srv.arg("token");
    tok.trim();
    tok.replace(" ", "");   // terminals sometimes wrap long tokens with a stray space / newline
    tok.replace("\r", "");
    tok.replace("\n", "");
    if (tok.length()) {
        bool ok = tok.startsWith("sk-ant-") && tok.length() >= 20 && tok.length() <= 256;
        for (size_t i = 0; ok && i < tok.length(); i++) {
            char c = tok[i];
            ok = isalnum((unsigned char)c) || c == '-' || c == '_';
        }
        if (ok) {
            claude_token_save(tok);
            claude_quota_token_changed();
            if (g_token_cb) g_token_cb();
            tok_q = "&tok=1";
        } else {
            tok_q = "&tokerr=1";
        }
    }

    srv.sendHeader("Location", String("/settings?saved=1") + tok_q, true);
    srv.send(303, "text/plain", "");
}

static void handle_touchcal()
{
    WebServer &srv = wifi_manager_server();
    if (!web_nonce_ok(srv)) return;
    if (g_touchcal) g_touchcal();
    srv.sendHeader("Location", "/settings?cal=1", true);
    srv.send(303, "text/plain", "");
}

void settings_register_routes()
{
    WebServer &srv = wifi_manager_server();
    srv.on("/settings", HTTP_GET, handle_get);
    srv.on("/settings", HTTP_POST, handle_post);
    srv.on("/touchcal", HTTP_POST, handle_touchcal);
}
