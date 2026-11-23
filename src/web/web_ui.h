#pragma once
#include <Arduino.h>

// Forward declaration only (see wifi_manager.h: <WebServer.h> must not be
// pulled into main.cpp).
class WebServer;

// Shared look and plumbing of the board's web pages (/settings, WiFi setup):
//   - one stylesheet at /ui.css (cached by the browser, sent once)
//   - HtmlOut: streams a page in ~1 KB chunks instead of building one big
//     String (the heap is shared with LVGL and the HTTPS fetches)
//   - a per-boot nonce in every POST form, so another website open in the
//     same browser cannot submit the board's forms (CSRF)

void web_ui_register(WebServer &srv);   // GET /ui.css

const char *web_nonce();                // value of the hidden field "n"
bool web_nonce_ok(WebServer &srv);      // false -> a 403 page has already been sent

String web_esc(const String &s);        // HTML text / attribute escaping

class HtmlOut {
public:
    explicit HtmlOut(WebServer &srv);   // sends the 200 header (chunked)
    ~HtmlOut();                         // ends the response if end() was not called
    HtmlOut &operator<<(const char *s);
    HtmlOut &operator<<(const String &s);
    HtmlOut &operator<<(const __FlashStringHelper *s);
    HtmlOut &operator<<(long v);
    HtmlOut &operator<<(int v) { return *this << (long)v; }
    HtmlOut &esc(const String &s) { return *this << web_esc(s); }
    void end();

private:
    void flush();
    WebServer &srv_;
    String buf_;
    bool ended_ = false;
};

enum web_nav_t { WEB_NAV_NONE, WEB_NAV_SETTINGS, WEB_NAV_WIFI };

// <head>, brand bar and (unless WEB_NAV_NONE) the Cài đặt / WiFi tabs; opens <main>.
void web_page_begin(HtmlOut &o, const char *title, web_nav_t active);
// Closes <main>; `script` (optional) goes in a <script> at the end of <body>.
void web_page_end(HtmlOut &o, const char *script = nullptr);

// Small full page with a title, a message and a link back.
void web_send_message(WebServer &srv, int code, const char *title, const char *msg,
                      const char *back_href, const char *back_text);
