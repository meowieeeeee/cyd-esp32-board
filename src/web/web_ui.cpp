#include "web_ui.h"
#include <WebServer.h>
#if __has_include(<esp_random.h>)
#include <esp_random.h>          // ESP-IDF 5
#else
#include <esp_system.h>          // ESP-IDF 4: esp_random() lives here
#endif

// ---- Stylesheet (design tokens match the screen: theme.h) ----

static const char CSS[] PROGMEM = R"CSS(
:root{--bg:#0b0b0c;--card:#17151c;--card2:#211e28;--line:#2e2a37;--text:#fbfaf7;--dim:#c9c5d1;
--faint:#8b8794;--accent:#d97757;--accent2:#e8916f;--ok:#8cc152;--warn:#f6c945;--err:#e0563b;
--r:16px;--r2:10px;color-scheme:dark}
*{box-sizing:border-box}
[hidden]{display:none!important}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--text);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
a{color:var(--accent2)}
.top{position:sticky;top:0;z-index:5;background:rgba(11,11,12,.92);backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.top-in{max-width:560px;margin:0 auto;padding:0 16px;display:flex;flex-wrap:wrap;align-items:center;gap:0 12px;min-height:52px}
.brand{display:flex;align-items:center;gap:10px;font-weight:650;font-size:17px;padding:12px 0}
.logo{width:26px;height:18px;border-radius:4px;background:var(--accent);position:relative}
.logo:before,.logo:after{content:"";position:absolute;top:6px;width:4px;height:4px;background:var(--bg);border-radius:1px}
.logo:before{left:6px}.logo:after{right:6px}
.tabs{display:flex;gap:4px;margin-left:auto}
.tabs{align-self:stretch}.tabs a{display:flex;align-items:center;color:var(--faint);text-decoration:none;padding:0 12px;border-bottom:2px solid transparent;font-weight:550}
.tabs a[aria-current=page]{color:var(--text);border-color:var(--accent)}
main{max-width:560px;margin:0 auto;padding:16px 16px 40px}
h1{font-size:22px;margin:4px 0 14px}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);padding:16px;margin:0 0 14px}
.card>h2{display:flex;align-items:center;gap:8px;font-size:15px;margin:0 0 12px;letter-spacing:.01em}
.card>h2 .ic{width:22px;height:22px;border-radius:7px;background:var(--card2);display:grid;place-items:center;font-size:13px}
.hint{color:var(--faint);font-size:13px;margin:6px 0 0}
label{display:block;color:var(--dim);font-size:13px;margin:12px 0 6px}
label:first-child{margin-top:0}
input,select{width:100%;min-height:44px;padding:10px 12px;border-radius:var(--r2);border:1px solid var(--line);
background:var(--card2);color:var(--text);font:inherit;outline:none}
input:focus-visible,select:focus-visible,button:focus-visible,a:focus-visible{outline:2px solid var(--accent);outline-offset:2px}
input::placeholder{color:#6f6b78}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(0,1fr));gap:8px}
.pw{position:relative}.pw input{padding-right:64px}
.pw button{position:absolute;right:6px;top:6px;height:32px;padding:0 10px;border-radius:8px;border:0;background:transparent;color:var(--faint);font:inherit;font-size:13px;cursor:pointer}
.pill{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:600;padding:3px 9px;border-radius:99px;background:var(--card2);color:var(--dim);margin-left:auto}
.pill:before{content:"";width:7px;height:7px;border-radius:50%;background:currentColor}
.pill.ok{color:var(--ok)}.pill.warn{color:var(--warn)}.pill.err{color:var(--err)}
.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px;color:var(--dim);word-break:break-all}
code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;background:var(--card2);padding:1px 6px;border-radius:6px;color:var(--text)}
.range{display:flex;align-items:center;gap:12px}.range output{min-width:44px;text-align:right;font-variant-numeric:tabular-nums}
input[type=range]{padding:0;min-height:32px;background:none;border:0;accent-color:var(--accent)}
.list{border:1px solid var(--line);border-radius:var(--r2);overflow:hidden;list-style:none;margin:0;padding:0}
.row{display:flex;align-items:center;gap:10px;padding:8px 8px 8px 12px;background:var(--card2);border-top:1px solid var(--line)}
.row:first-child{border-top:0}
.row .name{flex:1;min-width:0;margin:0;color:var(--text);font-size:15px;cursor:pointer}
.row.off .name{color:var(--faint)}
.mv{display:flex;gap:4px}
.mv button{width:36px;height:36px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--dim);font-size:15px;cursor:pointer}
.mv button:disabled{opacity:.3;cursor:default}
.sw{appearance:none;-webkit-appearance:none;flex:none;width:42px;min-height:0;height:24px;padding:0;border-radius:99px;
border:0;background:#3a3547;position:relative;cursor:pointer;transition:background .15s}
.sw:before{content:"";position:absolute;top:3px;left:3px;width:18px;height:18px;border-radius:50%;background:#fff;transition:transform .15s}
.sw:checked{background:var(--accent)}.sw:checked:before{transform:translateX(18px)}
.ev{background:var(--card2);border:1px solid var(--line);border-radius:var(--r2);padding:12px;margin-top:10px}
.ev .head{display:flex;gap:8px}
.ev .head input{flex:1}
.ev .grid{margin-top:8px}
.x{flex:none;width:44px;border-radius:var(--r2);border:1px solid var(--line);background:var(--card);color:var(--faint);font-size:18px;cursor:pointer}
.btn{display:inline-flex;justify-content:center;align-items:center;gap:8px;width:100%;min-height:48px;padding:12px 16px;border:0;
border-radius:12px;background:var(--accent);color:#1b0f0a;font:inherit;font-weight:650;cursor:pointer;text-decoration:none}
.btn:hover{background:var(--accent2)}
.btn.sec{background:var(--card2);color:var(--text);border:1px solid var(--line)}
.btn.ghost{background:transparent;color:var(--accent2);border:1px dashed var(--line);margin-top:10px;min-height:44px}
.bar{position:sticky;bottom:0;margin:0 -16px;padding:12px 16px calc(12px + env(safe-area-inset-bottom));
background:linear-gradient(transparent,var(--bg) 30%)}
.note{border-radius:12px;padding:12px 14px;margin:0 0 14px;font-weight:550}
.note.ok{background:#16240f;color:#b9e39a;border:1px solid #2b4220}
.note.err{background:#2a1210;color:#f3a596;border:1px solid #4a201b}
.nets{display:grid;gap:6px;margin:4px 0 0;padding:0;list-style:none}
.net{display:flex;align-items:center;gap:12px;width:100%;min-height:48px;padding:10px 12px;border-radius:var(--r2);
border:1px solid var(--line);background:var(--card2);color:var(--text);font:inherit;text-align:left;cursor:pointer}
.net[aria-pressed=true]{border-color:var(--accent);box-shadow:0 0 0 1px var(--accent) inset}
.net .n{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px}
.bars i{width:3px;background:#4a4555;border-radius:1px}.bars i.on{background:var(--dim)}
.bars i:nth-child(1){height:4px}.bars i:nth-child(2){height:7px}.bars i:nth-child(3){height:10px}.bars i:nth-child(4){height:14px}
.lock{color:var(--faint);font-size:12px}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:14px}.kv dt{color:var(--faint)}.kv dd{margin:0}
.center{text-align:center;padding-top:12vh}.center h1{font-size:24px}
)CSS";

static WebServer *g_srv = nullptr;

void web_ui_register(WebServer &srv)
{
    g_srv = &srv;
    srv.on("/ui.css", HTTP_GET, []() {
        g_srv->sendHeader("Cache-Control", "public, max-age=86400");
        g_srv->send_P(200, "text/css", CSS);
    });
}

// ---- Nonce ----

static char g_nonce[17] = "";

const char *web_nonce()
{
    if (!g_nonce[0]) {
        uint32_t a = esp_random(), b = esp_random();
        snprintf(g_nonce, sizeof(g_nonce), "%08lx%08lx", (unsigned long)a, (unsigned long)b);
    }
    return g_nonce;
}

bool web_nonce_ok(WebServer &srv)
{
    if (srv.arg("n") == web_nonce()) return true;
    web_send_message(srv, 403, "Trang đã cũ",
                     "Board đã khởi động lại hoặc biểu mẫu không đến từ board. Tải lại trang rồi thử lại.",
                     "/", "Tải lại");
    return false;
}

String web_esc(const String &s)
{
    String o;
    o.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '&') o += "&amp;";
        else if (c == '\'') o += "&#39;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

// ---- HtmlOut ----

#define CHUNK 1024

HtmlOut::HtmlOut(WebServer &srv) : srv_(srv)
{
    buf_.reserve(CHUNK + 256);
    srv_.sendHeader("Cache-Control", "no-store");
    srv_.setContentLength(CONTENT_LENGTH_UNKNOWN);
    srv_.send(200, "text/html; charset=utf-8", "");
}

HtmlOut::~HtmlOut() { end(); }

void HtmlOut::flush()
{
    if (buf_.length()) {
        srv_.sendContent(buf_);
        buf_ = "";
    }
}

HtmlOut &HtmlOut::operator<<(const char *s)
{
    buf_ += s;
    if (buf_.length() >= CHUNK) flush();
    return *this;
}

HtmlOut &HtmlOut::operator<<(const String &s)
{
    buf_ += s;
    if (buf_.length() >= CHUNK) flush();
    return *this;
}

HtmlOut &HtmlOut::operator<<(const __FlashStringHelper *s)
{
    buf_ += s;
    if (buf_.length() >= CHUNK) flush();
    return *this;
}

HtmlOut &HtmlOut::operator<<(long v)
{
    buf_ += String(v);
    return *this;
}

void HtmlOut::end()
{
    if (ended_) return;
    ended_ = true;
    flush();
    srv_.sendContent("");   // last chunk
}

// ---- Page frame ----

void web_page_begin(HtmlOut &o, const char *title, web_nav_t active)
{
    o << F("<!DOCTYPE html><html lang='vi'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
           "<meta name='theme-color' content='#0b0b0c'><title>")
      << title
      << F(" · Clawd</title><link rel='stylesheet' href='/ui.css'></head><body>"
           "<header class='top'><div class='top-in'><div class='brand'><span class='logo' aria-hidden='true'></span>Clawd</div>");
    if (active != WEB_NAV_NONE) {
        o << F("<nav class='tabs' aria-label='Trang'><a href='/settings'")
          << (active == WEB_NAV_SETTINGS ? " aria-current='page'" : "")
          << F(">Cài đặt</a><a href='/wifi'") << (active == WEB_NAV_WIFI ? " aria-current='page'" : "")
          << F(">WiFi</a></nav>");
    }
    o << F("</div></header><main>");
}

void web_page_end(HtmlOut &o, const char *script)
{
    o << F("</main>");
    if (script) o << F("<script>") << script << F("</script>");
    o << F("</body></html>");
    o.end();
}

void web_send_message(WebServer &srv, int code, const char *title, const char *msg,
                      const char *back_href, const char *back_text)
{
    String p;
    p.reserve(900);
    p += F("<!DOCTYPE html><html lang='vi'><head><meta charset='utf-8'>"
           "<meta name='viewport' content='width=device-width,initial-scale=1'><title>");
    p += title;
    p += F("</title><link rel='stylesheet' href='/ui.css'></head><body><main class='center'><h1>");
    p += title;
    p += F("</h1><p class='hint' style='font-size:15px'>");
    p += msg;
    p += F("</p>");
    if (back_href) {
        p += F("<p style='margin-top:24px'><a class='btn sec' href='");
        p += back_href;
        p += F("'>");
        p += back_text;
        p += F("</a></p>");
    }
    p += F("</main></body></html>");
    srv.sendHeader("Cache-Control", "no-store");
    srv.send(code, "text/html; charset=utf-8", p);
}
